"""Compare saved UE Landscape readbacks with the prepared source files."""

import json
from pathlib import Path

import numpy as np
from PIL import Image


def main():
    root = Path(__file__).resolve().parents[1] / "ArtSource/Environment/GuLiStrike_CombatIsland_2300m_v1/UEImport"
    manifest = json.loads((root / "import_manifest.json").read_text(encoding="utf-8"))
    evidence = root / "Evidence"
    original = np.asarray(Image.open(root / "GaeaExports/height_4081.png"), dtype=np.uint16)
    prepared = np.asarray(Image.open(root / manifest["height_png"]), dtype=np.uint16)
    exported = np.asarray(Image.open(evidence / "height_ue_readback.png"), dtype=np.uint16)
    height_equal = bool(np.array_equal(prepared, exported))
    orientation_equal = bool(np.array_equal(original[::-1], exported))
    assert exported.shape == (4081, 4081) and height_equal and orientation_equal
    assert int(exported.min()) == 0 and int(exported.max()) == 65535
    layer_results = []
    total = np.zeros(exported.shape, dtype=np.uint16)
    for layer in manifest["layers"]:
        src = np.asarray(Image.open(root / layer["weight_png"]))
        actual = np.asarray(Image.open(evidence / ("weight_ue_readback_" + layer["name"] + ".png")))
        same = bool(np.array_equal(src, actual))
        assert actual.dtype == np.uint8 and actual.shape == exported.shape and same
        total += actual.astype(np.uint16)
        layer_results.append({"layer": layer["name"], "pixel_equal": same, "bit_depth": 8})
    assert np.all(total == 255)
    raw = np.fromfile(root / manifest["height_r16"], dtype="<u2").reshape(exported.shape)
    assert np.array_equal(raw, exported)
    heights = exported.astype(np.float32) / 65535.0 * 140.0 - 20.0
    spacing = 2300.0 / 4080
    dy, dx = np.gradient(heights, spacing)
    slope = np.rad2deg(np.arctan(np.hypot(dx, dy)))
    land = heights > 0
    gentle_percent = float((slope[land] <= 15).mean() * 100)
    assert gentle_percent >= 60
    state = json.loads((evidence / "verify.json").read_text(encoding="utf-8"))["result"]
    reopened = json.loads((evidence / "reopen.json").read_text(encoding="utf-8"))["result"]
    collisions = json.loads((evidence / "collision_checks.json").read_text(encoding="utf-8"))["result"]
    assert state["success"] and reopened["success"] and collisions["success"]
    assert state["target_layers"] == [l["name"] for l in manifest["layers"]]
    assert reopened["collision_components"] == 256
    max_height_error_cm = max(abs(s["height_vertex_cm"] - s["expected_height_cm"]) for s in state["samples"])
    assert max_height_error_cm < 0.01
    result = {"success": True, "ue_map": manifest["ue_map"], "resolution": [4081, 4081],
        "height_bit_depth": 16, "height_pixel_equal": height_equal, "orientation_equal_to_flipped_gaea": orientation_equal,
        "height_range_m": [float(heights.min()), float(heights.max())], "extent_m": [2300, 2300],
        "max_height_sample_error_cm": max_height_error_cm, "gentle_land_at_most_15_degrees_percent": gentle_percent,
        "layers": layer_results, "all_weight_sums": 255, "landscape_components": 256, "collision_components": 256,
        "collision_sample_count": collisions["count"], "collision_max_error_cm": collisions["max_error_cm"],
        "sea_level_cm": 0, "sea_has_collision": False, "saved_map_reopened": True,
        "verification_scope": "Saved editor assets, native readback, geometry and editor collision. No PIE/runtime/nav validation."}
    (evidence / "delivery_validation.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
