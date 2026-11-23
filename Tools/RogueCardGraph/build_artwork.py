"""Publish registered and draft card artwork for the local graph."""

import hashlib
import json
from io import BytesIO
from pathlib import Path
from tempfile import NamedTemporaryFile

from PIL import Image
from formal_artwork import make_formal_face


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
UI = ROOT / "ArtSource/UI"
FACES = UI / "GroundMechRogueCards/AbilityCards_HTML_20261120_v1"
ICONS = UI / "GroundMech/RogueCardIcons_20261121_v1"
DEST = HERE / "artwork"
CARD_IDS = {
    "electromagnetic": "16.01",
    "split_barrel": "15.01",
    "ai_engine": "14.01",
    "aoe": "17.01",
    "demon": "draft-7ededb47-9902-4560-83b4-a3c824375246",
}


def read_image(root, entry):
    path = root / entry["file"]
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != entry["sha256"]:
        raise ValueError(f"Published image differs from its manifest: {path}")
    with Image.open(BytesIO(data)) as image:
        return image.convert("RGBA")


def write_atomic(path, data):
    if path.exists() and path.read_bytes() == data:
        return
    temporary = None
    try:
        with NamedTemporaryFile(dir=path.parent, delete=False) as output:
            temporary = Path(output.name)
            output.write(data)
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def export(key, role, image):
    buf = BytesIO()
    image.save(buf, format="WEBP", lossless=True, method=4)
    data = buf.getvalue()
    version = hashlib.sha256(data).hexdigest()[:16]
    name = f"{key}-{role}.{version}.webp"
    write_atomic(DEST / name, data)
    return "/artwork/" + name


def registered_cards():
    def rows(table):
        return json.loads((ROOT / "data/Json" / f"DT_GuLiStrike{table}.json").read_text(encoding="utf-8-sig"))

    artworks = {r["Material"]: r for r in rows("CardArtwork_Artworks")}
    templates = {r["Id"]: r["Name"] for r in rows("CardArtwork_Templates")}
    layer_rows = rows("CardArtwork_Layers")
    parameters = rows("CardArtwork_Parameters")
    icon_sources = json.loads((UI / "GroundMech/Icons/catalog.json").read_text(encoding="utf-8-sig"))
    source_by_icon = {r["resource_path"]: r for r in icon_sources["icons"]}
    icon_paths = {r["Id"]: r["ResourcePath"] for r in rows("Icons_Icons")}
    result = {}
    for card in rows("RogueCards_Cards"):
        art = artworks.get(card["FrontMaterial"])
        key = card["Name"].lower()
        media = {}
        if art and templates[art["TemplateId"]] != "BlankPlaceholder":
            template = templates[art["TemplateId"]]
            layers = [r for r in layer_rows if r["ArtworkId"] == art["Id"]]
            params = {r["Key"]: r for r in parameters if r["ArtworkId"] == art["Id"]}
            poster, atlas, window, motion = make_formal_face(ROOT, art, template, layers, params)
            media.update(face=export(key, "face", poster), layers=export(key, "layers", atlas),
                         window=window, motion=motion)
        if card["IconId"]:
            source = source_by_icon.get(icon_paths.get(card["IconId"]))
            if source:
                icon = read_image(ROOT, {"file": source["source_png"], "sha256": source["sha256"]})
                icon.thumbnail((192, 192), Image.Resampling.LANCZOS)
                media["icon"] = export(key, "icon", icon)
        if media:
            result[card["Id"]] = media
    return result


def damage_over_time_artwork():
    source = UI / "GroundMechRogueCards/DamageOverTime_20261123_v1"
    manifest = json.loads((source / "generation.json").read_text(encoding="utf-8"))
    parallax_file = source / manifest["parallax"]["manifest"]
    if hashlib.sha256(parallax_file.read_bytes()).hexdigest() != manifest["parallax"]["sha256"]:
        raise ValueError("Hourglass layer manifest does not match its published revision")
    root = parallax_file.parent
    art = json.loads(parallax_file.read_text(encoding="utf-8"))
    face = read_image(root, art["poster"])
    icon = read_image(source, manifest["icon"])
    icon.thumbnail((192, 192), Image.Resampling.LANCZOS)
    x, y, width, height = art["window"]
    tile = tuple(n // 2 for n in art["canvas"])
    face = face.resize((width // 2, height // 2), Image.Resampling.LANCZOS)
    layers = {layer["index"]: read_image(root, layer) for layer in art["layers"]}
    mask = read_image(root, art["mask"]).convert("L")
    passes = list(reversed(art["passes"]))
    atlas = Image.new("RGBA", (3 * tile[0], ((len(passes) + 2) // 3) * tile[1]))
    shifts = []
    for i, draw in enumerate(passes):
        n = draw["layer"]
        image = layers[n]
        if "part" in draw:
            image = image.copy()
            a = image.getchannel("A")
            zero = Image.new("L", a.size)
            image.putalpha(Image.composite(a, zero, mask) if draw["part"] == "front" else Image.composite(zero, a, mask))
        atlas.paste(image.resize(tile, Image.Resampling.LANCZOS), ((i % 3) * tile[0], (i // 3) * tile[1]))
        step = art["defaultTravel"] * art["depth"][n - 1] / 2
        shifts.append([step, step])
    return "18.01", {
        "face": export("damage_over_time", "face", face),
        "icon": export("damage_over_time", "icon", icon),
        "layers": export("damage_over_time", "layers", atlas),
        "window": [x / 2, y / 2, width / 2, height / 2],
        "motion": {"tile": tile, "shifts": shifts, "tilt": art["maxTilt"]},
        "caption": "V8 六层视差 · 待美术审核" if art["meetsTarget"] else "V8 六层试稿 · 原位保真待校准",
    }


def build():
    faces = json.loads((FACES / "manifest.json").read_text(encoding="utf-8"))
    icons = json.loads((ICONS / "manifest.json").read_text(encoding="utf-8"))
    by_key = {c["key"]: c for c in faces["cards"]}
    icon_by_key = {c["card"]: c for c in icons["icons"]}
    width, height = faces["canvas"]
    tile = (width // 2, height // 2)
    if (tile[0] * 2, tile[1] * 2) != (width, height) or len(faces["depth"]) != 6:
        raise ValueError("Expected an even-sized six-layer source canvas")
    result = {"tile": tile, "depth": faces["depth"], "travel": faces["defaultTravel"] / 2,
              "tilt": faces["maxTilt"], "cards": {}}
    DEST.mkdir(exist_ok=True)
    for key, card_id in CARD_IDS.items():
        card = by_key[key]
        icon = read_image(ICONS, icon_by_key[key])
        icon.thumbnail((192, 192), Image.Resampling.LANCZOS)
        layers = sorted(card["layers"], key=lambda layer: layer["index"])
        if [layer["index"] for layer in layers] != list(range(1, 7)):
            raise ValueError(f"Incomplete card face: {key}")
        # Three columns, two rows. L1 is the nearest layer, including for parallax.
        atlas = Image.new("RGBA", (tile[0] * 3, tile[1] * 2))
        stack = Image.new("RGBA", (width, height))
        for layer in reversed(layers):
            image = read_image(FACES, layer)
            if image.size != stack.size:
                raise ValueError(f"Unregistered card layer: {layer['file']}")
            stack.alpha_composite(image)
            i = layer["index"] - 1
            atlas.paste(image.resize(tile, Image.Resampling.LANCZOS), ((i % 3) * tile[0], (i // 3) * tile[1]))
        x, y, w, h = card["window"]
        poster = stack.convert("RGB").resize((round(w / 2), round(h / 2)), Image.Resampling.LANCZOS,
                                              box=(x, y, x + w, y + h))
        result["cards"][card_id] = {
            "icon": export(key, "icon", icon), "face": export(key, "face", poster),
            "layers": export(key, "layers", atlas), "window": [v / 2 for v in card["window"]],
        }
    result["cards"].update(registered_cards())
    card_id, media = damage_over_time_artwork()
    result["cards"][card_id] = media
    write_atomic(DEST / "catalog.json", (json.dumps(result, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    cards = result["cards"].values()
    print(f"Published {sum('face' in c for c in cards)} faces, {sum('icon' in c for c in cards)} icons "
          f"and {sum('layers' in c for c in cards)} parallax atlases to {DEST}")


if __name__ == "__main__":
    build()
