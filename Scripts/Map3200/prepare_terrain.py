"""Prepare numerical Landscape data for the approved 3.2 km / 9x9 migration."""
from pathlib import Path
import json
import math
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Artifacts/Map3200/20260923"
SOURCE = ROOT / "Artifacts/Map4200/20260922"
SIZE, HALF, CELL = 4081, 1600.0, 300.0
SCALE = 3200.0 / 8160.0
STEP = 3200.0 / (SIZE - 1)


def smooth(low, high, value):
    t = np.clip((value - low) / (high - low), 0, 1)
    return t * t * (3 - 2 * t)


def sample(field, x, y):
    ix, iy = (x + HALF) / STEP, (y + HALF) / STEP
    ix, iy = np.clip(ix, 0, SIZE-1), np.clip(iy, 0, SIZE-1)
    x0, y0 = int(ix), int(iy)
    x1, y1 = min(x0+1, SIZE-1), min(y0+1, SIZE-1)
    return float((field[y0,x0]*(1-(ix-x0))+field[y0,x1]*(ix-x0))*(1-(iy-y0))
                 +(field[y1,x0]*(1-(ix-x0))+field[y1,x1]*(ix-x0))*(iy-y0))


def profile(axis, levels):
    segment = np.clip(np.floor((axis+1200)/CELL).astype(np.int32), 0, 7)
    distance = axis - (-1200+segment*CELL)
    active = np.clip(distance-40, 0, 220)
    blend = np.where(active < 20, active*active/8000,
                     np.where(active > 200, 1-(220-active)**2/8000, (active-10)/200))
    result = levels[segment] + (levels[segment+1]-levels[segment])*blend
    return np.where(axis <= -1200, levels[0], np.where(axis >= 1200, levels[8], result)).astype(np.float32)


def main():
    terrain = OUT / "terrain"
    terrain.mkdir(parents=True, exist_ok=True)
    base = json.loads((SOURCE / "baseline.json").read_text(encoding="utf-8"))
    info = base["landscape"]["info"]
    original = np.asarray(Image.open(SOURCE / "terrain/source-height.png"))
    assert original.shape == (8161, 8161)
    height = original[::2, ::2].astype(np.float32)
    del original
    height = ((height-32768)*info["scale"][2]/128 + info["location"][2])/100*SCALE
    axis = np.linspace(-HALF, HALF, SIZE, dtype=np.float32)
    x, y = axis[None,:], axis[:,None]
    radius = np.sqrt(x*x+y*y)
    square = np.maximum(np.abs(x), np.abs(y))
    rho = np.sqrt((x/(3600*SCALE))**2+(y/(3300*SCALE))**2)
    theta = np.arctan2(y/(3300*SCALE), x/(3600*SCALE))
    basin = (-75+15*np.minimum(rho/.76,1)**2+2.5*np.cos(theta*2)*np.minimum(rho,.75)**2)*SCALE
    # Keep the center landmark, and move the rim outside the square battle area.
    carve = smooth(450,650,radius)*(1-smooth(1360,1540,square))
    height += (np.minimum(height,basin)-height)*carve
    del radius,square,rho,theta,basin,carve
    centers = np.arange(-1200,1201,CELL,dtype=np.float32)
    levels = np.array([[sample(height,float(cx),float(cy)) for cx in centers] for cy in centers],dtype=np.float32)
    for _ in range(9):
        previous = levels.copy()
        for r in range(9):
            for c in range(9):
                neighbors = [previous[rr,cc] for rr,cc in ((r-1,c),(r+1,c),(r,c-1),(r,c+1)) if 0<=rr<9 and 0<=cc<9]
                levels[r,c] = min(previous[r,c],min(neighbors)+32)
        if np.array_equal(previous,levels): break
    roads = np.zeros_like(height)
    weights = np.zeros_like(height)
    for index, center in enumerate(centers):
        weight = (1-smooth(25,75,np.abs(axis-center))).astype(np.float32)
        roads += weight[:,None]*profile(axis,levels[index,:])[None,:]
        roads += weight[None,:]*profile(axis,levels[:,index])[:,None]
        weights += weight[:,None]+weight[None,:]
    board = 1-smooth(1350,1410,np.maximum(np.abs(x),np.abs(y)))
    height += (roads/np.maximum(weights,1e-6)-height)*np.clip(weights,0,1)*board
    del roads,weights,board
    for r,cy in enumerate(centers):
        for c,cx in enumerate(centers):
            ix,iy = int(round((cx+HALF)/STEP)),int(round((cy+HALF)/STEP))
            half = math.ceil(80/STEP)
            xx,yy = axis[ix-half:ix+half+1][None,:]-cx,axis[iy-half:iy+half+1][:,None]-cy
            view = height[iy-half:iy+half+1,ix-half:ix+half+1]
            view += (levels[r,c]-view)*(1-smooth(40,80,np.sqrt(xx*xx+yy*yy)))
    # Small ore terraces next to the four ramp approaches, preserving their road cores.
    for cx,cy in ((0,300),(0,-300),(300,0),(-300,0)):
        ns = cx==0
        road = profile(axis,levels[:,4] if ns else levels[4,:])
        for dx in (-87.5,87.5):
            for dy in (-87.5,87.5):
                px,py = cx+dx,cy+dy
                level = float(np.interp(py if ns else px,axis,road))
                ix0,ix1 = np.searchsorted(axis,px-90),np.searchsorted(axis,px+90)+1
                iy0,iy1 = np.searchsorted(axis,py-90),np.searchsorted(axis,py+90)+1
                xx,yy = axis[ix0:ix1][None,:],axis[iy0:iy1][:,None]
                core = (np.abs(xx-np.round(xx/CELL)*CELL)<=25)|(np.abs(yy-np.round(yy/CELL)*CELL)<=25)
                weight = (1-smooth(42,90,np.sqrt((xx-px)**2+(yy-py)**2)))*(~core)
                view = height[iy0:iy1,ix0:ix1]
                view += (level-view)*weight
    # Grade the entire initial army / engineering / home factory apron, not one anchor.
    for sign in (-1,1):
        level = sample(height,0,sign*1125)
        inside_y = smooth(895,935,sign*y)*(1-smooth(1320,1370,sign*y))
        weight = (1-smooth(230,280,np.abs(x)))*inside_y
        # A constant complete heightfield tile has zero-thickness collision bounds
        # in this editor build. A 0.1% apron grade keeps a nondegenerate height range.
        height += (level+(sign*y-1125)*.001-height)*weight
    z_scale, z_origin = info['scale'][2]*SCALE, info['location'][2]*SCALE
    encoded = np.rint(32768+(height*100-z_origin)*128/z_scale)
    assert encoded.min()>0 and encoded.max()<65535
    Image.fromarray(encoded.astype(np.uint16)).save(terrain/'height-3200.png')
    for layer in info['layers']:
        Image.open(SOURCE/('terrain/source-weight-'+layer['layer_name']+'.png')).resize((SIZE,SIZE),Image.Resampling.BILINEAR).save(terrain/('weight-3200-'+layer['layer_name']+'.png'))
    gy,gx = np.gradient(height,STEP)
    slope = np.degrees(np.arctan(np.sqrt(gx*gx+gy*gy)))
    road_distance = np.abs(axis-np.round(axis/CELL)*CELL)
    board = np.abs(axis)<=1350
    road_core = board[:,None]&board[None,:]&((road_distance[:,None]<=20)|(road_distance[None,:]<=20))
    road_max = float(slope[road_core].max())
    pads = [{'key':f'Outpost_R{r}C{c}','x_cm':(c-5)*30000,'y_cm':(5-r)*30000,
             'ground_z_cm':sample(height,(c-5)*300,(5-r)*300)*100,
             'slope_deg':sample(slope,(c-5)*300,(5-r)*300)} for r in range(1,10) for c in range(1,10)]
    data = {'resolution':[SIZE,SIZE],'components':[16,16],'sections_per_component':1,'quads_per_section':255,
            'location':[-160000,-160000,z_origin],'scale':[320000/4080,320000/4080,z_scale],
            'min_z_cm':float(height.min())*100,'max_z_cm':float(height.max())*100,'center_heights_m':levels.tolist(),
            'outposts':pads,'source_layers':info['layers'],'maximum_road_core_slope_deg':road_max,
            'anchors':{name:[0,sign*ym*100,sample(height,0,sign*ym)*100] for name,sign,ym in
                       [('red_factory',1,1275),('red_assembly',1,1125),('blue_factory',-1,1275),('blue_assembly',-1,1125)]}}
    if (OUT/'initial-army-layout.json').exists():
        army = json.loads((OUT/'initial-army-layout.json').read_text())
        data['maximum_initial_army_slope_deg'] = max(sample(slope,s['x']/100,s['y']/100) for s in army['slots'])
        assert data['maximum_initial_army_slope_deg']<=15,data
    # Full height data stays recoverable even if a numerical constraint needs refinement.
    (terrain/'layout-3200.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
    preview = height[::4,::4]
    shade = np.clip(.88-gx[::4,::4]*.5+gy[::4,::4]*.3,.45,1.15)
    t = np.clip((preview+35)/90,0,1)[...,None]
    rgb = (np.array([66,82,84])*(1-t)+np.array([174,170,152])*t)*shade[...,None]
    image = Image.fromarray(np.clip(rgb,0,255).astype(np.uint8)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    draw = ImageDraw.Draw(image)
    point = lambda xx,yy:((xx+HALF)/3200*(image.width-1),(HALF-yy)/3200*(image.height-1))
    for coordinate in np.arange(-1350,1351,300):
        draw.line([point(coordinate,-1350),point(coordinate,1350)],fill=(115,155,170),width=1)
        draw.line([point(-1350,coordinate),point(1350,coordinate)],fill=(115,155,170),width=1)
    for pad in pads:
        px,py = point(pad['x_cm']/100,pad['y_cm']/100)
        draw.ellipse((px-3,py-3,px+3,py+3),fill=(244,212,143))
    image.save(terrain/'layout-preview.png')
    print(json.dumps({'success':road_max<=15,'maximum_road_slope':road_max,'maximum_pad_slope':max(p['slope_deg'] for p in pads),'z_range_cm':[data['min_z_cm'],data['max_z_cm']]}))
    assert road_max<=15,road_max


if __name__=='__main__': main()
