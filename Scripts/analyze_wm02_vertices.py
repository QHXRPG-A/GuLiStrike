"""Render orthographic density views from WM02 vertices exported by Unreal MCP."""

import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont


PROJECT = Path("D:/UE5.7/test1")
SOURCE = PROJECT / "Saved/VibeUE/wm02_vertices.json"
OUTPUT = PROJECT / "Saved/VibeUE/wm02_orthographic.png"


def density_panel(points, horizontal, vertical, title, size=760, margin=35):
    x = points[:, horizontal]
    y = points[:, vertical]
    x_min, x_max = float(x.min()), float(x.max())
    y_min, y_max = float(y.min()), float(y.max())
    x_pad = max((x_max - x_min) * 0.04, 1.0)
    y_pad = max((y_max - y_min) * 0.04, 1.0)
    hist, _, _ = np.histogram2d(
        x,
        y,
        bins=(size - 2 * margin, size - 2 * margin),
        range=((x_min - x_pad, x_max + x_pad), (y_min - y_pad, y_max + y_pad)),
    )
    density = np.log1p(hist.T)
    density /= max(float(density.max()), 1.0)
    rgb = np.zeros((density.shape[0], density.shape[1], 3), dtype=np.uint8)
    rgb[..., 0] = (density * 90).astype(np.uint8)
    rgb[..., 1] = (density * 210).astype(np.uint8)
    rgb[..., 2] = (density * 255).astype(np.uint8)
    plot = Image.fromarray(rgb[::-1], mode="RGB")
    panel = Image.new("RGB", (size, size), (12, 16, 22))
    panel.paste(plot, (margin, margin))
    draw = ImageDraw.Draw(panel)
    draw.rectangle((margin, margin, size - margin, size - margin), outline=(90, 105, 120), width=2)
    draw.text((margin, 8), title, fill=(235, 240, 245), font=ImageFont.load_default())
    draw.text(
        (margin, size - 25),
        f"H: {x_min:.0f} .. {x_max:.0f} cm",
        fill=(160, 175, 190),
        font=ImageFont.load_default(),
    )
    draw.text(
        (size // 2, size - 25),
        f"V: {y_min:.0f} .. {y_max:.0f} cm",
        fill=(160, 175, 190),
        font=ImageFont.load_default(),
    )
    return panel


def main():
    data = json.loads(SOURCE.read_text(encoding="utf-8"))
    points = np.asarray(data["points"], dtype=np.float32)
    panels = [
        density_panel(points, 0, 1, "TOP: X horizontal, Y vertical"),
        density_panel(points, 1, 2, "FRONT: Y horizontal, Z vertical"),
        density_panel(points, 0, 2, "SIDE: X horizontal, Z vertical"),
    ]
    output = Image.new("RGB", (sum(p.width for p in panels), panels[0].height), (12, 16, 22))
    x = 0
    for panel in panels:
        output.paste(panel, (x, 0))
        x += panel.width
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    output.save(OUTPUT)
    print(OUTPUT)


if __name__ == "__main__":
    main()
