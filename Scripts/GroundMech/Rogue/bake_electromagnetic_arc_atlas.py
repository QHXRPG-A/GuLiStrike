"""Bake the project's 16-frame atlas from a PNG export of the vendor T_Lightning_B."""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image


SOURCE_DIR = Path(__file__).resolve().parents[3] / "ArtSource/FX/GroundMech/Electromagnetic"
OUTPUT = SOURCE_DIR / "T_GroundElectromagneticArc16.png"


def red_centerline(frame):
    # The bolt material uses the red channel as its opacity mask.
    weights = frame[:, :, 0] ** 2
    mass = weights.sum(axis=1)
    rows = np.flatnonzero(mass > 1e-8)
    if not len(rows):
        raise ValueError("Lightning frame has no visible red-channel core")
    centers = (weights[rows] @ np.arange(frame.shape[1])) / mass[rows]
    return np.interp(np.arange(frame.shape[0]), rows, centers)


def align_rows(frame, source_center, target_center):
    """Move the core to target_center without sampling outside this frame."""
    height, width = frame.shape[:2]
    x = np.arange(width)[None, :]
    left = source_center[:, None]
    mid = np.clip(target_center[:, None], 0.001, width - 1.001)
    source_x = np.where(x <= mid, x * left / mid,
                        left + (x - mid) * (width - 1 - left) / (width - 1 - mid))
    source_x = np.clip(source_x, 0, width - 1)
    lo = source_x.astype(int)
    fraction = (source_x - lo)[:, :, None]
    y = np.arange(height)[:, None]
    return frame[y, lo] * (1 - fraction) + frame[y, np.minimum(lo + 1, width - 1)] * fraction


def anchor_endpoints(frame):
    """Extend a bright strand to the middle of each end of the Niagara sprite."""
    height, width = frame.shape[:2]
    red = frame[:, :, 0].astype(float)
    # UV 0.5 lies between two texels. Find a strand that survives filtering on two consecutive rows.
    pairs = (red[:, :-1] + red[:, 1:]) * 0.5
    shared = np.minimum(pairs[:-1], pairs[1:])
    trunk = 1 + np.flatnonzero(shared[1:-1].max(axis=1) >= max(88, pairs.max() * 0.45))
    if not len(trunk):
        raise ValueError("Lightning frame has no continuous bright core to connect to the endpoints")

    output = frame.copy()
    x = np.arange(width)
    middle = (width - 1) * 0.5
    guide = red_centerline(frame.astype(float))
    guide = np.convolve(np.pad(guide, 2, mode="edge"), [0.1, 0.2, 0.4, 0.2, 0.1], mode="valid")
    for edge, row, step in ((0, trunk[0], 1), (height - 1, trunk[-1], -1)):
        join = row if step == 1 else row + 1
        strand = shared[row].argmax()
        strand_x = strand + 0.5
        tip_gain = max(1.0, 120.0 / pairs[join, strand])
        # Carry the strand's own color and glow into the margin. Leave nearby branches alone.
        radius = max(3.0, width / 12.0)
        falloff = np.clip((radius - np.abs(x - strand_x)) / 2.0, 0.0, 1.0)
        profile = frame[join, :, :3] * falloff[:, None]
        for y in range(edge, join, step):
            t = (y - edge) / (join - edge)
            blend = t * t * (3.0 - 2.0 * t)
            center = middle + (strand_x - middle) * blend
            # Follow the surviving dim strand between the two pinned ends of the extension.
            curve = guide[y] - ((1.0 - t) * guide[edge] + t * guide[join])
            center += 2.0 * t * (1.0 - t) * curve
            sample_x = x - center + strand_x
            gain = (1.0 - blend) * tip_gain + blend
            for channel in range(3):
                extension = np.interp(sample_x, x, profile[:, channel], left=0.0, right=0.0)
                output[y, :, channel] = np.maximum(output[y, :, channel], np.rint(extension * gain).clip(0, 255))
    return output


def bake(source, output):
    with Image.open(source) as image:
        pixels = np.asarray(image.convert("RGBA"))
    height, width = pixels.shape[:2]
    if width % 4 or height % 2:
        raise ValueError("Expected a 4-by-2 source atlas")
    cell_w, cell_h = width // 4, height // 2
    atlas = Image.new("RGBA", (width, 4 * cell_h))
    frames = [pixels[y:y + cell_h, x:x + cell_w] for y in range(0, height, cell_h)
              for x in range(0, width, cell_w)]
    def put_frame(index, pixels):
        tile = Image.fromarray(anchor_endpoints(pixels))
        atlas.paste(tile, (index % 4 * cell_w, index // 4 * cell_h))

    # Interpolate the original keys before attaching the finished frames to the segment endpoints.
    keys = [round(i * 15 / 7) for i in range(8)]
    for index, frame in zip(keys, frames):
        put_frame(index, frame)
    for index, (start, end) in enumerate(zip(keys, keys[1:])):
        pair = np.stack(frames[index:index + 2]).astype(np.float64) / 255
        rgb = pair[..., :3]
        rgb[:] = np.where(rgb <= .04045, rgb / 12.92, ((rgb + .055) / 1.055) ** 2.4)
        a, b = pair
        a_center, b_center = red_centerline(a), red_centerline(b)
        for frame_no in range(start + 1, end):
            t = (frame_no - start) / (end - start)
            center = (1 - t) * a_center + t * b_center
            mix = (1 - t) * align_rows(a, a_center, center) + t * align_rows(b, b_center, center)
            rgb = mix[..., :3]
            rgb[:] = np.where(rgb <= .0031308, rgb * 12.92, 1.055 * rgb ** (1 / 2.4) - .055)
            put_frame(frame_no, np.rint(mix.clip(0, 1) * 255).astype(np.uint8))
    output.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="PNG exported by Unreal's TextureExporterPNG")
    bake(parser.parse_args().source, OUTPUT)
