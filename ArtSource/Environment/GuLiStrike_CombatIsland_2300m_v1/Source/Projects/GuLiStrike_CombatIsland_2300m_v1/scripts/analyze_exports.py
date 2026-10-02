"""Validate actual Gaea output and derive material masks and map previews."""
from pathlib import Path
from collections import deque
import argparse
import hashlib
import json
import struct
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT=Path(__file__).resolve().parents[1]
LAYOUT=json.loads((ROOT/'layout.json').read_text(encoding='utf-8')) if (ROOT/'layout.json').exists() else {}
BUILD=ROOT.parent.parent/'Builds'/ROOT.name
SPAN,ZMIN,RELIEF=2300.0,-20.0,140.0
COLORS={'flat_ground':[126,170,98],'hills':[105,147,87],'plateau':[169,163,116],'rock':[133,141,144],'beach':[224,207,151],'water':[42,135,155]}

def smooth(a,b,v):
    t=np.clip((v-a)/(b-a),0,1)
    return t*t*(3-2*t)

def read16(path):
    with open(path,'rb') as f: header=f.read(29)
    assert header[:8]==b'\x89PNG\r\n\x1a\n',str(path)
    width,height,depth,color=struct.unpack('>IIBB',header[16:26])
    assert depth==16 and color==0,(str(path),depth,color)
    data=np.asarray(Image.open(path),dtype=np.uint16)
    assert data.shape==(height,width),data.shape
    return data

def source(name,n):
    image=Image.open(ROOT/'inputs'/(name+'.png'))
    if image.size!=(n,n):image=image.resize((n,n),Image.Resampling.BILINEAR)
    return np.asarray(image,dtype=np.float32)/65535

def classified(height,slope):
    n=height.shape[0]
    water=1-smooth(-.1,.3,height)
    dry=1-water
    beach=(1-smooth(3,7,height))*(1-smooth(12,21,slope))*dry
    mountain=source('mountain_region',n)
    rock=np.maximum(smooth(20,33,slope),mountain*smooth(55,85,height))*dry*(1-beach)
    plateau=source('plateau_region',n)*(1-smooth(12,23,slope))*smooth(28,38,height)*dry*(1-beach)*(1-rock)
    hills=np.maximum(smooth(5,15,slope),.8*source('hills_region',n))*dry*(1-beach)*(1-rock)*(1-plateau)
    flat=np.maximum(0,dry-beach-rock-plateau-hills)
    fields={'flat_ground':flat,'hills':hills,'plateau':plateau,'rock':rock,'beach':beach,'water':water}
    total=sum(fields.values())
    return {k:np.clip(v/np.maximum(total,1e-8),0,1) for k,v in fields.items()}

def save16(field,path,n=None):
    value=Image.fromarray(np.asarray(field,dtype=np.float32))
    if n and value.size!=(n,n):value=value.resize((n,n),Image.Resampling.BILINEAR)
    Image.fromarray(np.rint(np.clip(np.asarray(value),0,1)*65535).astype(np.uint16)).save(path)

def write_source_masks(height):
    step=SPAN/(height.shape[0]-1)
    gy,gx=np.gradient(height,step)
    slope=np.degrees(np.arctan(np.hypot(gx,gy)))
    fields=classified(height,slope)
    for key,value in fields.items():save16(value,ROOT/'inputs'/('mask_'+key+'.png'),4096)
    return fields,slope

def route_distance(x,y,points):
    distance=np.full(x.shape,np.inf,dtype=np.float32)
    for a,b in zip(points[:-1],points[1:]):
        ax,ay=a[:2];bx,by=b[:2];dx,dy=bx-ax,by-ay
        t=np.clip(((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy),0,1)
        distance=np.minimum(distance,np.hypot(x-ax-t*dx,y-ay-t*dy))
    return distance

def land_connectivity(dry):
    """Count exact 4-neighbor components using row runs, without downsampling."""
    parent=[];areas=[];previous=[]
    def find(label):
        while parent[label]!=label:
            parent[label]=parent[parent[label]]
            label=parent[label]
        return label
    for row in dry:
        changes=np.diff(np.pad(row.astype(np.int8),(1,1)))
        starts=np.flatnonzero(changes==1);ends=np.flatnonzero(changes==-1)
        current=[];cursor=0
        for start,end in zip(starts,ends):
            label=len(parent);parent.append(label);areas.append(int(end-start))
            while cursor<len(previous) and previous[cursor][1]<=start:cursor+=1
            other=cursor
            while other<len(previous) and previous[other][0]<end:
                a,b=find(label),find(previous[other][2])
                if a!=b:parent[a]=b;areas[b]+=areas[a]
                other+=1
            current.append((int(start),int(end),label))
        previous=current
    component_areas=[areas[i] for i in range(len(parent)) if parent[i]==i]
    total=int(np.count_nonzero(dry))
    return {'method':'full-resolution 4-neighbor run-length components','component_count':len(component_areas),
            'largest_component_fraction':max(component_areas,default=0)/max(total,1)}

def font(size):
    return ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',size)

def previews(height,fields,slope,directory):
    n=1100
    mini=lambda v:np.asarray(Image.fromarray(v.astype(np.float32)).resize((n,n),Image.Resampling.BILINEAR))
    h=mini(height);s=mini(slope)
    rgb=np.zeros((n,n,3),dtype=np.float32)
    for key,value in fields.items():rgb+=mini(value)[...,None]*np.array(COLORS[key],dtype=np.float32)
    gy,gx=np.gradient(h,SPAN/(n-1))
    length=np.sqrt(1+gx*gx+gy*gy)
    light=np.clip((-.48*gx+.36*gy+.8)/length,0,1)
    shade=.58+.52*light
    rgb*=shade[...,None]
    water=mini(fields['water'])
    deep=np.clip(-h/20,0,1)[...,None]
    ocean=np.array([47,167,179])*(1-deep)+np.array([22,64,99])*deep
    rgb=rgb*(1-water[...,None])+ocean*water[...,None]
    map_image=Image.fromarray(np.clip(rgb,0,255).astype(np.uint8))
    def framed(image,title,subtitle):
        canvas=Image.new('RGB',(n+100,n+160),(21,29,38));canvas.paste(image,(50,100))
        draw=ImageDraw.Draw(canvas)
        draw.text((50,20),title,font=font(27),fill=(230,237,244))
        draw.text((50,59),subtitle,font=font(17),fill=(156,179,190))
        draw.text((n-50,110),'N ↑',font=font(23),fill=(242,241,225))
        draw.line([(80,n+122),(80+round(250/SPAN*n),n+122)],fill=(230,237,244),width=4)
        draw.text((80,n+130),'250 m',font=font(14),fill=(190,208,218))
        return canvas
    framed(map_image,'战斗海岛 · 实际高度图预览','2300 × 2300 m  |  自然非对称  |  海平面 0 m').save(directory/'topdown_preview.png')
    route_image=map_image.copy();draw=ImageDraw.Draw(route_image)
    point=lambda p:((p[0]+SPAN/2)/SPAN*(n-1),(SPAN/2-p[1])/SPAN*(n-1))
    route_colors=[(255,236,131),(98,221,239),(248,160,190)]
    for route,color in zip(LAYOUT['routes'],route_colors):
        pts=[point(p) for p in route['points']]
        draw.line(pts,fill=(28,41,47),width=7,joint='curve')
        draw.line(pts,fill=color,width=3,joint='curve')
        mid=pts[1] if route['name']=='Central' else pts[len(pts)//2]
        label={'Central':'中央主路','West':'西侧绕行','East':'东侧峡谷'}[route['name']]
        draw.text((mid[0]+9,mid[1]-20),label,font=font(19),fill=color,stroke_width=2,stroke_fill=(22,30,33))
    for pad in LAYOUT['pads']:
        cx,cy=pad['center_m'];sx,sy=pad['size_m']
        a=point((cx-sx/2,cy+sy/2));b=point((cx+sx/2,cy-sy/2))
        draw.rectangle([a,b],outline=(252,247,204),width=3)
        label={'Southwest':'西南集结区','Northeast':'东北集结区'}[pad['name']]
        draw.text((a[0],b[1]+7),label+' 160×160m',font=font(16),fill=(252,247,204),stroke_width=1,stroke_fill=(21,29,38))
    framed(route_image,'战斗海岛 · 三条通路与集结区','中央主路 ≥50 m  |  两条侧路 ≥40 m  |  高台设缓坡入口').save(directory/'routes_preview.png')
    slope_rgb=np.zeros((n,n,3),np.uint8)
    for a,b,color in [(0,3,(67,151,150)),(3,12,(128,183,96)),(12,15,(215,211,91)),(15,27,(226,148,75)),(27,91,(192,79,73))]:
        slope_rgb[(s>=a)&(s<b)]=color
    slope_rgb[h<=0]=(25,63,92)
    framed(Image.fromarray(slope_rgb),'战斗海岛 · 坡度分区','蓝绿 0–3°  |  绿 3–12°  |  黄 12–15°  |  橙 15–27°  |  红 >27°').save(directory/'slope_preview.png')

def analyze(stage,refresh=False):
    if stage=='foundation':
        height=read16(ROOT/'inputs'/'foundation_height.png').astype(np.float32)/65535*RELIEF+ZMIN
        fields,slope=write_source_masks(height)
        print(json.dumps({'success':True,'stage':'foundation_masks','mask_count':len(fields)}));return
    directory=BUILD/stage
    imagepath=directory/'height_4096.png'
    raw=read16(imagepath);height=raw.astype(np.float32)/65535*RELIEF+ZMIN;n=raw.shape[0]
    expected=1024 if stage=='draft' else 4096
    assert raw.shape==(expected,expected),raw.shape
    step=SPAN/(n-1)
    gy,gx=np.gradient(height,step)
    slope=np.degrees(np.arctan(np.hypot(gx,gy)))
    fields=classified(height,slope)
    for key,value in fields.items():save16(value,directory/('material_'+key+'.png'))
    if refresh:
        for key,value in fields.items():save16(value,ROOT/'inputs'/('mask_'+key+'.png'),4096)
    previews(height,fields,slope,directory)
    axis=np.linspace(-SPAN/2,SPAN/2,n,dtype=np.float32)
    x,y=np.meshgrid(axis,axis[::-1])
    dry=height>0.3
    passable=dry&(slope<=15)
    checks={'resolution':True,'png_16bit':True,'height_range':bool(height.min()>=-20.01 and height.max()<=120.01),'sea_floor':bool(abs(float(height[0,0])+20)<.01),'land_within_1800m':bool(not np.any(dry&((np.abs(x)>900+2*step)|(np.abs(y)>900+2*step))))}
    dry_count=np.count_nonzero(dry)
    ratio=float(np.count_nonzero(passable)/max(dry_count,1))
    checks['sixty_percent_gentle_land']=ratio>=.60
    route_reports=[]
    for route in LAYOUT['routes']:
        distance=route_distance(x,y,route['points'])
        core=distance<=route['minimum_width_m']/2
        maximum=float(np.max(slope[core]))
        minimum=float(np.min(height[core]))
        passed=maximum<=12 and minimum>.3
        checks['route_'+route['name']]=passed
        route_reports.append({'name':route['name'],'verified_width_m':route['minimum_width_m'],'maximum_slope_degrees':maximum,'minimum_height_m':minimum,'passed':passed})
        print('Route '+route['name']+': '+str(round(maximum,3))+' degrees',flush=True)
        del distance,core
    pad_reports=[]
    for pad in LAYOUT['pads']:
        cx,cy=pad['center_m'];sx,sy=pad['size_m']
        area=(np.abs(x-cx)<=sx/2)&(np.abs(y-cy)<=sy/2)
        maximum=float(np.max(slope[area]));deviation=float(np.max(np.abs(height[area]-pad['height_m'])))
        passed=maximum<=3 and deviation<.05
        checks['pad_'+pad['name']]=passed
        pad_reports.append({'name':pad['name'],'size_m':pad['size_m'],'maximum_slope_degrees':maximum,'maximum_height_error_m':deviation,'passed':passed})
    coarse=np.asarray(Image.fromarray(height).resize((512,512),Image.Resampling.BILINEAR))
    cy,cx=np.gradient(coarse,SPAN/511)
    walk=(coarse>.3)&(np.degrees(np.arctan(np.hypot(cx,cy)))<=15)
    cell=lambda p:(int(round((SPAN/2-p[1])/SPAN*511)),int(round((p[0]+SPAN/2)/SPAN*511)))
    start=cell(LAYOUT['pads'][0]['center_m']);end=cell(LAYOUT['pads'][1]['center_m'])
    visited=np.zeros_like(walk);queue=deque([start]);visited[start]=True
    while queue:
        yy,xx=queue.popleft()
        for ny,nx in [(yy-1,xx),(yy+1,xx),(yy,xx-1),(yy,xx+1)]:
            if 0<=ny<512 and 0<=nx<512 and walk[ny,nx] and not visited[ny,nx]:
                visited[ny,nx]=True;queue.append((ny,nx))
    checks['connected_pads']=bool(visited[end])
    connectivity=land_connectivity(dry)
    checks['single_main_landmass']=connectivity['component_count']==1
    report=json.loads((directory/'report.json').read_text(encoding='utf-8'))
    checks['gaea_build_success']=report['Result']=='Success' and report['Resolution']==n
    result={'passed':all(checks.values()),'stage':stage,'resolution':[n,n],'physical_size_m':[SPAN,SPAN],
      'pixel_spacing_m':step,'height_range_m':[float(height.min()),float(height.max())],'sea_level_m':0,'height_decode':'sample_u16/65535*140-20',
      'gentle_land_fraction':ratio,'dry_land_area_km2':float(dry_count*step*step/1e6),'connectivity':connectivity,'checks':checks,'routes':route_reports,'pads':pad_reports,
      'height_sha256':hashlib.sha256(imagepath.read_bytes()).hexdigest(),'gaea_build_report':report,
      'mask_source_refreshed':refresh,'validation_boundary':'Terrain data only; no Unreal import, navigation bake, or gameplay run.'}
    if stage=='final':
        compatibility=Image.fromarray(raw.astype(np.float32)).resize((4081,4081),Image.Resampling.BILINEAR)
        values=np.rint(np.clip(np.asarray(compatibility),0,65535)).astype(np.uint16)
        Image.fromarray(values).save(directory/'height_4081.png')
        values.astype('<u2').tofile(directory/'height_4081.r16')
        result['compatibility']={'resolution':[4081,4081],'pixel_spacing_m':SPAN/4080,'png':'height_4081.png','r16':'height_4081.r16','r16_bytes':int(values.size*2),'normalization_unchanged':True}
        assert read16(directory/'height_4081.png').shape==(4081,4081)
    (directory/'validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:result[k] for k in ['passed','stage','resolution','height_range_m','gentle_land_fraction','checks']},ensure_ascii=False),flush=True)
    if not result['passed']:raise SystemExit(2)

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('stage',choices=['foundation','draft','final'])
    parser.add_argument('--refresh-source-masks',action='store_true')
    args=parser.parse_args()
    analyze(args.stage,args.refresh_source_masks)
