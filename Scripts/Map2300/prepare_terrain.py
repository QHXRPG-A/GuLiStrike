"""Rebuild the 2.3 km terrain from the original height/weight sources."""
from pathlib import Path
import json
import math
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Artifacts/Map2300/20260923'
SOURCE = ROOT / 'Artifacts/Map4200/20260922'
SIZE, HALF, CELL = 4081, 1150.0, 200.0
SCALE, STEP = 2300.0 / 8160.0, 2300.0 / 4080

def smooth(low, high, value):
    t = np.clip((value-low)/(high-low), 0, 1)
    return t*t*(3-2*t)

def sample(field, x, y):
    ix, iy = np.clip((x+HALF)/STEP, 0, SIZE-1), np.clip((y+HALF)/STEP, 0, SIZE-1)
    x0, y0 = int(ix), int(iy)
    x1, y1 = min(x0+1, SIZE-1), min(y0+1, SIZE-1)
    return float((field[y0,x0]*(1-ix+x0)+field[y0,x1]*(ix-x0))*(1-iy+y0)
                 +(field[y1,x0]*(1-ix+x0)+field[y1,x1]*(ix-x0))*(iy-y0))

def interpolation(axis):
    index = np.clip(np.floor((axis+800)/CELL).astype(np.int32), 0, 7)
    active = np.clip(axis-(-800+index*CELL)-20, 0, 160)
    blend = np.where(active < 15, active*active/(30*145),
                     np.where(active > 145, 1-(160-active)**2/(30*145), (active-7.5)/145))
    blend = np.where(axis <= -800, 0, np.where(axis >= 800, 1, blend))
    return index, blend.astype(np.float32)

def main():
    terrain = OUT/'terrain'
    terrain.mkdir(parents=True, exist_ok=True)
    info = json.loads((SOURCE/'baseline.json').read_text(encoding='utf-8'))['landscape']['info']
    original = np.asarray(Image.open(SOURCE/'terrain/source-height.png'))
    assert original.shape == (8161,8161)
    height = original[::2,::2].astype(np.float32)
    del original
    height = ((height-32768)*info['scale'][2]/128+info['location'][2])/100*SCALE
    axis = np.linspace(-HALF,HALF,SIZE,dtype=np.float32)
    x,y = axis[None,:],axis[:,None]
    radius = np.sqrt(x*x+y*y)
    rho = np.sqrt((x/(3600*SCALE))**2+(y/(3300*SCALE))**2)
    theta = np.arctan2(y/(3300*SCALE),x/(3600*SCALE))
    basin = (-75+15*np.minimum(rho/.76,1)**2+2.5*np.cos(theta*2)*np.minimum(rho,.75)**2)*SCALE
    carve = smooth(300,450,radius)*(1-smooth(920,1080,np.maximum(np.abs(x),np.abs(y))))
    height += (np.minimum(height,basin)-height)*carve
    del radius,rho,theta,basin,carve
    centers = np.arange(-800,801,CELL,dtype=np.float32)
    levels = np.array([[sample(height,float(cx),float(cy)) for cx in centers] for cy in centers],dtype=np.float32)
    for _ in range(9):
        previous=levels.copy()
        for r in range(9):
            for c in range(9):
                neighbors=[previous[rr,cc] for rr,cc in ((r-1,c),(r+1,c),(r,c-1),(r,c+1)) if 0<=rr<9 and 0<=cc<9]
                levels[r,c]=min(previous[r,c],min(neighbors)+22)
        if np.array_equal(previous,levels):break
    # Grade the whole cell, including the mine approaches, from the new pad network.
    index,blend=interpolation(axis)
    profiles=levels[:,index]*(1-blend)[None,:]+levels[:,index+1]*blend[None,:]
    graded=profiles[index,:]*(1-blend)[:,None]+profiles[index+1,:]*blend[:,None]
    weight=1-smooth(920,1080,np.maximum(np.abs(x),np.abs(y)))
    height+=(graded-height)*weight
    del graded,profiles,weight
    for sign in (-1,1):
        level=sample(height,0,sign*725)
        weight=(1-smooth(230,300,np.abs(x)))*smooth(485,525,sign*y)*(1-smooth(895,945,sign*y))
        # A small grade keeps heightfield collision tiles nondegenerate.
        height+=(level+(sign*y-725)*.001-height)*weight
    z_scale,z_origin=info['scale'][2]*SCALE,info['location'][2]*SCALE
    encoded=np.rint(32768+(height*100-z_origin)*128/z_scale)
    assert encoded.min()>0 and encoded.max()<65535
    Image.fromarray(encoded.astype(np.uint16)).save(terrain/'height-2300.png')
    for layer in info['layers']:
        Image.open(SOURCE/('terrain/source-weight-'+layer['layer_name']+'.png')).resize((SIZE,SIZE),Image.Resampling.BILINEAR).save(terrain/('weight-2300-'+layer['layer_name']+'.png'))
    gy,gx=np.gradient(height,STEP)
    slope=np.degrees(np.arctan(np.sqrt(gx*gx+gy*gy)))
    inside=np.abs(axis)<=900
    road_distance=np.abs(axis-np.round(axis/CELL)*CELL)
    roads=inside[:,None]&inside[None,:]&((road_distance[:,None]<=25)|(road_distance[None,:]<=25))
    pads=[{'key':f'Outpost_R{r}C{c}','x_cm':(c-5)*20000,'y_cm':(5-r)*20000,
           'ground_z_cm':sample(height,(c-5)*200,(5-r)*200)*100,
           'slope_deg':sample(slope,(c-5)*200,(5-r)*200)} for r in range(1,10) for c in range(1,10)]
    maximum=float(slope[np.ix_(inside,inside)].max())
    data={'resolution':[SIZE,SIZE],'components':[16,16],'sections_per_component':1,'quads_per_section':255,
          'location':[-115000,-115000,z_origin],'scale':[230000/4080,230000/4080,z_scale],
          'min_z_cm':float(height.min())*100,'max_z_cm':float(height.max())*100,'center_heights_m':levels.tolist(),
          'outposts':pads,'source_layers':info['layers'],'maximum_road_core_slope_deg':float(slope[roads].max()),
          'maximum_battlefield_slope_deg':maximum,
          'anchors':{name:[0,sign*ym*100,sample(height,0,sign*ym)*100] for name,sign,ym in
                     [('red_factory',1,875),('red_assembly',1,725),('blue_factory',-1,875),('blue_assembly',-1,725)]}}
    (terrain/'layout-2300.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
    preview=height[::4,::4]
    shade=np.clip(.88-gx[::4,::4]*.5+gy[::4,::4]*.3,.45,1.15)
    t=np.clip((preview+25)/65,0,1)[...,None]
    rgb=(np.array([66,82,84])*(1-t)+np.array([174,170,152])*t)*shade[...,None]
    image=Image.fromarray(np.clip(rgb,0,255).astype(np.uint8)).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    draw=ImageDraw.Draw(image)
    point=lambda xx,yy:((xx+HALF)/2300*(image.width-1),(HALF-yy)/2300*(image.height-1))
    for coordinate in np.arange(-900,901,200):
        draw.line([point(coordinate,-900),point(coordinate,900)],fill=(115,155,170),width=1)
        draw.line([point(-900,coordinate),point(900,coordinate)],fill=(115,155,170),width=1)
    for pad in pads:
        px,py=point(pad['x_cm']/100,pad['y_cm']/100)
        draw.ellipse((px-3,py-3,px+3,py+3),fill=(244,212,143))
    image.save(terrain/'layout-preview.png')
    print(json.dumps({'success':maximum<=15,'road_slope':data['maximum_road_core_slope_deg'],'battlefield_slope':maximum,'max_pad_slope':max(p['slope_deg'] for p in pads)}))
    assert maximum<=15,maximum

if __name__=='__main__':main()
