"""Prepare a pane completion input and assemble the six layers for card 10.01."""

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage as ndi


ROOT = Path(__file__).resolve().parent
SIZE = (1152, 1728)
WINDOW = (64, 96, 1088, 1632)
VIEW = np.s_[96:1632, 64:1088]
BACKGROUND_RGB = (0, 20, 19)
BACKGROUND = np.array(BACKGROUND_RGB, dtype=np.float32) / 255
WIDTH, HEIGHT = 1024, 1536
IMPACT = (466, 799)
TRAVEL = (40, 48)
MIN_SIMILARITY = 0.95
MASTER_HASH = "d1047be91ecfcb09ec2eee2da359384c6609c6c874718681d43df7683d1b8dda"
LAYERS = (
    ("01_NearShards", "近景碎玻璃", 1.0),
    ("02_Projectile", "弹体", 0.22),
    ("03_FarShards", "远景碎玻璃", 0.63),
    ("04_CrackedPane", "裂纹与破口光", 0.22),
    ("05_BinaryRain", "二进制数字雨", 0.08),
    ("06_Background", "墨绿背景", 0.0),
)

# Anchors refer to the outside of the black ink on the selected master.
NEAR_SHARDS = (
    [(74,1359),(144,1082),(330,963),(351,979),(416,1131),(378,1246),(173,1365),(86,1407)],
    [(604,435),(732,333),(841,435),(814,480),(680,560),(612,545)],
)
FAR_SHARDS = (
    [(65,539),(154,530),(182,534),(406,666),(395,692),(251,700),(77,585)],
    [(687,1020),(693,999),(724,975),(739,932),(762,927),(857,960),(862,976),(835,1059),(757,1119)],
    [(526,1067),(553,953),(608,963),(658,1049),(656,1080),(597,1142)],
    [(425,598),(437,555),(512,550),(516,563),(501,620),(464,630)],
    [(557,642),(590,601),(608,628),(608,638),(568,671),(560,668)],
    [(752,585),(763,566),(786,547),(815,564),(804,583),(780,596)],
    [(385,929),(397,891),(410,884),(424,879),(434,915),(418,950),(407,949)],
)
PROJECTILE = (
    (464,801),(465,784),(471,767),(482,751),(499,738),(554,709),
    (610,681),(673,655),(735,636),(800,626),(840,620),(879,621),
    (912,626),(944,639),(969,659),(992,684),(1008,709),(1014,727),
    (1008,751),(997,776),(980,801),(957,826),(932,845),(906,861),
    (873,872),(838,880),(789,885),(735,885),(689,882),(635,881),
    (585,881),(537,876),(508,869),(487,852),(474,835),(466,819),
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def master():
    path = ROOT / "Master.png"
    if sha(path) != MASTER_HASH:
        raise ValueError("Master.png does not match the selected reference")
    with Image.open(path) as image:
        if image.size != (WIDTH, HEIGHT):
            raise ValueError("Unexpected reference canvas size")
        base = Image.new("RGBA", image.size, (*BACKGROUND_RGB, 255))
        flattened = Image.alpha_composite(base, image.convert("RGBA"))
        return np.asarray(flattened.convert("RGB"), dtype=np.float32) / 255


def silhouette(vertices, brightness):
    """Fit visible closed outlines to the nearest ink trough."""
    mask = Image.new("L", (WIDTH, HEIGHT))
    ImageDraw.Draw(mask).polygon(vertices, fill=255)
    inside = np.asarray(mask) != 0
    ys, xs = np.where(inside)
    y0, y1 = max(0, ys.min() - 18), min(HEIGHT, ys.max() + 19)
    x0, x1 = max(0, xs.min() - 18), min(WIDTH, xs.max() + 19)
    crop = np.s_[y0:y1, x0:x1]
    body = inside[crop]
    yy, xx = np.indices(body.shape)
    cy, cx = np.argwhere(body).mean(axis=0)
    angle = np.arctan2(yy-cy, xx-cx)
    distance = np.hypot(yy-cy, xx-cx)
    count = 1024
    sectors = np.linspace(-np.pi, np.pi, count, endpoint=False)
    steps = np.arange(0, distance.max() + 1, 0.6)
    ray_y = cy + np.sin(sectors)[:, None] * steps
    ray_x = cx + np.cos(sectors)[:, None] * steps
    reaches = ndi.map_coordinates(body.astype(np.uint8), (ray_y, ray_x), order=0, mode="constant")
    outline = np.max(reaches * steps, axis=1)
    offsets = np.arange(-8, 9, dtype=np.float32)
    search = outline[:, None] + offsets
    ray_y = cy + np.sin(sectors)[:, None] * search
    ray_x = cx + np.cos(sectors)[:, None] * search
    greens = ndi.map_coordinates(brightness[crop], (ray_y, ray_x), order=1, mode="nearest")
    # Prefer the outermost dark ink beside the expected boundary.
    penalty = np.abs(offsets - 0.75) * 0.002
    candidate = np.argmin(greens + penalty, axis=1)
    minima = greens[np.arange(count), candidate]
    selected = np.where(minima < 0.058, offsets[candidate] + 0.85, 0.0)
    selected = ndi.median_filter(selected, size=9, mode="wrap")
    contour = np.interp(angle, sectors, outline + selected, period=2*np.pi)
    coverage = np.clip((contour - distance + 1.55) / 2.2, 0, 1)
    labels, _ = ndi.label(coverage >= 0.5, structure=np.ones((3,3), dtype=bool))
    components = np.bincount(labels[labels > 0])
    solid = ndi.binary_fill_holes(labels == components.argmax())
    fringe = ndi.binary_dilation(solid, structure=np.ones((3,3), dtype=bool))
    alpha = np.zeros(inside.shape, dtype=np.float32)
    alpha[crop] = np.where(solid, 1.0, np.minimum(coverage, 0.5)*fringe)
    return alpha


def plate_and_glyphs(rgb, blocked):
    r, g, b = np.moveaxis(rgb, 2, 0)
    # The reflection from the impact varies slowly compared with the type and cracks.
    quiet = ndi.grey_opening(ndi.grey_closing(rgb, size=(3, 3, 1)), size=(13, 13, 1))
    excess = g - quiet[..., 1]
    colored = (g - r > 6 / 255) & (r < 0.82 * g) & (g - b > 0.105 * (g - r))
    peaks = ndi.maximum_filter(excess, size=(15, 9))
    strokes = colored & (excess > np.maximum(5 / 255, 0.42 * peaks)) & ~blocked
    labels, _ = ndi.label(strokes, structure=np.ones((3,3), dtype=bool))
    boxes = np.zeros(strokes.shape, dtype=bool)
    glyphs = []
    for number, region in enumerate(ndi.find_objects(labels), 1):
        if region is None:
            continue
        rows, cols = region
        width = cols.stop - cols.start
        height = rows.stop - rows.start
        if width > 18 or not 3 <= height <= 29 or height < width * 0.88:
            continue
        component = labels[region] == number
        if component.sum() < 4:
            continue
        center = (cols.start + cols.stop) / 2
        if not any(abs(center - lane) < 7 for lane in CODE_COLUMNS):
            continue
        boxes[region] = True
        glyphs.append((region, component))
    # Keep pale crack strokes when they cross behind a glyph.
    crack = (excess > 12 / 255) & (g - b < 0.085 * (g - r))
    glow = ndi.maximum_filter(boxes, size=9) & ~crack & ~blocked
    # Sample from both sides of the type. An opening filter alone can retain
    # the shadow of a solid digit, which would move with the wrong layer.
    clear = ~(glow | crack | blocked | (excess > 11/255))
    xx = np.arange(rgb.shape[1])[None, :]
    left = np.maximum.accumulate(np.where(clear, xx, -1), axis=1)
    right = np.minimum.accumulate(np.where(clear, xx, rgb.shape[1])[:,::-1], axis=1)[:,::-1]
    y, x = np.where(glow)
    l, r = left[y,x], right[y,x]
    l = np.where(l >= 0, l, np.minimum(r, rgb.shape[1]-1))
    r = np.where(r < rgb.shape[1], r, l)
    blend = np.clip((x-l)/np.maximum(r-l, 1), 0, 1)[:,None]
    clean = rgb.copy()
    clean[glow] = rgb[y,l]*(1-blend) + rgb[y,r]*blend
    return clean, glyphs, glow


CODE_COLUMNS = (
    26,32,43,56,63,72,96,121,142,168,225,274,296,333,348,380,409,446,455,481,
    551,593,606,609,659,695,733,749,775,816,824,830,849,870,898,903,928,933,946,969,983,
)


def identify(rgb):
    brightness = rgb[..., 1]
    masks = {
        1: np.maximum.reduce([silhouette(shard, brightness) for shard in NEAR_SHARDS]),
        2: silhouette(PROJECTILE, brightness),
        3: np.maximum.reduce([silhouette(shard, brightness) for shard in FAR_SHARDS]),
    }
    occupied = np.maximum.reduce(list(masks.values()))
    pane, glyphs, rain = plate_and_glyphs(rgb, occupied > 0)
    pane, concealed_glyphs, remainder = plate_and_glyphs(pane, (occupied > 0) | rain)
    glyphs.extend(concealed_glyphs)
    rain |= remainder
    return masks, occupied, pane, glyphs, rain


def matte(target, underneath, opacity=0.0):
    """Solve straight RGBA for the observed colour over the known lower layer."""
    delta = target - underneath
    need = np.where(delta >= 0, delta / np.maximum(1-underneath, 1e-5),
                    -delta / np.maximum(underneath, 1e-5)).max(axis=2)
    alpha = np.clip(np.maximum(need, opacity), 0, 1)
    alpha = np.where(alpha > 1 / 255, alpha, 0)
    colour = np.clip(underneath + delta / np.maximum(alpha[..., None], 1e-5), 0, 1)
    colour[alpha == 0] = 0
    return np.dstack((colour, alpha))


def blank():
    return np.zeros((SIZE[1], SIZE[0], 4), dtype=np.float32)


def encode(array):
    return Image.fromarray(np.rint(np.clip(array, 0, 1) * 255).astype(np.uint8))


def prepare():
    source = master()
    masks, occupied, pane, glyphs, _ = identify(source)
    inputs = ROOT / "Inputs"
    inputs.mkdir(exist_ok=True)
    output = blank()
    base = np.broadcast_to(BACKGROUND, pane.shape)
    guide = matte(pane, base)
    guide[occupied > 0] = 0
    output[VIEW] = guide
    solid = Image.new("RGBA", SIZE, (*BACKGROUND_RGB, 255))
    Image.alpha_composite(solid, encode(output)).convert("RGB").save(
        inputs / "04_CrackedPane_Anchor.png"
    )
    for index, mask in masks.items():
        output = blank()
        output[VIEW] = np.dstack((source * (mask > 0)[...,None], mask))
        encode(output).save(inputs / f"{LAYERS[index-1][0]}_Anchor.png")
    print(json.dumps({
        "anchor_pixels": {str(index): int(np.count_nonzero(mask > 0.5)) for index, mask in masks.items()},
        "glyph_candidates": len(glyphs),
    }))


def fit_completion():
    path = ROOT / "Inputs/04_CrackedPane_Completed.png"
    with Image.open(path) as image:
        raw = image.convert("RGB")
    rgb = np.asarray(raw, dtype=np.float32) / 255
    bright = ndi.gaussian_filter(np.maximum(0, rgb[..., 0]-0.40), 9)
    x0, x1 = round(raw.width * 0.29), round(raw.width * 0.63)
    y0, y1 = round(raw.height * 0.30), round(raw.height * 0.68)
    patch = bright[y0:y1, x0:x1]
    y, x = np.unravel_index(patch.argmax(), patch.shape)
    raw_x, raw_y = x+x0, y+y0
    # Anchor the generated flare to the bright visible rim of the opening.
    target_x, target_y = 452 + WINDOW[0], 797 + WINDOW[1]
    scale = 1.01 * max(target_x/raw_x, (SIZE[0]-target_x)/(raw.width-raw_x),
                       target_y/raw_y, (SIZE[1]-target_y)/(raw.height-raw_y))
    offset = (target_x-raw_x*scale, target_y-raw_y*scale)
    fitted = raw.transform(
        SIZE, Image.Transform.AFFINE,
        (1/scale, 0, -offset[0]/scale, 0, 1/scale, -offset[1]/scale),
        Image.Resampling.BICUBIC, fillcolor=BACKGROUND_RGB,
    )
    return np.asarray(fitted, dtype=np.float32) / 255, {
        "file": path.relative_to(ROOT).as_posix(),
        "sha256": sha(path),
        "size": list(raw.size),
        "scale": scale,
        "offset": list(offset),
        "impact_source": [int(raw_x), int(raw_y)],
    }


def smoothstep(lo, hi, values):
    t = np.clip((values-lo)/(hi-lo), 0, 1)
    return t*t*(3-2*t)


def continue_inside(pane, painted, hidden):
    """Match the generated backing to the existing pane's nearby illumination."""
    quiet = ndi.grey_opening(pane, size=(13, 13, 1))
    nearest = ndi.distance_transform_edt(hidden, return_distances=False, return_indices=True)
    light = ndi.gaussian_filter(quiet[nearest[0], nearest[1]], (9, 9, 0))
    painted_light = ndi.grey_opening(painted, size=(13, 13, 1))
    result = np.clip(light + painted - painted_light, 0, 1)
    # The cut edge follows each existing spoke for a short distance before
    # handing off to the complete painted surface behind it.
    angles = np.linspace(-np.pi, np.pi, 2048, endpoint=False)
    radius = np.arange(1050, dtype=np.float32)
    ray_y = IMPACT[1] + np.sin(angles)[:, None] * radius
    ray_x = IMPACT[0] + np.cos(angles)[:, None] * radius
    seen = ndi.map_coordinates((~hidden).astype(np.uint8), (ray_y, ray_x), order=0) != 0
    ray_paint = np.stack([ndi.map_coordinates(channel, (ray_y,ray_x), order=1)
                         for channel in np.moveaxis(pane, 2, 0)], axis=2)
    for index, visible in enumerate(seen):
        if not visible.any():
            continue
        for channel in range(3):
            ray_paint[index,:,channel] = np.interp(
                radius, radius[visible], ray_paint[index,visible,channel]
            )
    yy, xx = np.where(hidden)
    distance = ndi.distance_transform_edt(hidden)[yy, xx]
    local = distance < 33
    y, x = yy[local], xx[local]
    phase = (np.arctan2(y-IMPACT[1], x-IMPACT[0]) + np.pi) * len(angles)/(2*np.pi)
    length = np.hypot(y-IMPACT[1], x-IMPACT[0])
    weight = (1-smoothstep(10, 33, distance[local]))[:, None]
    to_sample = np.concatenate((ray_paint, ray_paint[:1]), axis=0)
    radial = np.stack([ndi.map_coordinates(channel, (phase,length), order=1)
                       for channel in np.moveaxis(to_sample, 2, 0)], axis=1)
    result[y,x] = weight*radial + (1-weight)*result[y,x]
    return result


def join_margin(painted, edge):
    """Register the painted crack crossings along one edge of the native window."""
    width, depth = painted.shape[:2]
    profile = edge[:, 0]
    outer = painted[:, 0, 0]
    reference = np.flatnonzero((profile > 0.12) & (profile == ndi.maximum_filter1d(profile, 9)))
    candidates = np.flatnonzero((outer > 0.11) & (outer == ndi.maximum_filter1d(outer, 9)))
    pairs = []
    previous = -1
    for line in reference:
        viable = candidates[(candidates > previous) & (abs(candidates-line) < 75)]
        if len(viable):
            previous = int(viable[np.argmin(abs(viable-line))])
            pairs.append((int(line), previous))
    if pairs:
        original, generated = np.array(pairs).T
        cross = np.arange(width)
        shift = np.interp(cross, original, generated-original)
        reach = np.arange(depth)
        x = cross[:,None] + shift[:,None]*(1-smoothstep(8, 56, reach))
        y = np.broadcast_to(reach, x.shape)
        painted = np.stack([ndi.map_coordinates(channel, (x,y), order=1, mode="nearest")
                            for channel in np.moveaxis(painted, 2, 0)], axis=2)
    # Join colours at the boundary while retaining the generated outer paths.
    blend = smoothstep(0, 8, np.arange(1, depth+1))[None,:,None]
    return edge[:,None,:]*(1-blend) + painted*blend


def assemble_pane(source, painted, hidden):
    left, top, right, bottom = WINDOW
    core = source.copy()
    under = continue_inside(core, painted[VIEW], hidden)
    core[hidden] = under[hidden]
    full = painted.copy()
    full[VIEW] = core
    full[top:bottom,:left] = join_margin(painted[top:bottom,:left][:,::-1], core[:,0])[:,::-1]
    full[top:bottom,right:] = join_margin(painted[top:bottom,right:], core[:,-1])
    full[:top,left:right] = join_margin(painted[:top,left:right][::-1].transpose(1,0,2),
                                       core[0]).transpose(1,0,2)[::-1]
    full[bottom:,left:right] = join_margin(painted[bottom:,left:right].transpose(1,0,2),
                                          core[-1]).transpose(1,0,2)
    return matte(full, np.broadcast_to(BACKGROUND, full.shape))


def over(base, rgba):
    a = rgba[..., 3:4]
    return base*(1-a) + rgba[..., :3]*a


def add_binary_continuations(base, occupied, glyphs):
    """Lay out literal 0/1 type where the reference columns are occluded or cropped."""
    font_path = Path("C:/Windows/Fonts/consola.ttf")
    ink = Image.new("RGBA", SIZE)
    painter = ImageDraw.Draw(ink)
    blocked = occupied > 0.97
    columns = {}
    for (rows, cols), _ in glyphs:
        height = rows.stop - rows.start
        x = (cols.start + cols.stop) / 2
        if height >= 6:
            lane = min(CODE_COLUMNS, key=lambda column: abs(column-x))
            columns.setdefault(lane, []).append((rows.start, height))
    used = 0
    for x, entries in columns.items():
        entries.sort()
        starts = np.array([y for y,_ in entries])
        gaps = np.diff(starts)
        steps = gaps[(gaps >= 10) & (gaps <= 39)]
        if len(steps) < 2:
            continue
        step = round(float(np.median(steps)))
        height = int(np.median([height for _,height in entries]))
        font = ImageFont.truetype(str(font_path), round(height*1.32))
        positions = set()
        for start in starts:
            for y in range(int(start-step*7), int(start+step*8), step):
                if -WINDOW[1] <= y < HEIGHT + WINDOW[1] - step:
                    positions.add(y)
        accepted = []
        for y in sorted(positions):
            if np.any(abs(starts-y) < step*0.73) or (accepted and y-accepted[-1] < step*0.88):
                continue
            if 0 <= y <= HEIGHT-step:
                region = blocked[y:y+step, max(0,x-9):min(WIDTH,x+10)]
                if not region.all():
                    continue
            elif -step < y < HEIGHT:
                continue
            digit = "01"[(x*7 + y*3 + y//step) % 2]
            brightness = 65 if height < 10 else 112
            painter.text((x+WINDOW[0], y+WINDOW[1]), digit, font=font,
                         fill=(51, 255, 184, brightness), anchor="mt")
            accepted.append(y)
            used += 1
    return Image.alpha_composite(ink, encode(base)), used


def separate(source, occupied, pane, glyphs, support, painted):
    glass = assemble_pane(pane, painted, occupied > 0)
    glass_core = glass[VIEW]
    alpha = glass_core[..., 3:4]
    # The observed glyph colour is transmitted through the pane above it.
    background = np.broadcast_to(BACKGROUND, source.shape)
    desired = (source - alpha*glass_core[..., :3]) / np.maximum(1-alpha, 0.002)
    rain = blank()
    rain[VIEW] = matte(np.clip(desired, 0, 1), background)
    rain[VIEW][~support] = 0
    rain_image, count = add_binary_continuations(rain, occupied, glyphs)
    return glass, np.asarray(rain_image, dtype=np.float32)/255, count


def structural_similarity(source, stack):
    """Full-window RGB SSIM with the card skill's 11×11 Gaussian window."""
    def blur(image):
        return ndi.gaussian_filter(image, (1.5,1.5,0), radius=(5,5,0))
    a, b = blur(source), blur(stack)
    product = a*b
    square_a, square_b = a*a, b*b
    variance = blur(source*source) + blur(stack*stack) - square_a - square_b
    covariance = blur(source*stack) - product
    return float(np.mean(
        ((2*product + 0.01**2) * (2*covariance + 0.03**2)) /
        ((square_a + square_b + 0.01**2) * (variance + 0.03**2))
    ))


def compose(planes):
    stack = planes[6][VIEW][..., :3]
    for index in (5,4,3,2,1):
        stack = over(stack, planes[index][VIEW])
    return stack


def finish():
    source = master()
    masks, occupied, pane, glyphs, support = identify(source)
    painted, completion = fit_completion()
    glass, rain, digit_count = separate(source, occupied, pane, glyphs, support, painted)
    background = blank()
    background[..., :3] = BACKGROUND
    background[..., 3] = 1
    planes = {4: glass, 5: rain, 6: background}
    under = over(over(background[VIEW][..., :3], rain[VIEW]), glass[VIEW])
    for index in (3,2,1):
        shape = masks[index]
        objects = matte(source, under, shape*(1.0 if index == 2 else 0.80))
        objects[shape == 0] = 0
        solid = objects[..., 3] >= 0.70
        holes = ndi.binary_fill_holes(solid) & ~solid
        objects[holes, :3] = source[holes]
        objects[holes, 3] = 1
        full = blank()
        full[VIEW] = objects
        under = over(under, objects)
        planes[index] = full

    images = {index: encode(plane) for index,plane in planes.items()}
    rounded = {index: np.asarray(image,dtype=np.float32)/255 for index,image in images.items()}
    score = structural_similarity(source, compose(rounded))
    if score < MIN_SIMILARITY:
        raise ValueError(f"The six-layer stack has RGB SSIM {score:.6f}")

    path = ROOT / "Layers"
    path.mkdir(exist_ok=True)
    info = []
    for index, (slug, title, depth) in enumerate(LAYERS, 1):
        relative = f"Layers/{slug}.png"
        images[index].save(ROOT / relative)
        info.append({
            "file": relative, "name": title, "index": index, "depth": depth,
            "rgba_extrema": images[index].getextrema(), "sha256": sha(ROOT / relative),
        })
    manifest = {
        "card_id": "10.01", "name": "强化子弹", "revision": 1,
        "reference": {"file": "Master.png", "sha256": MASTER_HASH,
                      "attachment": "codex-clipboard-badda9c9-31a5-4aea-8eb9-7ae2c2c43f6f.png",
                      "rendered_over": "#001413", "size": [WIDTH, HEIGHT]},
        "canvas": list(SIZE), "window": list(WINDOW), "travel_px": list(TRAVEL),
        "draw_order": [6,5,4,3,2,1], "layers": info,
        "pane_completion": completion,
        "rain": {"visible_glyph_regions": len(glyphs), "new_binary_glyphs": digit_count},
        "reference_similarity": {"metric": "RGB SSIM", "value": score,
                                 "minimum": MIN_SIMILARITY, "gaussian_width": 11, "sigma": 1.5},
    }
    (ROOT / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2)+"\n", encoding="utf-8"
    )
    browser_data = {k: manifest[k] for k in ("revision","canvas","window","travel_px","draw_order","layers")}
    browser_data.update({"source": "Master.png", "background": "#001413",
                         "similarity_percent": round(score*100,2)})
    (ROOT / "card-data.js").write_text(
        "window.cardData = " + json.dumps(browser_data, ensure_ascii=False, indent=2) + ";\n",
        encoding="utf-8",
    )
    print(json.dumps({"similarity": score, "glyph_candidates": len(glyphs), "new_digits": digit_count,
                      "layer_sizes": [image.size for image in images.values()]}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("stage", choices=["prepare","finish"])
    args = parser.parse_args()
    if args.stage == "prepare":
        prepare()
    else:
        finish()
