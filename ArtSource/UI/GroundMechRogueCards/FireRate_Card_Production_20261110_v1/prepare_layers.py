"""Freeze the selected card and prepare coordinate anchors for six model edits."""

import hashlib
import json
from pathlib import Path
import shutil

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as ndi


ROOT = Path(__file__).resolve().parent
REFERENCE = ROOT.parent / "FireRate_Reference_20261110_v7" / "FireRate_Card_Reference_v7.png"
SIZE = (1152, 1728)
WINDOW = (64, 96, 1088, 1632)
LAYER_NAMES = (
    ("01_DustNear", "近处尘埃 · 前"),
    ("02_DustBehind", "近处尘埃 · 后"),
    ("03_Lightning", "闪电"),
    ("04_GunAndMech", "枪管和机甲身体"),
    ("05_MuzzleFlame", "枪口火焰与弹道"),
    ("06_Background", "背景"),
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def polygon_mask(size, vertices):
    image = Image.new("1", size)
    ImageDraw.Draw(image).polygon(vertices, fill=1)
    return np.asarray(image, dtype=bool)


def owner_map(rgb):
    """Assign each visible source pixel once. These are production anchors."""
    height, width = rgb.shape[:2]
    y, x = np.ogrid[:height, :width]
    red, green, blue = np.moveaxis(rgb.astype(np.int16), -1, 0)
    backdrop = (red < 21) & (green >= 51) & (green <= 81) & (
        blue - green >= 24
    ) & (blue - green <= 47)
    body_area = polygon_mask((width, height), [
        (0, 209), (121, 266), (225, 360), (281, 504), (334, 427),
        (462, 390), (575, 407), (700, 474), (765, 550), (798, 656),
        (810, 804), (766, 923), (687, 1007), (557, 1054), (442, 1114),
        (341, 1169), (278, 1212), (286, 1295), (245, 1389), (0, 1465),
    ])
    dust_start = np.interp(
        np.arange(width), [0, 130, 245, 380, 565, 720, 1023],
        [1355, 1295, 1215, 1140, 1035, 974, 960],
    )
    dust = (y >= dust_start) & (red > 67) & (red - green >= 7) & (green - blue >= 10)
    # A lower sheet and an overlapping upper crest are separately completed.
    overlap_line = np.interp(
        np.arange(width), [0, 155, 307, 501, 704, 874, 1023],
        [1480, 1439, 1383, 1312, 1232, 1179, 1128],
    )
    near = dust & (y >= overlap_line)
    labels, count = ndi.label(near, structure=np.ones((3, 3), dtype=bool))
    roots = np.unique(labels[-1])
    near = np.isin(labels, roots[roots > 0])
    far = dust & ~near

    fire_zone = ((x >= 620) & (y >= 643) & (y <= 807)) | (
        (x >= 746) & (y >= 386) & (y <= 807)
    )
    fire = fire_zone & (red >= 143) & (red - green >= 7) & (green - blue >= 10)
    fire |= ndi.binary_fill_holes(fire) & fire_zone & (red > 155)

    # White-cyan filaments and their antialiased neighbours. Solid pale-blue
    # paint planes are assigned to the armour rather than the discharge.
    cool_core = (red > 171) & (green > 207) & (blue > 222) & (
        green >= red - 2
    ) & (blue >= red + 3) & (y < 1190)
    broad_paint = ndi.binary_opening(cool_core & body_area, iterations=5)
    core = cool_core & ~broad_paint
    blue_edge = (green > 113) & (blue > 147) & (blue >= green - 3) & (
        green >= red + 7
    )
    lightning = core | (ndi.maximum_filter(core, size=5) & blue_edge & ~broad_paint)
    lightning |= (~body_area & ~backdrop & (y < 1180) & (blue > 129) &
                  (green > 82) & (blue > red + 22) & ~fire & ~dust)
    lightning &= ~fire & ~dust

    body = body_area & ~backdrop & ~fire & ~dust & ~lightning
    body = ndi.binary_fill_holes(body | (body_area & lightning))
    body &= body_area & ~fire & ~dust
    owners = np.full((height, width), 6, dtype=np.uint8)
    owners[body] = 4
    owners[fire] = 5
    owners[lightning] = 3
    owners[far] = 2
    owners[near] = 1
    return owners


COMMON = """Use case: complete one independently registered game-card art layer.
Image 1 is this layer's incomplete isolated anchor on the entire 1152 by 1728 working canvas. The original 1024 by 1536 art window occupies [64, 96, 1088, 1632]. The surrounding gutter is new art space. Keep this entire coordinate frame and the exact existing position, size, and direction of the visible anchor pixels. Preserve all negative space. Do not crop, recenter, zoom, rotate, or fit to the object. Image 2 is the frozen full reference for design, style, and hidden relationships only.

Use the supplied reference's crisp cel-painted planes, clean ink outlines, blue and cream A4B2 armour, angular sand, and directed recoil. Complete only this one layer. Restore any portions hidden by other layers and genuinely paint the continuation of shapes that cross the old window into the outer gutter. Preserve the original visible core and shape anchors. Nothing from unrelated layers belongs in the output. No labels, border, checkerboard or UI.
"""

INSTRUCTIONS = (
    """The nearest dust sheet ONLY. Continue the dense ochre sand across the very bottom in wide flat angular planes and rear-swept brush shapes, with the same diagonal blast direction. The input is the lower foreground mass; complete its upper and hidden surfaces into one coherent solid dust sheet, not a sliced stripe. Continue it naturally through the bottom gutter, and beyond left/right wherever it was cropped. Keep its leading crest and lower dark-tan planes distinct from the lighter dust sheet farther away. No gun, ivory armour, lightning, muzzle flame, background blue, or independent upper debris. Return genuine RGBA transparency above and outside the dust.""",
    """The second near dust sheet and its loose tan chips ONLY. Keep the lighter upper diagonal crest and separated small angular grit at their original positions. Restore a complete continuous drifting sand sheet behind the dense front dust, filling the missing lower part under that overlap so separation never exposes a cut or a robot-shaped hole. Extend the sweep into the right and bottom gutter in the same wind direction. Use warm sand and ochre planes, retain open space between isolated chips, and leave clear transparent sky. No duplicate near dense sheet, armour, shoulder, gun, cyan electric arcs or gold gunfire. True RGBA outside the sand.""",
    """The cold blue-white electricity ONLY, one continuous three-dimensional overload loop at the exact original perspective and scale. Recover delicate forks and the far side of the loop behind the gun, keeping its visible front arc and primary top-right lightning branch exactly located. Join accidental gaps. The original top and right branches must keep extending beyond the old crop all the way into the new gutters, with natural taper and branching. It should be one complete transparent electricity asset even where the far side will later be covered by the gun. No barrel, dark backing, orange flame, dust, studio gradient, solid blue plate, or rendered checkerboard. True RGBA with thin white cores and restrained cyan edges.""",
    """The one A4B2 arm gun and the attached left mech body ONLY. Use the original integrated mechanical silhouette, blue side housing, ivory side plate, black concentric muzzle lip with a dark bore, shoulder and visible arm parts. Preserve the exact perspective, identity, original crop, dark clean line art and the directional left-side recoil smear. Restore actual metal in the strips previously covered by the blue lightning and the warm flash. Restore underlying connected mech structure where it passes behind lower sand. Extend the same cropped left armour and arm naturally into the left gutter at unchanged scale, with opaque solid mechanical interiors. Keep the bore dark, with no white hole through the mesh. No orange flash, gold trail, cyan electricity, dust or backdrop. Return genuine transparent RGBA outside this one complete mechanical assembly.""",
    """The attached warm muzzle flash and its thick continuous golden firing trajectory ONLY. Preserve the flash root and the actual axis to the upper-right; in the original window the broad gold-white shape comes straight from the central bore and continues beyond the right boundary. Complete its clipped right continuation throughout the new right gutter at the same width and angle. Preserve the original angular painterly gold/ivory core and its few related warm sparks. The position of the flash root is fixed. No metal cartridge, silhouette at the tip, separate glowing rods, blue electrical branches, machine ring, dust, or background. True RGBA around the flame and its natural edges.""",
    """The far studio backdrop ONLY. Paint a fully opaque seamless deep blue photographer's background, matching the quiet soft low-frequency navy-to-slate blue of the visible patches. The entire enlarged rectangle is continuous backdrop with no transparent gaps. Reconstruct all missing background behind the machine, electricity, gold shot, dust and particles. Continue the same gentle blue lighting through every outer gutter. No foreground, highlights shaped like gun or dust, horizon, scenery, wall seams, decoration, sparks, texture noise or vignette pattern. Full opaque RGB.""",
)


def main():
    for name in ("Anchors", "Inputs", "Prompts", "Raw", "Layers", "Masks"):
        (ROOT / name).mkdir(exist_ok=True)
    master = ROOT / "Master.png"
    assert not master.exists()
    expected = json.loads(REFERENCE.with_name("manifest.json").read_text(encoding="utf-8"))
    assert sha(REFERENCE) == expected["output"]["sha256"]
    shutil.copyfile(REFERENCE, master)
    rgb = np.asarray(Image.open(master).convert("RGB"))
    assert rgb.shape == (1536, 1024, 3)
    owners = owner_map(rgb)
    Image.fromarray(owners).save(ROOT / "Masks/Source_Owner.png")
    rows = []
    for index, ((slug, title), spec) in enumerate(zip(LAYER_NAMES, INSTRUCTIONS), 1):
        mask = (owners == index).astype(np.uint8) * 255
        anchor = Image.fromarray(np.dstack((np.where(mask[..., None] > 0, rgb, 0), mask)))
        anchor.save(ROOT / "Anchors" / f"{slug}.png")
        guide = Image.new("RGBA", SIZE)
        guide.paste(anchor, WINDOW[:2])
        target = ROOT / "Inputs" / f"{slug}_Edit.png"
        guide.save(target)
        prompt = COMMON + "\nLayer " + str(index) + " / " + title + "\n" + spec
        prompt_file = ROOT / "Prompts" / f"{slug}.txt"
        prompt_file.write_bytes(prompt.encode("utf-8"))
        rows.append({
            "index": index, "slug": slug, "title": title, "transparent_background": index != 6,
            "anchor_pixels": int(np.count_nonzero(mask)), "prompt": str(prompt_file),
            "prompt_sha256": sha(prompt_file), "referenced_image_paths": [str(target), str(master)],
            "input_sha256": [sha(target), sha(master)],
        })
    report = {
        "card_id": "09.01", "model_id": 4001, "date": "2026-11-10",
        "selection": "紧接 v7 后指定六层，以当前 v7 作为分层基准",
        "reference": str(REFERENCE), "reference_sha256": sha(master),
        "canvas": list(SIZE), "reference_window": list(WINDOW), "layers": rows,
    }
    (ROOT / "requests.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"canvas": list(SIZE), "reference_window": list(WINDOW), "anchors": [
        {"index": r["index"], "pixels": r["anchor_pixels"]} for r in rows
    ]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
