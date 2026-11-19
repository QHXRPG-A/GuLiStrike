"""Register complete painted surfaces to the approved, visible card art."""

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as ndi


BACKGROUND = np.array([8, 64, 102], dtype=np.float32) / 255
ARC_ROUTES = (
    [(355,423),(389,410),(427,394),(468,403),(516,414),(558,435),(602,462),(640,475),
     (684,497),(710,522),(726,558),(743,596)],
    [(360,379),(328,393),(302,426),(298,470),(283,501),(242,521),(207,550),
     (178,589),(156,636),(112,681)],
    [(349,556),(363,592),(394,626),(407,658),(431,688),(436,722),(425,752),
     (406,773),(391,807),(350,837),(340,873),(357,915),(359,943),(377,968),
     (416,981),(469,1011),(523,1008),(573,999),(612,972),(657,928),(699,916),
     (736,878),(759,835),(777,799),(777,759)],
    [(342,586),(312,602),(280,625),(242,643),(213,651),(180,651),(145,655),(112,675)],
    [(550,468),(516,492),(486,518),(456,537),(444,570),(443,613),(431,666),(435,703)],
    [(409,770),(436,732),(471,705),(509,695),(538,654)],
    [(343,875),(314,904),(277,918),(240,931),(213,945)],
    [(290,942),(319,953),(345,965),(376,965)],
    [(280,1058),(333,1035),(367,1017),(409,1009),(446,1016)],
    [(413,1059),(452,1068),(485,1066),(537,1023)],
    [(547,930),(586,937),(626,927),(667,918),(703,910),(739,893)],
    [(349,556),(375,528),(416,517),(458,492),(507,481),(550,468)],
)
WRAP_OUTLINE = (
    (550,468),(602,462),(640,475),(684,497),(710,522),(726,558),(743,596),
    (755,634),(766,676),(774,721),(777,759),(777,799),(759,835),(736,878),
    (699,916),(657,928),(612,972),(573,999),(523,1008),(469,1011),
    (416,981),(377,968),(359,943),(357,915),(340,873),(350,837),(391,807),
    (406,773),(425,752),(436,722),(431,688),(407,658),(394,626),(363,592),
    (349,556),(375,528),(416,517),(458,492),(507,481),
)


def largest_region(mask):
    labels, _ = ndi.label(mask, structure=np.ones((3, 3), dtype=bool))
    if not mask.any():
        return mask
    return labels == np.bincount(labels[mask]).argmax()


def smooth_step(low, high, value):
    unit = np.clip((value - low) / (high - low), 0, 1)
    return unit * unit * (3 - 2 * unit)


def route_distance(size, routes):
    image = Image.new("L", size)
    drawing = ImageDraw.Draw(image)
    for route in routes:
        drawing.line(route, fill=255, width=1)
    return ndi.distance_transform_edt(np.asarray(image) == 0)


def estimate_sky(source):
    # The old studio background is low-frequency. Use only its quiet blue samples.
    candidate = np.max(np.abs(source - BACKGROUND), axis=2) < 19 / 255
    weight = ndi.gaussian_filter(candidate.astype(np.float32), 24)
    field = np.empty_like(source)
    for channel in range(3):
        average = ndi.gaussian_filter(candidate * source[..., channel], 24)
        field[..., channel] = np.where(
            weight > 0.015, average / np.maximum(weight, 0.015), BACKGROUND[channel]
        )
    return field


def reference_masks(source, mech, sky):
    height, width = source.shape[:2]
    y, x = np.ogrid[:height, :width]
    r, g, b = np.moveaxis(source * 255, 2, 0)

    # The root bounds follow the flame, excluding the thin concentric metal rims.
    flight_x = [620, 630, 642, 661, 689, 722, 750, 790, 835, 890, 950, 1023]
    upper = np.interp(np.arange(width), flight_x, [714,690,673,660,636,599,535,458,421,397,373,347])
    lower = np.interp(np.arange(width), flight_x, [722,741,755,770,770,764,767,767,760,757,750,743])
    main_flight = (x >= 628) & (y >= upper) & (y <= lower)
    warm = (r >= 107) & (r - g >= 5) & (g - b >= 9)
    warm &= (x >= 684) | ((g >= 170) & (b >= 55))
    main = largest_region(ndi.binary_opening(main_flight & warm & (r >= 153), iterations=1))
    fire = main_flight & warm & (ndi.distance_transform_edt(~main) <= 4)
    sparks = (x >= 746) & (y >= 381) & (y <= 799) & ~mech & warm & (r >= 137)
    fire |= sparks
    fire = ndi.binary_fill_holes(fire)

    dust_floor = np.interp(
        np.arange(width), [0,100,210,300,400,540,680,740,1023],
        [1340,1300,1260,1190,1140,1060,1045,990,977],
    )
    sand = (y >= dust_floor) & (r >= 51) & (r - g >= 3) & (g - b >= 5)
    dust = ndi.binary_fill_holes(sand)
    divide = np.interp(
        np.arange(width), [0,160,314,501,705,885,1023],
        [1474,1431,1378,1310,1237,1172,1148],
    )
    near = largest_region(dust & (y >= divide))
    far = dust & ~near

    pale = (r >= 174) & (g >= 199) & (b >= 223) & (g >= r - 2) & (b >= g - 4)
    cyan = (g >= 90) & (b >= 127) & (g >= r + 6) & (b >= g - 4)
    distance = route_distance((width, height), ARC_ROUTES)
    cold_gain = np.max(source - sky, axis=2)
    # Away from the metal, brightness cleanly separates the small filaments.
    outside_metal = ndi.distance_transform_edt(~mech) > 8
    exterior = outside_metal & (y < 1120) & (cold_gain > 0.028) & (g >= r - 2) & (b >= g - 4)
    core = pale & ((distance < 13) | outside_metal)
    core &= ~fire & ~dust & (y < 1120)
    arc = core | (cyan & (ndi.distance_transform_edt(~core) < 3.1))
    arc |= exterior & (cyan | pale)
    arc &= ~fire & ~dust

    # The complete source establishes the contour behind all front effects.
    nearby = ndi.distance_transform_edt(~mech) <= 45
    foreground = np.max(np.abs(source - sky), axis=2) > 11 / 255
    covering = fire | arc | dust
    armour = nearby & foreground & ~covering
    armour |= covering & mech
    armour = largest_region(ndi.binary_fill_holes(ndi.binary_closing(armour, iterations=2)))
    return near, far, arc, armour, fire


def put_colour(plane, region, rgb, alpha=1):
    plane[region, :3] = rgb[region]
    plane[region, 3] = alpha


def over(background, plane):
    alpha = plane[..., 3:4]
    return plane[..., :3] * alpha + background * (1 - alpha)


def derive_effect(source, underneath, mask, colour_floor):
    """Unmix transparent edges from the surface on which they were painted."""
    result = np.zeros((*mask.shape, 4), dtype=np.float32)
    diff = source[mask] - underneath[mask]
    base = underneath[mask]
    needed = np.where(diff >= 0, diff / np.maximum(1 - base, 1e-5),
                      -diff / np.maximum(base, 1e-5)).max(axis=1)
    strength = np.clip(np.maximum(needed + 0.006, colour_floor), 1e-3, 1)
    result[mask, :3] = np.clip(base + diff / strength[:, None], 0, 1)
    result[mask, 3] = strength
    return result


def fix_material(base, source, known, shape):
    """Match the clean underpainting to neighbouring visible armour values."""
    result = np.zeros_like(base)
    delta = (source - base[..., :3]) * known[..., None]
    support = ndi.gaussian_filter(known.astype(np.float32), 9)
    colour = ndi.gaussian_filter(delta, (9, 9, 0)) / np.maximum(support[..., None], 0.12)
    result[..., :3] = np.clip(base[..., :3] + np.clip(colour, -0.20, 0.20), 0, 1)
    result[..., 3] = shape
    put_colour(result, known, source)
    return result


def weld_left(fitted, base, known):
    # Keep one continuous mechanical surface across the old left frame edge.
    blend = smooth_step(0, 37, np.arange(fitted.shape[1], dtype=np.float32))[None, :, None]
    fitted[:] = base * (1 - blend) + fitted * blend
    fitted[..., 3][known & (blend[..., 0] >= 1)] = 1


def recover_armour(fitted, base, source, covers):
    solid = fitted[..., 3] >= 0.5
    holes = ndi.binary_fill_holes(solid) & ~solid
    put_colour(fitted, holes & ~covers, source)
    put_colour(fitted, holes & covers, base[..., :3])


def radii_of(mask, centre, direction):
    rays = np.arange(0, 740, 0.75)
    points = centre[:, None, None] + direction[:, :, None] * rays
    inside = ndi.map_coordinates(mask.astype(np.uint8), points, order=0) > 0
    return np.max(np.where(inside, rays, 0), axis=1)


def sample_plane(plane, points):
    return np.stack([ndi.map_coordinates(channel, points, order=1)
                     for channel in np.moveaxis(plane, -1, 0)], axis=-1)


def align_completed_ring(model, source, visible_arc):
    """Fit the painted closed ring's centreline to the original firing arc."""
    height, width = visible_arc.shape
    inside = Image.new("L", (width, height))
    ImageDraw.Draw(inside).polygon(WRAP_OUTLINE, fill=255)
    model_support = model[..., 3] > 0.38
    model_inside = largest_region(ndi.binary_fill_holes(model_support) & ~model_support)
    if np.count_nonzero(model_inside) < 80000:
        raise ValueError("Missing closed loop in the painted completion")
    model_centre = np.array(ndi.center_of_mass(model_inside))
    centre = np.array([739, 545])
    angles = np.linspace(-np.pi, np.pi, 1440, endpoint=False)
    rays = np.array([np.sin(angles), np.cos(angles)])
    model_radius = radii_of(model_inside, model_centre, rays)
    radius = radii_of(np.asarray(inside) > 0, centre, rays)

    offsets = np.arange(-13, 14)
    samples = centre[:, None, None] + rays[:, :, None] * (radius[:, None] + offsets)
    original_core = visible_arc & (source[..., :3].min(axis=2) > 0.65)
    brightness = ndi.map_coordinates((source.mean(axis=2) * original_core), samples, order=1)
    peak = np.argmax(brightness - np.abs(offsets) * 0.014, axis=1)
    accepted = brightness[np.arange(angles.size), peak] > 0.66
    weight = ndi.gaussian_filter1d(accepted.astype(np.float32), 2.4, mode="wrap")
    delta = ndi.gaussian_filter1d(offsets[peak] * accepted, 2.4, mode="wrap")
    radius += delta / np.maximum(weight, 0.30)

    model_offsets = np.arange(-1, 14)
    model_samples = model_centre[:, None, None] + rays[:, :, None] * (
        model_radius[:, None] + model_offsets
    )
    strength = ndi.map_coordinates(model[..., 3], model_samples, order=1)
    ridge = model_offsets[np.argmax(strength, axis=1)]
    model_radius += ndi.gaussian_filter1d(ridge.astype(np.float32), 1.5, mode="wrap")
    yy, xx = np.ogrid[:height, :width]
    y, x = yy - centre[0], xx - centre[1]
    distance = np.hypot(y, x)
    phase = np.arctan2(y, x)
    contour = np.interp(phase, angles, radius, period=2 * np.pi)
    source_contour = np.interp(phase, angles, model_radius, period=2 * np.pi)
    shift = source_contour - contour
    scale = (distance + shift) / np.maximum(distance, 1)
    result = sample_plane(model, np.array([model_centre[0] + scale * y, model_centre[1] + scale * x]))
    result[..., 3] *= 1 - smooth_step(6, 12, np.abs(distance - contour))
    return result


def spans(alpha):
    changes = np.diff(np.pad((alpha >= 0.18).astype(np.int8), (1, 1)))
    return [(int(start), int(stop - 1)) for start, stop in
            zip(np.flatnonzero(changes == 1), np.flatnonzero(changes == -1)) if stop - start >= 2]


def registered_margin(painted, edge):
    """Match the painted continuation's crossings to the actual source edge."""
    foreground = spans(edge[:, 3])
    model = spans(painted[:, 0, 3])
    if not foreground or not model:
        return painted
    pairs = []
    available = list(model)
    for start, stop in foreground:
        if not available:
            break
        middle = (start + stop) / 2
        def mismatch(span):
            return abs((span[0] + span[1]) / 2 - middle) + 0.4 * abs((span[1] - span[0]) - (stop - start))
        match = min(available, key=mismatch)
        if abs((match[0] + match[1]) / 2 - middle) > 92:
            continue
        pairs.extend(((start, match[0]), (stop, match[1])))
        available = [span for span in available if span[0] > match[1]]
    if not pairs:
        return painted
    target, origin = np.array(pairs).T
    cross = np.arange(painted.shape[0], dtype=np.float32)
    depth = np.arange(painted.shape[1], dtype=np.float32)[None, :]
    correction = np.interp(cross, target, origin - target)
    factor = 1 - smooth_step(5, 50, depth)
    coordinates = cross[:, None] + correction[:, None] * factor
    result = sample_plane(painted, np.array([coordinates, np.broadcast_to(depth, coordinates.shape)]))
    common = (result[:, 0, 3] >= 0.15) & (edge[:, 3] >= 0.15)
    colour = (edge[:, :3] - result[:, 0, :3]) * common[:, None]
    result[..., :3] = np.clip(result[..., :3] + colour[:, None] * (1 - smooth_step(0, 24, depth))[..., None], 0, 1)
    return result


def lift_into_canvas(base, core, window):
    x1, y1, x2, y2 = window
    result = base.copy()
    result[y1:y2, x1:x2] = core
    return result


def fitted_extensions(complete, core, window, edges):
    result = lift_into_canvas(complete, core, window)
    x1, y1, x2, y2 = window
    if "left" in edges:
        result[y1:y2, :x1] = registered_margin(complete[y1:y2, :x1][:, ::-1], core[:, 0])[:, ::-1]
    if "right" in edges:
        result[y1:y2, x2:] = registered_margin(complete[y1:y2, x2:], core[:, -1])
    if "top" in edges:
        paint = complete[:y1, x1:x2][::-1].swapaxes(0, 1)
        result[:y1, x1:x2] = registered_margin(paint, core[0]).swapaxes(0, 1)[::-1]
    if "bottom" in edges:
        paint = complete[y2:, x1:x2].swapaxes(0, 1)
        result[y2:, x1:x2] = registered_margin(paint, core[-1]).swapaxes(0, 1)
    return result


def reconstruct(source, complete, window):
    x1, y1, x2, y2 = window
    view = np.s_[y1:y2, x1:x2]
    base = {index: image[view] for index, image in complete.items()}
    raw_mech = base[4][..., 3] > 0.55
    sky = estimate_sky(source)
    near, far, arc, armour, fire = reference_masks(source, raw_mech, sky)

    # Use the reference dust for the visible planes, with a separate painted
    # continuation under the nearer dust sheet.
    dust1 = np.zeros_like(base[1])
    put_colour(dust1, near, source)
    dust2 = np.zeros_like(base[2])
    dust2[near] = base[2][near]
    put_colour(dust2, far, source)

    known = armour & ~arc & ~fire & ~near & ~far
    body = fix_material(base[4], source, known, armour)
    weld_left(body, base[4], known)
    recover_armour(body, base[4], source, arc | fire | near | far)
    backdrop = np.broadcast_to(BACKGROUND, source.shape).copy()
    ring = align_completed_ring(base[3], source, arc)
    hidden = ~arc & ((body[..., 3] > 0.45) | fire)
    rear_ring = ring.copy()
    rear_ring[..., 3] *= hidden
    body_stack = over(over(backdrop, rear_ring), body)
    muzzle = derive_effect(source, body_stack, fire, 0.77)
    light = derive_effect(source, over(body_stack, muzzle), arc, 0.42)
    light[~arc] = ring[~arc]
    rear = np.zeros(complete[3].shape[:2], dtype=bool)
    rear[view] = hidden

    parts = {1: dust1, 2: dust2, 3: light, 4: body, 5: muzzle}
    edges = {1: ("left", "right", "bottom"), 2: ("left", "right", "bottom"),
             3: ("top", "right"), 4: (), 5: ("right",)}
    result = {i: fitted_extensions(complete[i], parts[i], window, edges[i]) for i in parts}
    result[6] = complete[6]
    return result, rear


def original_position(layers, rear, window):
    left, top, right, bottom = window
    view = np.s_[top:bottom, left:right]
    canvas = layers[6][view][..., :3]
    for i, selector in ((3, rear[view]), (4, None), (5, None),
                        (3, ~rear[view]), (2, None), (1, None)):
        plane = layers[i][view]
        if selector is not None:
            plane = plane.copy()
            plane[..., 3] *= selector
        canvas = over(canvas, plane)
    return canvas


def structural_similarity(source, stack):
    # Full-window RGB SSIM with an 11×11 Gaussian neighbourhood.
    def blur(image):
        return ndi.gaussian_filter(image, (1.5, 1.5, 0), radius=(5, 5, 0))

    mean_source, mean_stack = blur(source), blur(stack)
    square_source, square_stack = mean_source ** 2, mean_stack ** 2
    product = mean_source * mean_stack
    variance = blur(source * source) + blur(stack * stack) - square_source - square_stack
    covariance = blur(source * stack) - product
    return float(np.mean(
        ((2 * product + 0.01 ** 2) * (2 * covariance + 0.03 ** 2)) /
        ((square_source + square_stack + 0.01 ** 2) * (variance + 0.03 ** 2))
    ))
