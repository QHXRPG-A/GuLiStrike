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
    "aoe": "draft-c36c227b-ba6f-4ac4-8c16-d9e80c20de27",
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
    reference_root = UI / "GroundMechRogueCards/AbilityReferences_20261120_v7"
    generation = json.loads((reference_root / "generation.json").read_text(encoding="utf-8"))
    hourglass = next(row for row in generation["cards"] if row["title"] == "增强持续伤害")
    image = read_image(reference_root, {"file": hourglass["file"], "sha256": hourglass["artSHA256"]})
    image.thumbnail((650, 650), Image.Resampling.LANCZOS)
    result["cards"]["draft-3f4be030-0a25-43fa-baf2-ab56f1eebddf"] = {
        "face": export("damage_over_time", "face", image), "window": [0, 0, *image.size],
        "caption": f"参考稿 · {generation['review']}",
    }
    write_atomic(DEST / "catalog.json", (json.dumps(result, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    cards = result["cards"].values()
    print(f"Published {sum('face' in c for c in cards)} faces, {sum('icon' in c for c in cards)} icons "
          f"and {sum('layers' in c for c in cards)} parallax atlases to {DEST}")


if __name__ == "__main__":
    build()
