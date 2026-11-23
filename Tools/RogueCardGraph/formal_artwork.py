"""Map registered card layers to the graph's illustration window using their material parameters."""

import json
import math
import os
from pathlib import Path
from tempfile import gettempdir

from PIL import Image, ImageChops, ImageDraw


def load_texture(root, parameter):
    source = parameter["SourceFile"].replace("\\", "/")
    # Older import metadata records paths such as D:/../../../ArtSource/...
    _, marker, relative = source.partition("ArtSource/")
    if marker:
        path = root / marker / relative
        if path.is_file():
            with Image.open(path) as image:
                return image.convert("RGBA")
    cache = Path(os.environ.get("GULI_GRAPH_TEXTURE_CACHE", Path(gettempdir()) / "GuLiStrike-GraphTextures"))
    index = cache / "textures.json"
    path = parameter["Texture"]
    files = json.loads(index.read_text(encoding="utf-8")) if index.is_file() else {}
    if path not in files or not (cache / files[path]).is_file():
        raise FileNotFoundError(f"{path}: run Scripts/Cards/export_graph_artwork_textures.py in Unreal first")
    with Image.open(cache / files[path]) as image:
        return image.convert("RGBA")


def material_window(root, art, template, base_layer, params):
    if "Artwork Aperture" in params:
        aperture = params["Artwork Aperture"]
        # The formal shader extends two thousandths of a UV unit beneath the physical frame.
        left, top = aperture["VectorR"] + .002, aperture["VectorG"] + .002
        right, bottom = aperture["VectorB"] - .002, aperture["VectorA"] - .002
    elif template in ("RegisteredSixForegroundMask", "RegisteredSixRigid"):
        # These review pages show the entire square illustration inside the six registered layers.
        manifest = json.loads((root / art["SourceRoot"] / "manifest.json").read_text(encoding="utf-8-sig"))
        width, height = manifest["canvas"]
        x0, y0, x1, y1 = manifest["source_window"]
        scale_x, scale_y = base_layer["ScaleU"], base_layer["ScaleV"]
        cx, cy = .5 + base_layer["OffsetU"], .5 + base_layer["OffsetV"]
        return ((cx + (x0 / width - .5) * scale_x, cy + (y0 / height - .5) * scale_y,
                 cx + (x1 / width - .5) * scale_x, cy + (y1 / height - .5) * scale_y),
                (x1 - x0) / (y1 - y0))
    else:
        # Visible opening in the 30 x 45 legacy commander card mesh.
        left, top, right, bottom = .052, .050, .948, .765
    return (left, top, right, bottom), (right - left) / (bottom - top) * 30 / 45


def passes_for(template, params):
    normal = [6, 5, 4, 3, 2, 1]
    if template == "RegisteredSix":
        return [6, 5, 4, 1, 3, 2], None
    if template == "RegisteredSixRigidProjectileFront":
        return [6, 5, 4, 1, 2, 3], None
    if template == "RegisteredSixForegroundMask":
        return [6, 4, 5, -1, 3, 1, 2], 1
    if params.get("Layer 3 Split Enabled", {}).get("ScalarValue"):
        return [6, -3, 4, 5, 3, 2, 1], 3
    if params.get("Ring Occlusion Enabled", {}).get("ScalarValue"):
        return [6, 5, -3, 4, 3, 2, 1], 3
    if template == "ModelComicAtlasV9" and params["Ability behind machinery"]["ScalarValue"]:
        normal[3:5] = [2, 3]
    return normal, None


def prepare_sources(root, template, layers, params):
    planes = {}
    atlas = load_texture(root, params["BaseColor Map"]) if template == "ModelComicAtlasV9" else None
    for index, layer in layers.items():
        if layer["Opacity"] == 0:
            continue
        if atlas is None:
            image = load_texture(root, params[layer["TextureParameter"]])
        else:
            if atlas.width % 3 or atlas.height % 2:
                raise ValueError("Invalid 3 x 2 commander atlas")
            w, h = atlas.width // 3, atlas.height // 2
            x, y = ((index - 1) % 3) * w, ((index - 1) // 3) * h
            image = atlas.crop((x, y, x + w, y + h))
        if index == 6 and template in ("RegisteredSix", "ModelComicAtlasV9"):
            image.putalpha(255)  # The legacy materials take the base layer's RGB directly.
        elif index == 3 and template in ("RegisteredSix", "ModelComicAtlasV9"):
            def machinery_alpha(value):
                t = min(1, max(0, (value / 255 - .12) / .60))
                return round(255 * t * t * (3 - 2 * t))
            image.putalpha(image.getchannel("A").point(machinery_alpha))
        if layer["Opacity"] != 1:
            image.putalpha(image.getchannel("A").point(lambda value: round(value * layer["Opacity"])))
        planes[index] = image
    order, split = passes_for(template, params)
    if split:
        source = planes[split]
        if params.get("Ring Occlusion Enabled", {}).get("ScalarValue"):
            plane = params["Ring Occlusion Plane"]
            slope, intercept = plane["VectorR"], plane["VectorG"]
            # The authored plane is measured from (256, 256) in this 1536-square ring texture.
            front = Image.new("L", source.size)
            y0 = 256 + intercept - 256 * slope
            w, h = source.size
            ImageDraw.Draw(front).polygon([(0, y0), (w, y0 + slope * w), (w, h), (0, h)], fill=255)
        else:
            front = load_texture(root, params["Foreground Front Mask"]).getchannel("R")
            if template == "RegisteredSixForegroundMask":
                front = front.point(lambda value: 255 if value >= 128 else 0)
        alpha = source.getchannel("A")
        planes[-split] = source.copy()
        planes[-split].putalpha(ImageChops.multiply(alpha, ImageChops.invert(front)))
        source.putalpha(ImageChops.multiply(alpha, front))
    return planes, [index for index in order if index in planes]


def make_formal_face(root, art, template, layer_rows, params):
    layers = {row["LayerIndex"]: row for row in layer_rows}
    if set(layers) != set(range(1, 7)):
        raise ValueError(f"Incomplete registered layers for {art['Name']}")
    planes, order = prepare_sources(root, template, layers, params)
    (left, top, right, bottom), aspect = material_window(root, art, template, layers[6], params)
    width = 600
    height = round(width / aspect)
    px_u, px_v = width / (right - left), height / (bottom - top)
    tilt = 16
    strength = params["Layers global depth"]["ScalarValue"]
    travel = params.get("Parallax Travel")
    motions = []
    for index in order:
        layer = layers[abs(index)]
        owner = layer["MotionOwnerLayer"] or abs(index)
        depth = layers[owner]["Depth"]
        if travel:
            dx, dy = travel["VectorR"] * layer["ScaleU"], travel["VectorG"] * layer["ScaleV"]
        elif template in ("ModelComicAtlasV9", "RegisteredSix"):
            # A restrained displacement for the older bump-offset materials in this flat browser preview.
            dx = dy = .014
            depth *= .25
        else:
            dx = dy = -math.tan(math.radians(tilt))
        motions.append((round(px_u * dx * strength * depth, 3), round(px_v * dy * strength * depth, 3)))

    pad = math.ceil(max(abs(v) for delta in motions for v in delta)) + 2
    tile = (width + 2 * pad, height + 2 * pad)
    uv = (left - pad / px_u, top - pad / px_v, right + pad / px_u, bottom + pad / px_v)
    stack = Image.new("RGBA", tile)
    atlas = Image.new("RGBA", (3 * tile[0], math.ceil(len(order) / 3) * tile[1]))
    for position, index in enumerate(order):
        layer, image = layers[abs(index)], planes[index]
        box = [((value - .5 - layer["Offset" + axis]) / layer["Scale" + axis] + .5) * length
               for value, axis, length in zip(uv, "UVUV", image.size * 2)]
        plane = image.transform(tile, Image.Transform.EXTENT, box, Image.Resampling.BICUBIC)
        if template == "RegisteredSix" and index == 6:
            margin = load_texture(root, params["Background Margin Map"])
            margin_box = [(v / size + .25) / 1.5 * out for v, size, out in zip(box, image.size * 2, margin.size * 2)]
            outer = margin.transform(tile, Image.Transform.EXTENT, margin_box, Image.Resampling.BICUBIC)
            outer.alpha_composite(plane)
            plane = outer
        stack.alpha_composite(plane)
        i = len(order) - 1 - position  # Atlas cells and offsets are both stored nearest first.
        atlas.paste(plane, ((i % 3) * tile[0], (i // 3) * tile[1]))
    poster = stack.crop((pad, pad, pad + width, pad + height))
    motion = {"tile": tile, "shifts": list(reversed(motions)), "tilt": tilt}
    return poster, atlas, [pad, pad, width, height], motion
