"""Create reproducible macro layout and 16-bit masks for the Gaea graph."""
from pathlib import Path
import argparse
import json
import math
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
NAME = 'GuLiStrike_CombatIsland_2300m_v1'
SPAN, ZMIN, ZMAX, SEED = 2300.0, -20.0, 120.0, 20261001
RELIEF = ZMAX-ZMIN
ROUTES = [
    {'name':'Central','width_m':60.0,'minimum_width_m':50.0,'points':[[-390,-350,9],[0,0,18],[210,240,23],[390,380,26]]},
    {'name':'West','width_m':48.0,'minimum_width_m':40.0,'points':[[-390,-350,9],[-560,-170,12],[-420,180,18],[-170,440,23],[170,560,26],[390,380,26]]},
    {'name':'East','width_m':48.0,'minimum_width_m':40.0,'points':[[-390,-350,9],[-140,-480,13],[230,-360,19],[480,-90,31],[490,80,40],[390,380,26]]},
]
PADS = [
    {'name':'Southwest','center_m':[-390,-350],'size_m':[160,160],'height_m':9.0},
    {'name':'Northeast','center_m':[390,380],'size_m':[160,160],'height_m':26.0},
]

def smooth(a,b,t):
    v=np.clip((t-a)/(b-a),0,1)
    return v*v*(3-2*v)

def route_fields(x,y,points,endpoint_pads=False):
    distance=np.full(x.shape,np.inf,dtype=np.float32)
    levels=np.zeros(x.shape,dtype=np.float32)
    for index,(a,b) in enumerate(zip(points[:-1],points[1:])):
        ax,ay,az=a; bx,by,bz=b
        vx,vy=bx-ax,by-ay
        t=np.clip(((x-ax)*vx+(y-ay)*vy)/(vx*vx+vy*vy),0,1)
        d=np.hypot(x-ax-t*vx,y-ay-t*vy)
        replace=d<distance
        # Flat tangents at each waypoint prevent nearest-segment height seams
        # on the inside of sharp road bends.
        length=math.hypot(vx,vy)
        start_flat=120 if endpoint_pads and index==0 else min(.27*length,55)
        end_flat=120 if endpoint_pads and index==len(points)-2 else min(.27*length,55)
        assert start_flat+end_flat<length
        grade_t=smooth(start_flat/length,1-end_flat/length,t)
        levels=np.where(replace,az+(bz-az)*grade_t,levels)
        distance=np.minimum(distance,d)
    return distance,levels

def save16(field,path,size):
    value=Image.fromarray(np.asarray(field,dtype=np.float32)).resize((size,size),Image.Resampling.BILINEAR)
    encoded=np.rint(np.clip(np.asarray(value),0,1)*65535).astype(np.uint16)
    Image.fromarray(encoded).save(path)

def generate(size=4096,design_size=1024):
    inputs=ROOT/'inputs'; inputs.mkdir(parents=True,exist_ok=True)
    axis=np.linspace(-SPAN/2,SPAN/2,design_size,dtype=np.float32)
    x,y=np.meshgrid(axis,axis[::-1])
    theta=np.arctan2((y-25)/790,(x-20)/805)
    rho=np.hypot((x-20)/805,(y-25)/790)
    radius=1+.055*np.sin(3*theta+.4)+.035*np.cos(5*theta-.7)+.025*np.sin(7*theta)+.025*np.cos(theta+.4)
    distance=(radius-rho)*790
    # A recessed southeast bay stays outside the east flanking corridor.
    distance-=260*np.exp(-((x-630)/240)**2-((y+480)/190)**2)
    distance+=55*np.exp(-((x+720)/170)**2-((y+130)/200)**2)
    distance=np.minimum(distance,np.minimum(900-np.abs(x),900-np.abs(y)))
    interior=smooth(0,150,distance)
    south=1-smooth(-300,150,y)
    offshore=-20+20*smooth(-170,0,distance)
    landlevel=(10*(1-south)+4*south)*smooth(0,110,distance)
    plane=np.maximum(0,3+.003*x+.002*y)*interior
    base=np.where(distance>=0,landlevel+plane,offshore)
    cliff_region=smooth(220,450,y)*(1-smooth(-60,240,x))*(1-smooth(95,210,distance))*smooth(-2,35,distance)
    base+=36*cliff_region
    river_points=[[660,370,22],[620,190,16],[650,-30,8],[570,-250,3],[560,-400,-1],[620,-500,-5]]
    river_distance,river_levels=route_fields(x,y,river_points)
    # Carry the low valley through the shoreline rather than restoring a beach
    # ridge across its mouth during coastline protection.
    mouth=(1-smooth(10,60,river_distance))*(1-smooth(-400,-180,y))*smooth(-100,20,distance)
    base=base*(1-mouth)+np.minimum(base,river_levels)*mouth
    coast_profile=base.copy()
    hill_region=np.exp(-.5*((x-20)/340)**2-.5*((y-120)/280)**2)*interior
    base+=12*hill_region
    mountain_region=np.exp(-.5*((x+390)/215)**2-.5*((y-430)/220)**2)*interior
    base+=(105*np.exp(-.5*((x+390)/130)**2-.5*((y-430)/145)**2)
           +22*np.exp(-.5*((x+590)/90)**2-.5*((y-230)/130)**2))*interior
    plateau_radius=np.hypot((x-455)/180,(y-125)/135)
    plateau=1-smooth(.65,1.40,plateau_radius)
    base=base*(1-plateau)+44*plateau
    valley=(1-smooth(15,85,river_distance))*interior
    base=base*(1-valley)+river_levels*valley
    protection=np.zeros_like(base)
    road_core=np.zeros_like(base)
    route_distances={}
    for route in ROUTES:
        d,levels=route_fields(x,y,route['points'],endpoint_pads=True)
        half=route['width_m']/2
        grade=1-smooth(half+12,half+135,d)
        base=base*(1-grade)+levels*grade
        protected=1-smooth(half+6,half+68,d)
        protection=np.maximum(protection,protected)
        road_core=np.maximum(road_core,1-smooth(half+8,half+30,d))
        route_distances[route['name']]=d
    for pad in PADS:
        cx,cy=pad['center_m']; sx,sy=pad['size_m']
        edge=np.maximum(np.abs(x-cx)-sx/2,np.abs(y-cy)-sy/2)
        grade=(1-smooth(0,150,edge))*(1-road_core)
        grade=np.maximum(grade,(edge<=0).astype(np.float32))
        base=base*(1-grade)+pad['height_m']*grade
        protection=np.maximum(protection,1-smooth(2,72,edge))
    # Grading must not extend a rectangular pad or road footprint into the sea.
    base=coast_profile+(base-coast_profile)*smooth(35,120,distance)
    base=np.clip(base,ZMIN,ZMAX)
    # Coastline and seabed are stable; all three routes and both pads are protected.
    shore_protection=1-smooth(25,120,distance)
    protection=np.maximum(protection,shore_protection)
    # Keep the tiny summit cap at the requested 120m while allowing erosion
    # and procedural detail on the surrounding mountain slopes.
    protection=np.maximum(protection,smooth(117,119,base))
    rough_area=np.clip(mountain_region+.7*hill_region+.3*plateau,0,1)
    erosion=rough_area*(1-protection)*smooth(8,20,base)
    mountain_gain=np.minimum(24*mountain_region,np.maximum(0,ZMAX-base))*(1-protection)/RELIEF
    hills_gain=(1.1+3.1*hill_region)*interior*(1-protection)/RELIEF
    plateau_gain=3.0*plateau*(1-protection)/RELIEF
    canyon_gain=2.5*valley*(1-protection)/RELIEF
    fields={
        'foundation_height':(base-ZMIN)/RELIEF,
        'mountain_gain':mountain_gain,'hills_gain':hills_gain,
        'plateau_gain':plateau_gain,'canyon_gain':canyon_gain,
        'erosion_allowed':erosion,'protection':protection,'unprotected':1-protection,
        'land_outline':smooth(-2,2,distance),'plateau_region':plateau,
        'mountain_region':mountain_region,'hills_region':hill_region,
        'canyon_region':valley,'beach_region':(1-smooth(55,100,distance))*smooth(-8,0,distance)*(1-cliff_region),
        'water_region':1-smooth(-2,1,distance),
    }
    for route in ROUTES:
        fields['route_'+route['name'].lower()]=1-smooth(route['width_m']/2,route['width_m']/2+3,route_distances[route['name']])
    for key,field in fields.items():
        save16(field,inputs/(key+'.png'),size)
    metadata={
        'name':NAME,'seed':SEED,'physical_size_m':[SPAN,SPAN],'land_limit_m':[-900,900],
        'height_range_m':[ZMIN,ZMAX],'height_span_m':RELIEF,'sea_level_m':0,
        'sea_level_normalized':-ZMIN/RELIEF,'height_decode':'world_height_m = sample_u16/65535*140 - 20',
        'pixel_orientation':'image top is +Y (north); image right is +X (east)',
        'source_resolution':[size,size],'design_resolution':[design_size,design_size],
        'routes':ROUTES,'pads':PADS,'river_points':river_points,
        'source_files':{k:'inputs/'+k+'.png' for k in fields},
        'graph_parameters':{'mountain_amplitude_m':24,'hills_amplitude_m':[1.1,4.2],'plateau_amplitude_m':3,'canyon_amplitude_m':2.5,'erosion_duration':12,'erosion_downcutting':.05},
        'prompt':'自然非对称的战斗海岛，宽阔平原连接低丘、高台和峡谷，外围具有沙滩、浅湾及岩石陡岸；主岛连续，主要路线宽阔平缓，地形轮廓清楚。',
    }
    (ROOT/'layout.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'success':True,'source_count':len(fields),'resolution':size,'foundation_range_m':[float(base.min()),float(base.max())],'root':str(ROOT)},ensure_ascii=False))

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--size',type=int,default=4096)
    parser.add_argument('--design-size',type=int,default=1024)
    args=parser.parse_args()
    generate(args.size,args.design_size)
