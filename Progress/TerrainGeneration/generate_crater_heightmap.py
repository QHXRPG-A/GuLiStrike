"""Generate the LVL_Main meteor-crater + central-plateau Landscape heightmap.

The output is an Unreal-compatible 16-bit PNG at the exact 8161 x 8161
resolution of the existing Landscape. Coordinates are centred on world origin;
the Landscape uses 100 cm XY scale, so one pixel is one metre.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image


RESOLUTION = 8161
LANDSCAPE_Z_CM = 100.0
LANDSCAPE_Z_SCALE = 150.0
CM_PER_HEIGHT_UNIT = LANDSCAPE_Z_SCALE / 128.0


def smoothstep(edge0: float, edge1: float, value: np.ndarray) -> np.ndarray:
    t = np.clip((value - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def generate_heightmap(output_path: Path, preview_path: Path | None) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    result = np.empty((RESOLUTION, RESOLUTION), dtype=np.uint16)

    # The 8.16 km Landscape is centred at (0, 0), at one metre per vertex.
    axis_m = np.arange(RESOLUTION, dtype=np.float32) - (RESOLUTION - 1) * 0.5
    x = axis_m[None, :]
    chunk_rows = 256

    for row0 in range(0, RESOLUTION, chunk_rows):
        row1 = min(row0 + chunk_rows, RESOLUTION)
        y = axis_m[row0:row1, None]

        # Elliptical impact basin. The long/short radii keep the boundary organic
        # while preserving a readable, near-symmetric competitive layout.
        rho = np.sqrt((x / 3600.0) ** 2 + (y / 3300.0) ** 2)
        theta = np.arctan2(y / 3300.0, x / 3600.0)

        # Broad, mostly level playable floor: approximately -75 m at the centre,
        # gently lifting toward the inner crater wall.
        basin = -75.0 + 15.0 * np.minimum(rho / 0.76, 1.0) ** 2
        basin += 2.5 * np.cos(theta * 2.0) * np.minimum(rho, 0.75) ** 2

        # Physical world boundary. A modest talus rise leads into a steep final
        # wall, then a raised rim/outer highland. The steep band is intentionally
        # above ordinary ground-unit traversal limits.
        talus = 28.0 * smoothstep(0.74, 0.86, rho)
        wall = 230.0 * smoothstep(0.86, 0.92, rho)
        crest = 42.0 * np.exp(-((rho - 0.945) / 0.045) ** 2)
        terrain_m = basin + talus + wall + crest

        # Low-frequency breakup is applied only to the inaccessible outer rim;
        # the basin itself stays clean enough for Mass/vehicle traversal.
        outer_mask = smoothstep(0.93, 1.03, rho)
        outer_breakup = (
            10.0 * np.sin(x / 175.0)
            + 8.0 * np.sin(y / 210.0)
            + 5.0 * np.sin((x + y) / 130.0)
        )
        terrain_m += outer_mask * outer_breakup

        # Central elevated plateau. Most of the perimeter is a strong escarpment;
        # four cardinal sectors extend the blend to form broad playable ramps.
        plateau_rho = np.sqrt((x / 780.0) ** 2 + (y / 650.0) ** 2)
        cardinal = np.abs(np.cos(theta * 2.0))
        ramp_weight = smoothstep(0.78, 0.97, cardinal)
        plateau_outer = 1.10 + 0.48 * ramp_weight
        plateau_factor = 1.0 - smoothstep(0.80, plateau_outer, plateau_rho)
        plateau_top_m = 65.0
        terrain_m += plateau_factor * (plateau_top_m - terrain_m)

        # Clamp well inside the existing Z-scale range before converting metres
        # to Unreal's uint16 Landscape encoding.
        terrain_cm = np.clip(terrain_m, -300.0, 300.0) * 100.0
        encoded = np.rint(
            32768.0 + (terrain_cm - LANDSCAPE_Z_CM) / CM_PER_HEIGHT_UNIT
        )
        result[row0:row1] = np.clip(encoded, 0.0, 65535.0).astype(np.uint16)

    Image.fromarray(result).save(output_path)

    if preview_path is not None:
        preview_path.parent.mkdir(parents=True, exist_ok=True)
        step = 4
        sample = result[::step, ::step].astype(np.float32)
        height_m = ((sample - 32768.0) * CM_PER_HEIGHT_UNIT + LANDSCAPE_Z_CM) / 100.0
        gy, gx = np.gradient(height_m)
        shade = np.clip(0.72 - gx * 0.055 + gy * 0.035, 0.35, 1.0)

        # Graybox palette: basin blue-gray, plateau warm gray, rim dark basalt.
        low = np.array([62.0, 75.0, 84.0], dtype=np.float32)
        mid = np.array([132.0, 124.0, 108.0], dtype=np.float32)
        high = np.array([73.0, 68.0, 65.0], dtype=np.float32)
        t_mid = np.clip((height_m + 80.0) / 150.0, 0.0, 1.0)[..., None]
        t_high = np.clip((height_m - 70.0) / 170.0, 0.0, 1.0)[..., None]
        rgb = low * (1.0 - t_mid) + mid * t_mid
        rgb = rgb * (1.0 - t_high) + high * t_high
        rgb *= shade[..., None]
        Image.fromarray(np.clip(rgb, 0.0, 255.0).astype(np.uint8)).save(
            preview_path
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--preview", type=Path)
    args = parser.parse_args()
    generate_heightmap(args.output, args.preview)


if __name__ == "__main__":
    main()
