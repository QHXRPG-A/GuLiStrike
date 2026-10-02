"""Prepare the approved Gaea export for Unreal's south-to-north Landscape rows."""

import hashlib
import json
import shutil
from pathlib import Path

import numpy as np
from PIL import Image


NAME = "GuLiStrike_CommanderIsland_1800m_v1"
PROJECT = Path(__file__).resolve().parents[2]
GAEA = Path.home() / "Documents/Gaea/MCP"
SOURCE = GAEA / "Builds" / NAME / "final"
MASTER = GAEA / "Projects" / NAME
DEST = PROJECT / "ArtSource/Environment" / NAME / "UEImport"
LAYERS = [
    ("FlatGround", "flat_ground", [0.205, 0.355, 0.092]),
    ("Hills", "hills", [0.145, 0.285, 0.078]),
    ("Plateau", "plateau", [0.31, 0.29, 0.13]),
    ("Rock", "rock", [0.29, 0.31, 0.30]),
    ("Beach", "beach", [0.67, 0.53, 0.25]),
    ("Water", "water", [0.03, 0.12, 0.16]),
]


def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def main():
    original = DEST / "GaeaExports"
    converted = DEST / "LandscapeInputs"
    evidence = DEST / "Evidence"
    for folder in (original, converted, evidence):
        folder.mkdir(parents=True, exist_ok=True)
    source_files = ["height_4096.png", "height_4081.png", "height_4081.r16", "validation.json",
                    "delivery-checks.json", "topdown_preview.png", "routes_preview.png", "slope_preview.png"]
    source_files += [f"mask_{stem}.png" for _, stem, _ in LAYERS]
    hashes = {}
    for filename in source_files:
        shutil.copy2(SOURCE / filename, original / filename)
        hashes[filename] = sha(original / filename)
    for filename in ("layout.json", "REFERENCES.md", "README.md"):
        shutil.copy2(MASTER / filename, original / filename)
    layout = json.loads((MASTER / "layout.json").read_text(encoding="utf-8"))
    raw = np.asarray(Image.open(original / "height_4081.png"), dtype=np.uint16)
    assert raw.shape == (4081, 4081)
    assert np.array_equal(raw, np.fromfile(original / "height_4081.r16", dtype="<u2").reshape(raw.shape))
    height = np.ascontiguousarray(raw[::-1, :])
    height_png = converted / "height_4081_ue.png"
    Image.fromarray(height).save(height_png)
    height.astype("<u2").tofile(converted / "height_4081_ue.r16")
    assert np.array_equal(height, np.asarray(Image.open(height_png), dtype=np.uint16))

    n = 4081
    weights = np.empty((n, n, len(LAYERS)), dtype=np.float32)
    for channel, (name, stem, _) in enumerate(LAYERS):
        mask = np.asarray(Image.open(original / f"mask_{stem}.png"), dtype=np.float32) / 65535.0
        resized = Image.fromarray(mask).resize((n, n), Image.Resampling.BILINEAR)
        weights[:, :, channel] = np.asarray(resized)[::-1, :]
    packed = np.empty(weights.shape, dtype=np.uint8)
    for row in range(0, n, 128):
        w = weights[row:row + 128]
        total = w.sum(axis=2, keepdims=True)
        assert float(total.min()) > 0
        w /= total
        q = np.rint(w * 255).astype(np.int16)
        residual = 255 - q.sum(axis=2)
        largest = np.argmax(w, axis=2)
        rr, cc = np.indices(largest.shape)
        q[rr, cc, largest] += residual
        assert int(q.min()) >= 0 and int(q.max()) <= 255
        assert np.all(q.sum(axis=2) == 255)
        packed[row:row + 128] = q.astype(np.uint8)
    packed_path = converted / "weights_4081_x6.u8"
    packed.tofile(packed_path)
    assert packed_path.stat().st_size == 4081 * 4081 * 6
    layers = []
    for channel, (name, stem, color) in enumerate(LAYERS):
        path = converted / f"weight_{name}_4081.png"
        Image.fromarray(packed[:, :, channel]).save(path)
        layers.append({"name": name, "source_mask": f"GaeaExports/mask_{stem}.png", "weight_png":
                       f"LandscapeInputs/{path.name}", "channel": channel, "color_linear": color,
                       "weight_sha256": sha(path), "weight_min": int(packed[:, :, channel].min()),
                       "weight_max": int(packed[:, :, channel].max())})
    xy = 180000.0 / (n - 1)
    zscale = 14000.0 * 128.0 / 65535.0
    zoffset = 14000.0 * 32768.0 / 65535.0 - 2000.0
    checks = []
    def sample(label, x_m, y_m):
        x = (x_m * 100 + 90000) / xy
        y = (y_m * 100 + 90000) / xy
        ix, iy = int(round(x)), int(round(y))
        u16 = int(height[iy, ix])
        checks.append({"label": label, "vertex": [ix, iy],
                       "world_xy_cm": [-90000 + ix * xy, -90000 + iy * xy],
                       "expected_u16": u16, "expected_height_cm": u16 / 65535 * 14000 - 2000})
    for pad in layout["pads"]:
        sample(pad["name"] + "_Pad", *pad["center_m"])
    sample("Central_Plain", 0, 0)
    sample("East_Canyon", 480, -90)
    sample("Northwest_Ridge", -500, 400)
    sample("South_Bay", 500, -650)
    sample("Southwest_Seabed", -850, -850)
    peak_y, peak_x = np.unravel_index(int(height.argmax()), height.shape)
    sample("Highest_Peak", (-90000 + peak_x * xy) / 100, (-90000 + peak_y * xy) / 100)
    metadata = {
        "name": NAME, "source_project": str(MASTER / (NAME + ".terrain")),
        "source_project_sha256": sha(MASTER / (NAME + ".terrain")),
        "source_export_root": str(SOURCE), "source_sha256": hashes,
        "ue_map": "/Game/Maps/LVL_CommanderMassPrototype",
        "ue_asset_root": "/Game/GuLiStrike/Environment/CommanderIsland_1800m_v1",
        "height_png": "LandscapeInputs/height_4081_ue.png", "height_r16": "LandscapeInputs/height_4081_ue.r16",
        "packed_weights": "LandscapeInputs/weights_4081_x6.u8",
        "packed_weights_sha256": sha(packed_path), "height_sha256": sha(height_png),
        "resolution": [n, n], "height_bit_depth": 16,
        "extent_m": [1800, 1800], "height_range_m": [-20, 120], "sea_level_cm": 0,
        "actor_location_cm": [-90000, -90000, zoffset], "actor_scale": [xy, xy, zscale],
        "sections_per_component": 1, "quads_per_section": 255, "components_xy": [16, 16],
        "row_conversion": "Gaea top=+Y; UE first row=-Y. Height and all six weights are vertically flipped together.",
        "height_decode": "world_cm = (u16 - 32768) / 128 * actor_scale.z + actor_location.z",
        "weight_normalization": "Bilinear resample to 4081, normalize all six together, round to uint8 with residual in largest channel; each vertex sums to 255.",
        "layers": layers, "checkpoints": checks,
        "source_prompt": "自然非对称的战斗海岛，宽阔平原连接低丘、高台和峡谷，外围具有沙滩、浅湾及岩石陡岸；主岛连续，主要路线宽阔平缓，地形轮廓清楚。",
    }
    (DEST / "import_manifest.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"success": True, "destination": str(DEST), "resolution": [n, n], "layers": len(layers),
                      "weight_bytes": packed_path.stat().st_size, "height_range": [int(height.min()), int(height.max())],
                      "location": metadata["actor_location_cm"], "scale": metadata["actor_scale"]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
