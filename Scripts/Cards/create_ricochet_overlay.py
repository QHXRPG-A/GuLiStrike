"""Create a transparent, continuous tracer layer for the fixed card composition."""
import argparse
import json
import math
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, PngImagePlugin


def draw_taper(draw, start, end, width_start, width_end, color, scale):
    ax, ay = (v * scale for v in start)
    bx, by = (v * scale for v in end)
    length = math.hypot(bx - ax, by - ay)
    if length < 1:
        raise ValueError("Tracer segment endpoints must differ")
    nx, ny = -(by - ay) / length, (bx - ax) / length
    wa, wb = width_start * scale / 2, width_end * scale / 2
    draw.polygon([(ax + nx * wa, ay + ny * wa), (bx + nx * wb, by + ny * wb),
                  (bx - nx * wb, by - ny * wb), (ax - nx * wa, ay - ny * wa)], fill=color)


def create(config, output):
    width, height = config["canvas"]
    points = config["friendly_path"]
    assert len(points) == 4, "One muzzle and exactly three connected impact points are required"
    assert all(0 <= x < width and 0 <= y < height for x, y in points)
    scale = 4
    size = (width * scale, height * scale)
    result = Image.new("RGBA", size)
    # Soft halo, orange body and warm-white core share exactly the same vertices.
    core_ratio = float(config.get("friendly_core_ratio", .32))
    for factor, blur, color in [(3.0, 8.0, (255, 124, 36, 42)), (1.9, 2.4, (255, 155, 44, 105)),
                                 (1.0, .35, (255, 179, 71, 230)), (core_ratio, 0, (255, 246, 213, 255))]:
        layer = Image.new("RGBA", size)
        draw = ImageDraw.Draw(layer)
        widths = config.get("friendly_widths", [5.6, 4.4, 3.5, 2.6])
        for index in range(3):
            draw_taper(draw, points[index], points[index + 1], widths[index] * factor,
                       widths[index + 1] * factor, color, scale)
        for index, (x, y) in enumerate(points[1:-1], 1):
            radius = widths[index] * factor * scale / 2
            draw.ellipse((x * scale - radius, y * scale - radius, x * scale + radius, y * scale + radius), fill=color)
        if blur:
            layer = layer.filter(ImageFilter.GaussianBlur(blur * scale))
        result = Image.alpha_composite(result, layer)
    for streak in config.get("enemy_streaks", []):
        layer = Image.new("RGBA", size)
        draw = ImageDraw.Draw(layer)
        draw_taper(draw, streak[0], streak[1], 1.2, .35, (255, 171, 171, 220), scale)
        halo = layer.filter(ImageFilter.GaussianBlur(1.2 * scale))
        result = Image.alpha_composite(result, halo)
        result = Image.alpha_composite(result, layer)
    for impact in config.get("impacts", []):
        rng = random.Random(impact["seed"])
        cx, cy = impact["position"]
        radius = impact["radius"]
        layer = Image.new("RGBA", size)
        draw = ImageDraw.Draw(layer)
        # Small exposed dark mechanism, cracks, and painted armor chips.
        cavity = []
        for index in range(11):
            angle = index * math.tau / 11
            reach = radius * rng.uniform(.48, .88)
            cavity.append(((cx + math.cos(angle) * reach) * scale, (cy + math.sin(angle) * reach) * scale))
        draw.polygon(cavity, fill=(26, 24, 47, 220), outline=(3, 46, 48, 245))
        for index in range(7):
            angle = rng.uniform(0, math.tau)
            distance = radius * rng.uniform(1.05, 2.7)
            x, y = cx + math.cos(angle) * distance, cy + math.sin(angle) * distance
            chip = radius * rng.uniform(.14, .42)
            vertices = [((x + math.cos(angle + turn) * chip) * scale,
                         (y + math.sin(angle + turn) * chip) * scale) for turn in (0, 2.4, 4.2)]
            draw.polygon(vertices, fill=[(254, 228, 217, 245), (163, 64, 83, 245), (102, 34, 73, 245)][index % 3],
                         outline=(26, 24, 47, 250))
        result = Image.alpha_composite(result, layer)
        if impact.get("fire_cloud"):
            layer = Image.new("RGBA", size)
            draw = ImageDraw.Draw(layer)
            silhouette = []
            for index in range(24):
                angle = index * math.tau / 24
                reach = radius * rng.uniform(.75, 1.2)
                silhouette.append(((cx + math.cos(angle) * reach) * scale,
                                   (cy + math.sin(angle) * reach) * scale))
            draw.polygon(silhouette, fill=(238, 157, 88, 250))
            for factor, color in [(.78, (255, 178, 68, 245)), (.48, (255, 218, 133, 250))]:
                inner = [((cx * scale + (x - cx * scale) * factor),
                          (cy * scale + (y - cy * scale) * factor)) for x, y in silhouette]
                draw.polygon(inner, fill=color)
            result = Image.alpha_composite(result, layer)
        halo = Image.new("RGBA", size)
        hdraw = ImageDraw.Draw(halo)
        rr = radius * scale
        hdraw.ellipse((cx * scale - rr, cy * scale - rr, cx * scale + rr, cy * scale + rr), fill=(255, 151, 59, 120))
        result = Image.alpha_composite(result, halo.filter(ImageFilter.GaussianBlur(radius * .36 * scale)))
        layer = Image.new("RGBA", size)
        draw = ImageDraw.Draw(layer)
        for index in range(18):
            angle = rng.uniform(0, math.tau)
            distance = radius * rng.uniform(.9, 2.2)
            start = (cx + math.cos(angle) * radius * .22, cy + math.sin(angle) * radius * .22)
            end = (cx + math.cos(angle) * distance, cy + math.sin(angle) * distance)
            draw_taper(draw, start, end, rng.uniform(.55, 1.3), .18, (255, 220, 150, 225), scale)
        star = []
        for index in range(20):
            angle = index * math.tau / 20
            reach = radius * (rng.uniform(.55, .9) if index % 2 == 0 else .11)
            star.append(((cx + math.cos(angle) * reach) * scale, (cy + math.sin(angle) * reach) * scale))
        draw.polygon(star, fill=(255, 245, 210, 255))
        result = Image.alpha_composite(result, layer)
    result = result.resize((width, height), Image.Resampling.LANCZOS)
    metadata = PngImagePlugin.PngInfo()
    metadata.add_text("trajectory_config", json.dumps(config, ensure_ascii=False))
    output.parent.mkdir(parents=True, exist_ok=True)
    result.save(output, pnginfo=metadata)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    create(json.loads(args.config.read_text(encoding="utf-8")), args.output)
