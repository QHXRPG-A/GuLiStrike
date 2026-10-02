"""Prepare explicit marker, density and water inputs, preserving source identities."""
from pathlib import Path
import copy,json,math
import numpy as np
from PIL import Image,ImageFilter

REPO=Path(__file__).resolve().parents[2]
NAME='GuLiStrike_CommanderIsland_1800m_v1'
ROOT=REPO/'ArtSource/Environment'/NAME
FILES=ROOT/'UEImport'
BEFORE=json.loads((ROOT/'Before/editor_snapshot.json').read_text(encoding='utf-8'))
LAYOUT=json.loads((ROOT/'layout.json').read_text(encoding='utf-8'))

def budgets():
    overlap=np.empty((7,9))
    for i in range(7):
        for j in range(9):overlap[i,j]=max(0,min((i+1)/7,(j+1)/9)-max(i/7,j/9))*9
    data={}
    for kind,total in [('Blue',200),('Red',40)]:
        old=np.zeros((9,9),dtype=int)
        for m in BEFORE['layout']['markers']:
            p=m['parameters'];old[p['BoardRow']-1,p['BoardColumn']-1]=p[kind+'ClusterBudget']
        assert old.sum()==total
        quota=overlap@old@overlap.T;quota=(quota+quota[::-1,::-1])/2
        center=int(round(quota[3,3]/2)*2)
        out=np.zeros((7,7),dtype=int);out[3,3]=center
        pairs=[]
        for k in range(24):
            r,c=divmod(k,7);v=float(quota[r,c]);base=math.floor(v)
            pairs.append([v-base,k,base])
        remaining=(total-center)//2-sum(p[2] for p in pairs)
        assert 0<=remaining<=len(pairs)
        for p in sorted(pairs,key=lambda p:(-round(p[0],12),p[1]))[:remaining]:p[2]+=1
        for _,k,v in pairs:
            r,c=divmod(k,7);out[r,c]=out[6-r,6-c]=v
        assert out.sum()==total and np.array_equal(out,out[::-1,::-1])
        raw=out.copy()
        transfers=[]
        if kind=='Blue':
            # Four coast corners have no cluster centers outside the protected
            # pad/route geometry. Transfer their pairs to cells with measured room.
            for source,target in [(0,1),(0,12),(6,5),(6,13),(1,4),(1,4),(15,19),(15,19),(3,4),(2,10),(7,12),(14,19)]:
                sr,sc=divmod(source,7);tr,tc=divmod(target,7)
                out[sr,sc]-=1;out[6-sr,6-sc]-=1
                out[tr,tc]+=1;out[6-tr,6-tc]+=1
                transfers.append({'source_pair_indices':[source,48-source],
                    'target_pair_indices':[target,48-target],'clusters_per_cell':1,
                    'reason':'Protected routes, anchored pads and the complete cluster footprint limit usable land; target has measured geometric capacity.'})
            assert out.sum()==total and np.array_equal(out,out[::-1,::-1]) and out.min()>=0
        assert out.tolist()==LAYOUT[kind.lower()+'_budgets']
        data[kind]={'source_9x9':old.tolist(),'area_quota_7x7':quota.tolist(),'rounded_area_allocation_7x7':raw.tolist(),
            'land_capacity_transfers':transfers,'allocated_7x7':out.tolist(),'total':total}
    (ROOT/'budget_resampling.json').write_text(json.dumps(data,indent=2),encoding='utf-8')

def prepare():
    budgets()
    h=np.asarray(Image.open(FILES/'LandscapeInputs/height_4081_ue.png'),dtype=np.float32)/65535*140-20
    n=len(h);step=1800/(n-1);dy,dx=np.gradient(h,step);sl=np.rad2deg(np.arctan(np.hypot(dx,dy)))
    def samples(field,x,y):
        ix=np.rint((x+900)/1800*(n-1)).astype(int);iy=np.rint((y+900)/1800*(n-1)).astype(int)
        return field[iy,ix]
    old={m['marker_key']:m for m in BEFORE['layout']['markers']}
    patches=[]
    for p in LAYOUT['outposts']:
        m=old[p['name']];x,y=p['center_m'];z=float(samples(h,np.array(x),np.array(y)))*100
        regions=[]
        for q in m['regions']:
            r={'region_id':q['region_id'],'region_key':q['region_key'],'enabled':True}
            if q['region_key']=='Territory':
                half=90000/7;cx,cy=p['logical_center_m']
                r.update(position=[cx*100-x*100,cy*100-y*100,-z],rotation_pitch_yaw_roll=[0,0,0],
                    shape_type='PolygonPrism',geometry={'vertices':[[-half,-half],[half,-half],[half,half],[-half,half]],'min_z':-50000,'max_z':50000})
            else:
                assert q['region_key']=='Capture'
                r.update(position=[0,0,0],rotation_pitch_yaw_roll=[0,0,0],shape_type='Cylinder',geometry=q['geometry'])
            regions.append(r)
        patches.append({'schema_version':1,'marker_id':m['marker_id'],'marker_key':p['name'],
            'display_name':f"R{p['row']}C{p['column']} 据点",'enabled':True,'position':[x*100,y*100,z],
            'rotation_pitch_yaw_roll':[0,0,0],'parameters':{'BoardRow':p['row'],'BoardColumn':p['column'],
                'InitialOwner':p['initial_owner'],'BlueClusterBudget':p['blue_cluster_budget'],'RedClusterBudget':p['red_cluster_budget']},'regions':regions})
    keep={p['marker_id'] for p in patches};removed=[m for m in old.values() if m['marker_id'] not in keep]
    assert len(keep)==49 and len(removed)==32
    (ROOT/'marker_migration.json').write_text(json.dumps({'patches':patches,'removed':removed,'layout_version':6},ensure_ascii=False,indent=2),encoding='utf-8')

    axis=(np.arange(-36,36)+.5)*25;x,y=np.meshgrid(axis,axis)
    allowed=np.zeros(x.shape,dtype=bool)
    # Retain a cell when it has a valid native subcell candidate. The native baker
    # checks precise authoring regions and anchor/army clearance for every point.
    for ox in (-6.25,0,6.25):
        for oy in (-6.25,0,6.25):
            px,py=x+ox,y+oy
            clear=np.ones(x.shape,dtype=bool)
            for route in LAYOUT['routes']:
                d=np.full(x.shape,np.inf)
                for a,b in zip(route['points'][:-1],route['points'][1:]):
                    vx,vy=b[0]-a[0],b[1]-a[1];t=np.clip(((px-a[0])*vx+(py-a[1])*vy)/(vx*vx+vy*vy),0,1)
                    d=np.minimum(d,np.hypot(px-a[0]-t*vx,py-a[1]-t*vy))
                clear &= d>route['minimum_width_m']/2+5.2
            for p in LAYOUT['outposts']:
                ax,ay=p['center_m'];clear &= np.hypot(px-ax,py-ay)>=65
            for slot in BEFORE['army_layout']['slots']:
                sx=slot['x']/100;sy=slot['y']/100+(-35 if slot['team']==1 else 35)
                clear &= np.hypot(px-sx,py-sy)>slot['radius_cm']/100+5.2+1.5
            for sign in (-1,1):clear &= np.hypot(px,py-sign*740)>20
            clear &= (samples(h,px,py)>.3)&(samples(sl,px,py)<=15)
            for angle in np.linspace(0,2*np.pi,32,endpoint=False):
                qx,qy=px+math.cos(angle)*5.2,py+math.sin(angle)*5.2
                clear &= (samples(h,qx,qy)>.3)&(samples(sl,qx,qy)<=15)
            allowed |= clear & clear[::-1,::-1]
    density=[]
    for layer in BEFORE['density']['layers']:
        original=np.zeros((72,72),dtype=np.uint8)
        for c in layer['cells']:original[c['cell_y']+36,c['cell_x']+36]=c['density_u8']
        # Keep the original relative field; soften obsolete 9x9 corridor holes.
        smoothed=np.asarray(Image.fromarray(original).filter(ImageFilter.GaussianBlur(3)),dtype=np.uint8)
        weights=np.rint((smoothed.astype(float)+smoothed[::-1,::-1])/2).astype(np.uint8)
        weights[~allowed]=0
        cells=[{'cell_x':int(cx-36),'cell_y':int(cy-36),'density_u8':int(weights[cy,cx])}
            for cy in range(72) for cx in range(72)]
        density.append({'schema_version':1,'layer_key':layer['layer_key'],'cells':cells})
    (ROOT/'density_migration.json').write_text(json.dumps({'density_map_id':BEFORE['density']['density_map_id'],
        'cell_size_cm':2500,'method':'Gaussian 75m smoothing of original weights, deterministic rotational pairing, actual road/anchor/army/sea exclusions','patches':density},indent=2),encoding='utf-8')

    # Conservative water tiles: every wet height vertex is covered by NavArea_Null.
    count=144;size=12.5;wet=np.zeros((count,count),dtype=bool)
    for yy in range(count):
        y0=int(math.floor(yy/count*(n-1)));y1=int(math.ceil((yy+1)/count*(n-1)))+1
        for xx in range(count):
            x0=int(math.floor(xx/count*(n-1)));x1=int(math.ceil((xx+1)/count*(n-1)))+1
            wet[yy,xx]=h[y0:y1,x0:x1].min()<=0
    active={};rects=[]
    for yy,row in enumerate(wet):
        runs=[];xx=0
        while xx<count:
            if not row[xx]:xx+=1;continue
            start=xx
            while xx<count and row[xx]:xx+=1
            runs.append((start,xx))
        current={}
        for run in runs:current[run]=(active[run][0],yy+1) if run in active else (yy,yy+1)
        for run,rows in active.items():
            if run not in current:rects.append((*run,*rows))
        active=current
    for run,rows in active.items():rects.append((*run,*rows))
    covered=np.zeros_like(wet);boxes=[]
    for x0,x1,y0,y1 in rects:
        covered[y0:y1,x0:x1]=True
        boxes.append({'center_cm':[(-900+(x0+x1)*size/2)*100,(-900+(y0+y1)*size/2)*100,6000],
            'extent_cm':[(x1-x0)*size*50,(y1-y0)*size*50,9000]})
    assert np.array_equal(covered,wet)
    assert all(not wet[min(143,int((p['center_m'][1]+900)/size)),min(143,int((p['center_m'][0]+900)/size))] for p in LAYOUT['outposts'])
    (FILES/'water_navigation_boxes.json').write_text(json.dumps({'tile_size_m':size,'water_cells_covered':True,
        'outpost_cells_clear':True,'water_cells':int(wet.sum()),'boxes':boxes},indent=2),encoding='utf-8')
    print(json.dumps({'success':True,'markers_kept':49,'markers_removed':32,'water_boxes':len(boxes),
        'positive_density_cells':{p['layer_key']:sum(c['density_u8']>0 for c in p['cells']) for p in density}}))

if __name__=='__main__':prepare()
