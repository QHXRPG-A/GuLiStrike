"""Resample exported numerical Landscape data and grade the approved 7x7 board."""
from __future__ import annotations

import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Artifacts/Map4200/20260922"
TERRAIN = OUT / "terrain"
SIZE = 4081
HALF_M = 2100.0
SCALE = 4200.0 / 8160.0
STEP_M = 4200.0 / (SIZE - 1)


def smoothstep(low, high, x):
    t = np.clip((x - low) / (high - low), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def sample(height, x_m, y_m):
    ix = np.clip((x_m + HALF_M) / STEP_M, 0, SIZE - 1)
    iy = np.clip((y_m + HALF_M) / STEP_M, 0, SIZE - 1)
    x0, y0 = int(ix), int(iy)
    x1, y1 = min(x0 + 1, SIZE - 1), min(y0 + 1, SIZE - 1)
    fx, fy = ix - x0, iy - y0
    return float((height[y0, x0] * (1-fx) + height[y0, x1] * fx) * (1-fy)
                 + (height[y1, x0] * (1-fx) + height[y1, x1] * fx) * fy)


def grade_profile(axis, levels):
    """50 m level approaches, 20 m eased grade transitions, bounded centerline slope."""
    cell = np.clip(np.floor((axis + 1200.0) / 400.0).astype(np.int32), 0, 5)
    distance = axis - (-1200.0 + cell * 400.0)
    active = np.clip(distance - 50.0, 0.0, 300.0)
    blend = np.where(active < 20.0, active * active / 11200.0,
                     np.where(active > 280.0, 1.0 - (300.0-active)**2 / 11200.0,
                              (active-10.0) / 280.0))
    result = levels[cell] + (levels[cell + 1] - levels[cell]) * blend
    return np.where(axis <= -1200.0, levels[0], np.where(axis >= 1200.0, levels[6], result)).astype(np.float32)


def main():
    baseline = json.loads((OUT / "baseline.json").read_text(encoding="utf-8"))
    info = baseline["landscape"]["info"]
    original = np.asarray(Image.open(TERRAIN / "source-height.png"))
    assert original.shape == (8161, 8161)
    height = original[::2, ::2].astype(np.float32)
    del original
    height = ((height - 32768.0) * info["scale"][2] / 128.0 + info["location"][2]) / 100.0 * SCALE
    axis = np.linspace(-HALF_M, HALF_M, SIZE, dtype=np.float32)
    x, y = axis[None, :], axis[:, None]
    radius = np.sqrt(x*x + y*y)
    square_radius = np.maximum(np.abs(x), np.abs(y))
    rho = np.sqrt((x / (3600.0*SCALE))**2 + (y / (3300.0*SCALE))**2)
    theta = np.arctan2(y / (3300.0*SCALE), x / (3600.0*SCALE))
    basin = (-75.0 + 15.0 * np.minimum(rho / 0.76, 1.0)**2
             + 2.5 * np.cos(theta*2.0) * np.minimum(rho, 0.75)**2) * SCALE
    # Preserve the central landmark and original outer rim; push the old corner
    # scarps outside the square gameplay board rather than leaving corner outposts on them.
    carve = smoothstep(650.0, 850.0, radius) * (1.0-smoothstep(1450.0, 1750.0, square_radius))
    height += (np.minimum(height, basin) - height) * carve
    del radius, square_radius, rho, theta, basin, carve

    centers = np.arange(-1200.0, 1201.0, 400.0, dtype=np.float32)
    center_heights = np.array([[sample(height, float(cx), float(cy)) for cx in centers] for cy in centers], dtype=np.float32)
    # Limit adjacent pad elevation differences so the eased road/pad junctions,
    # including the east/west platform approaches, stay below 15 degrees.
    for _ in range(7):
        previous = center_heights.copy()
        for row in range(7):
            for col in range(7):
                adjacent = [previous[r,c] for r,c in ((row-1,col),(row+1,col),(row,col-1),(row,col+1))
                            if 0 <= r < 7 and 0 <= c < 7]
                center_heights[row,col] = min(previous[row,col], min(adjacent)+55.0)
        if np.array_equal(previous,center_heights): break
    road_sum = np.zeros_like(height)
    road_weight = np.zeros_like(height)
    for index, center in enumerate(centers):
        weight = (1.0 - smoothstep(25.0, 85.0, np.abs(axis - center))).astype(np.float32)
        profile_x = grade_profile(axis, center_heights[index, :])
        profile_y = grade_profile(axis, center_heights[:, index])
        road_sum += weight[:, None] * profile_x[None, :]
        road_sum += weight[None, :] * profile_y[:, None]
        road_weight += weight[:, None] + weight[None, :]
    board_blend = 1.0 - smoothstep(1400.0, 1500.0, np.maximum(np.abs(x), np.abs(y)))
    amount = np.clip(road_weight, 0.0, 1.0) * board_blend
    height += (road_sum / np.maximum(road_weight, 1e-6) - height) * amount
    del road_sum, road_weight, amount, board_blend
    # Flat 40 m pads around each fixed outpost; these fit inside the existing 130 m ore reserve.
    for row, cy in enumerate(centers):
        for col, cx in enumerate(centers):
            ix = int(round((float(cx) + HALF_M) / STEP_M))
            iy = int(round((float(cy) + HALF_M) / STEP_M))
            half = int(np.ceil(80.0 / STEP_M))
            xx = axis[ix-half:ix+half+1][None, :] - cx
            yy = axis[iy-half:iy+half+1][:, None] - cy
            weight = 1.0 - smoothstep(40.0, 80.0, np.sqrt(xx*xx + yy*yy))
            view = height[iy-half:iy+half+1, ix-half:ix+half+1]
            view += (center_heights[row, col] - view) * weight

    # Ore beside the four central approaches needs room for the unchanged
    # 25 m sampling footprint. Grade small terraces with level branches from
    # the adjacent road; leave the central landmark and 50 m road cores intact.
    ore_terraces = []
    for cx,cy in ((0.0,400.0),(0.0,-400.0),(400.0,0.0),(-400.0,0.0)):
        north_south = cx == 0.0
        profile = grade_profile(axis, center_heights[:,3] if north_south else center_heights[3,:])
        for dx in (-137.5,137.5):
            for dy in (-137.5,137.5):
                px,py = cx+dx,cy+dy
                level = float(np.interp(py if north_south else px,axis,profile))
                xlo,xhi = min(cx,px-110.0)-1,max(cx,px+110.0)+1
                ylo,yhi = min(cy,py-110.0)-1,max(cy,py+110.0)+1
                ix0,ix1 = np.searchsorted(axis,xlo),np.searchsorted(axis,xhi)+1
                iy0,iy1 = np.searchsorted(axis,ylo),np.searchsorted(axis,yhi)+1
                xx,yy = axis[ix0:ix1][None,:],axis[iy0:iy1][:,None]
                core = (np.abs(xx-np.round(xx/400.0)*400.0)<=25.0) | (np.abs(yy-np.round(yy/400.0)*400.0)<=25.0)
                weight = (1.0-smoothstep(65.0,110.0,np.sqrt((xx-px)**2+(yy-py)**2))) * (~core)
                view = height[iy0:iy1,ix0:ix1]
                view += (level-view)*weight
                along = (xx-cx)*np.sign(dx) if north_south else (yy-cy)*np.sign(dy)
                across = np.abs(yy-py) if north_south else np.abs(xx-px)
                branch = (1.0-smoothstep(20.0,45.0,across)) * (1.0-smoothstep(137.5,202.5,along)) * (along>=0) * (~core)
                road = profile[iy0:iy1][:,None] if north_south else profile[ix0:ix1][None,:]
                desired = road+(level-road)*smoothstep(25.0,75.0,along)
                view += (desired-view)*branch
                ore_terraces.append({'x_m':px,'y_m':py,'z_m':level,'flat_radius_m':65.0})

    target_z_scale = info["scale"][2] * SCALE
    target_z_origin_cm = info["location"][2] * SCALE
    encoded = np.rint(32768.0 + (height * 100.0 - target_z_origin_cm) * 128.0 / target_z_scale)
    assert float(encoded.min()) > 0 and float(encoded.max()) < 65535
    Image.fromarray(encoded.astype(np.uint16)).save(TERRAIN / "height-4200.png")
    for layer in info["layers"]:
        source = Image.open(TERRAIN / ("source-weight-" + layer["layer_name"] + ".png"))
        source.resize((SIZE, SIZE), Image.Resampling.BILINEAR).save(TERRAIN / ("weight-4200-" + layer["layer_name"] + ".png"))

    gy, gx = np.gradient(height, STEP_M)
    slope = np.degrees(np.arctan(np.sqrt(gx*gx + gy*gy)))
    road_distance = np.abs(axis - np.round(axis/400.0)*400.0)
    in_board = np.abs(axis) <= 1400.0
    road_core = in_board[:,None] & in_board[None,:] & ((road_distance[:,None] <= 20.0) | (road_distance[None,:] <= 20.0))
    road_maximum_slope = float(slope[road_core].max())
    assert road_maximum_slope < 15.0, road_maximum_slope
    pads = []
    for row in range(1, 8):
        for col in range(1, 8):
            px, py = (col-4)*400.0, (4-row)*400.0
            pads.append({"key": f"Outpost_R{row}C{col}", "x_cm": px*100.0, "y_cm": py*100.0,
                         "ground_z_cm": sample(height, px, py)*100.0,
                         "slope_deg": sample(slope, px, py)})
    assert max(p["slope_deg"] for p in pads) < 15.0
    data = {
        "resolution": [SIZE, SIZE], "components": [16, 16], "sections_per_component": 1, "quads_per_section": 255,
        "location": [-210000.0, -210000.0, target_z_origin_cm],
        "scale": [420000.0/4080.0, 420000.0/4080.0, target_z_scale],
        "min_z_cm": float(height.min())*100.0, "max_z_cm": float(height.max())*100.0,
        "center_heights_m": center_heights.tolist(), "outposts": pads,
        "anchors": {name: [0.0, sign*y_m*100.0, sample(height, 0.0, sign*y_m)*100.0]
                    for name, sign, y_m in [("red_factory",1,1300.0),("red_assembly",1,1100.0),("blue_factory",-1,1300.0),("blue_assembly",-1,1100.0)]},
        "maximum_outpost_slope_deg": max(p["slope_deg"] for p in pads),
        "road_half_width_cm": 2500.0, "object_scale_changed": False,
        "maximum_road_core_slope_deg": road_maximum_slope,
        "central_ore_terraces": ore_terraces,
    }
    (TERRAIN / "layout-4200.json").write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    # A numerical terrain preview for inspection; the final visual check is in UE.
    preview = height[::4, ::4]
    low = np.array([59.,75.,79.], dtype=np.float32)
    mid = np.array([153.,157.,144.], dtype=np.float32)
    high = np.array([112.,108.,105.], dtype=np.float32)
    t = np.clip((preview+40.0)/80.0,0.0,1.0)[...,None]
    q = np.clip((preview-40.0)/90.0,0.0,1.0)[...,None]
    rgb = (low*(1-t)+mid*t)*(1-q)+high*q
    shade = np.clip(0.88-gx[::4,::4]*0.5+gy[::4,::4]*0.3,0.45,1.15)
    image = Image.fromarray(np.clip(rgb*shade[...,None],0,255).astype(np.uint8)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    draw = ImageDraw.Draw(image)
    size = image.width
    point = lambda x,y: ((x+2100.0)/4200.0*(size-1),(2100.0-y)/4200.0*(size-1))
    for coordinate in np.arange(-1400.0,1401.0,400.0):
        draw.line([point(float(coordinate),-1400),point(float(coordinate),1400)],fill=(130,163,171),width=1)
        draw.line([point(-1400,float(coordinate)),point(1400,float(coordinate))],fill=(130,163,171),width=1)
    for p in pads:
        px,py=point(p['x_cm']/100.0,p['y_cm']/100.0)
        color = (255,107,100) if p['key']=='Outpost_R1C4' else ((100,173,255) if p['key']=='Outpost_R7C4' else (246,218,143))
        draw.ellipse((px-4,py-4,px+4,py+4),fill=color)
        draw.text((px+6,py-7),p['key'].removeprefix('Outpost_'),fill=color)
    image.save(TERRAIN / "layout-preview.png")
    print(json.dumps({"success": True, "resolution": data['resolution'], "components": 256,
                      "z_range_cm": [data['min_z_cm'],data['max_z_cm']], "max_outpost_slope_deg": data['maximum_outpost_slope_deg']}, indent=2))


if __name__ == "__main__":
    main()
