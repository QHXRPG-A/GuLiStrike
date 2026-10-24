"""Create the reviewable, idempotent Grid300 map migration payload from the captured world.

This program writes ordinary JSON only. The editor application step verifies these input
hashes and preserves the original packages before making any asset changes.
"""
import copy
import hashlib
import json
import math
from collections import defaultdict
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'ArtSource/Navigation/Grid300/Baseline'
OUT=ROOT/'ArtSource/Navigation/Grid300'
SCALE=.945
HALF=85050.
CELL=300.
ORE_SCALE=900/1040

def read(path):return json.loads(path.read_text(encoding='utf-8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def save(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

def align_blocked_deployment(actors,dimensions):
    # Preserve the existing enclosed QA setup when its unit moves to a grid centre.
    # Snapping only the unit can put its full square footprint through a wall.
    by_name={a['path'].rsplit('.',1)[-1]:a for a in actors}
    point=by_name['GuLiCommanderDeploymentPoint_4']
    size=next(d['footprint_cells'] for d in dimensions['units'] if d['id']==1)
    position=point['target']['location']
    centre=[(math.floor((position[i]+HALF)/CELL-size/2+.5)+size/2)*CELL-HALF for i in (0,1)]
    delta=[centre[i]-position[i] for i in (0,1)]
    changed=[]
    for name in ('GuLiCommanderDeploymentPoint_4','StaticMeshActor_38','StaticMeshActor_39','StaticMeshActor_40','StaticMeshActor_41'):
        row=by_name[name]
        before=copy.deepcopy(row['target'])
        row['target']['location'][:2]=[before['location'][i]+delta[i] for i in (0,1)]
        if before!=row['target']:
            changed.append({'kind':'deployment_grid_alignment','identity':row['path'],
                            'previous_target':before,'target':copy.deepcopy(row['target'])})
    return changed

class Terrain:
    def __init__(self):
        self.height=np.fromfile(OUT/'Terrain/heights_cm.f32',dtype='<f4').reshape(568,568)
        self.clearance=np.fromfile(OUT/'Terrain/clearance.i32',dtype='<i4').reshape(567,567)
    def anchor(self,p,n):return tuple(math.floor((p[i]+HALF)/CELL-n/2+.5) for i in (0,1))
    def center(self,a,n):return [(a[i]+n/2)*CELL-HALF for i in (0,1)]
    def fits(self,p,n):
        x,y=self.anchor(p,n)
        return 0<=x<567 and 0<=y<567 and int(self.clearance[y,x])>=n
    def level_pad(self,p,radius=1500.,degrees=3.):
        limit=math.tan(math.radians(degrees))
        for dx in (-radius,0.,radius):
            for dy in (-radius,0.,radius):
                x,y=(p[0]+dx+HALF)/CELL,(p[1]+dy+HALF)/CELL
                ix,iy=math.floor(x),math.floor(y)
                if not (0<=ix<567 and 0<=iy<567):return False
                a,b,c,d=map(float,(self.height[iy,ix],self.height[iy,ix+1],self.height[iy+1,ix],self.height[iy+1,ix+1]))
                sx,sy=(b-a,d-b) if x-ix>=y-iy else (d-c,c-a)
                if self.z([p[0]+dx,p[1]+dy])<=0 or math.hypot(sx,sy)/CELL>limit:return False
        return True
    def z(self,p):
        x,y=[min(567,max(0,(v+HALF)/CELL)) for v in p[:2]]
        ix,iy=min(566,int(x)),min(566,int(y));fx,fy=x-ix,y-iy
        h00,h10,h01,h11=map(float,(self.height[iy,ix],self.height[iy,ix+1],self.height[iy+1,ix],self.height[iy+1,ix+1]))
        return h00+(h10-h00)*fx+(h11-h10)*fy if fx>=fy else h00+(h11-h01)*fx+(h01-h00)*fy
    def candidates(self,p,n,radius=12):
        x,y=self.anchor(p,n)
        cells=[self.center((x+dx,y+dy),n) for dy in range(-radius,radius+1) for dx in range(-radius,radius+1)]
        return sorted(cells,key=lambda c:((c[0]-p[0])**2+(c[1]-p[1])**2,c[1],c[0]))

def road_contains(regions,p):
    for r in regions:
        t=r['world_transform'];q=[p[i]-t['position'][i] for i in (0,1)]
        a=math.radians(-t['rotation_pitch_yaw_roll'][1]);c,s=math.cos(a),math.sin(a)
        x,y=q[0]*c-q[1]*s,q[0]*s+q[1]*c;g=r['geometry']
        if r['shape_type']=='Cylinder' and x*x+y*y<=g['radius']**2:return True
        if r['shape_type']=='Box' and abs(x)<=g['half_extents'][0] and abs(y)<=g['half_extents'][1]:return True
    return False

def scaled_markers(original,territories):
    result=copy.deepcopy(original);by_id={t['territory_id']:t for t in territories}
    for m in result['markers']:
        before=copy.deepcopy(m['world_transform'])
        p=m['world_transform']['position'];p[0]*=SCALE;p[1]*=SCALE
        territory=by_id.get(m['marker_key'])
        if territory:m['world_transform']['position']=territory['outpost_ground_location'][:]
        for r in m['regions']:
            g=r['geometry'];w=r['world_transform'];w['position'][0]*=SCALE;w['position'][1]*=SCALE
            if 'radius' in g:g['radius']*=SCALE
            if 'half_extents' in g:g['half_extents'][:2]=[v*SCALE for v in g['half_extents'][:2]]
            if 'vertices' in g:g['vertices']=[[v*SCALE for v in point] for point in g['vertices']]
            if territory:
                if r['region_key']=='Territory':
                    w['position']=territory['center'][:];g['vertices']=copy.deepcopy(territory['local_polygon'])
                elif r['region_key']=='Capture':w['position']=territory['outpost_ground_location'][:]
            # All existing markers are horizontal and unscaled; preserve yaw-relative coordinates.
            assert before['scale']==[1,1,1] and abs(before['rotation_pitch_yaw_roll'][0])<1e-6 and abs(before['rotation_pitch_yaw_roll'][2])<1e-6
            diff=[w['position'][i]-m['world_transform']['position'][i] for i in range(3)]
            a=math.radians(-m['world_transform']['rotation_pitch_yaw_roll'][1]);c,s=math.cos(a),math.sin(a)
            r['relative_transform']['position']=[diff[0]*c-diff[1]*s,diff[0]*s+diff[1]*c,diff[2]]
    return result

def density(original):
    result=copy.deepcopy(original);cell=result['cell_size_cm'];statistics=[]
    for old,new in zip(original['layers'],result['layers']):
        sums=defaultdict(float)
        for v in old['cells']:
            x0,y0=v['cell_x']*cell*SCALE,v['cell_y']*cell*SCALE
            x1,y1=(v['cell_x']+1)*cell*SCALE,(v['cell_y']+1)*cell*SCALE
            for y in range(math.floor(y0/cell),math.ceil(y1/cell)):
                for x in range(math.floor(x0/cell),math.ceil(x1/cell)):
                    area=max(0,min(x1,(x+1)*cell)-max(x0,x*cell))*max(0,min(y1,(y+1)*cell)-max(y0,y*cell))
                    sums[x,y]+=area/(cell*cell)*v['density_u8']
        new['cells']=[{'cell_x':x,'cell_y':y,'density_u8':min(255,math.floor(v+.5+1e-9))}
                      for (x,y),v in sorted(sums.items(),key=lambda kv:(kv[0][1],kv[0][0])) if v>=.5-1e-9]
        statistics.append({'layer':new['layer_key'],'old_cells':len(old['cells']),'new_cells':len(new['cells']),
                           'old_integral':sum(v['density_u8'] for v in old['cells']),
                           'target_integral_before_rounding':sum(sums.values()),'new_integral':sum(v['density_u8'] for v in new['cells'])})
    result.pop('territory_summary',None);result.pop('anomalies',None)
    return result,statistics

def main():
    editor=read(BASE/'editor.json');ids=read(BASE/'definitions.json');grid=Terrain()
    source=editor['assets']['/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap'];resource=copy.deepcopy(source)
    dimensions=read(OUT/'dimensions.json');changes=[];occupied=[]
    resource.update(layout_version=7,playable_minimum=[-HALF,-HALF],playable_maximum=[HALF,HALF],baked_grid_cell_cm=300.)
    for t,old in zip(resource['territories'],source['territories']):
        r,c=t['board_row'],t['board_column'];center=[(c-4)*24300.,(4-r)*24300.,0.]
        t['center']=center;t['local_polygon']=[[-12150,-12150],[12150,-12150],[12150,12150],[-12150,12150]]
        desired=[v*SCALE for v in old['outpost_ground_location'][:2]]
        p=next((p for p in grid.candidates(desired,2,20) if grid.fits(p,2) and grid.level_pad(p)
                and abs(p[0]-center[0])<11850 and abs(p[1]-center[1])<11850),None)
        if p is None:raise RuntimeError('No full outpost footprint for '+t['territory_id'])
        t['outpost_ground_location']=[*p,grid.z(p)];occupied.append((p,2))
        changes.append({'kind':'territory','identity':t['territory_id'],'source':old,'target':copy.deepcopy(t)})
    for key,p in {'red_factory':[150.,70050.],'blue_factory':[-150.,-70050.],
                  'red_assembly':[0.,65100.],'blue_assembly':[0.,-65100.]}.items():
        n=4 if 'factory' in key else 1
        if not grid.fits(p,n):raise RuntimeError('Invalid fixed anchor '+key)
        resource['spawn_anchors'][key]=[*p,grid.z(p)]
        if n==4:occupied.append((p,n))
    markers=scaled_markers(json.loads(editor['authoring']['json']),resource['territories'])
    roads=[r for m in markers['markers'] if m['marker_key']=='CommanderIsland_Roads' for r in m['regions'] if r['enabled']]
    pairs={tuple(round(v,2) for v in c['center'][:2]):i for i,c in enumerate(source['clusters'])}
    placed={};minimum=5100.
    def legal(p,index):
        c=source['clusters'][index];center=resource['territories'][c['territory_index']]['center']
        return (grid.fits(p,3) and max(abs(p[i]-center[i]) for i in (0,1))<=12150-1200
                and not road_contains(roads,p)
                and all(max(abs(p[i]-other[i]) for i in (0,1))>=(3+n)*150 for other,n in occupied)
                and all(math.dist(p,other)>=minimum for other in placed.values()))
    for i,c in enumerate(source['clusters']):
        if i in placed:continue
        j=pairs.get(tuple(round(-v,2) for v in c['center'][:2]));assert j is not None and i!=j
        assert source['clusters'][j]['territory_index']==48-c['territory_index']
        assert source['clusters'][j]['resource_type']==c['resource_type']
        desired=[v*SCALE for v in c['center'][:2]]
        p=next((p for p in grid.candidates(desired,3,20) if legal(p,i) and legal([-v for v in p],j)
                and math.dist(p,[-v for v in p])>=minimum),None)
        if p is None:raise RuntimeError(f'No symmetric ore placement for clusters {i+1}, {j+1}')
        placed[i]=p;placed[j]=[-v for v in p]
    for i,(c,old,identity) in enumerate(zip(resource['clusters'],source['clusters'],ids['clusters'])):
        c.update(identity);c['center']=[*placed[i],grid.z(placed[i])];c['obstacle_radius_centimeters']=450.
        c['footprint_cells']=3;c['source_collision_diameter_cm']=old['obstacle_radius_centimeters']*2
        changes.append({'kind':'cluster','identity':identity['cluster_id'],'source_center':old['center'],
                        'target_center':c['center'],'source_radius':old['obstacle_radius_centimeters'],'target_radius':450.,
                        'internal_scale':ORE_SCALE})
    for node,old,identity in zip(resource['nodes'],source['nodes'],ids['nodes']):
        node.update(identity);i=identity['cluster_id']-1;src=source['clusters'][i]['center'];dst=resource['clusters'][i]['center']
        node['world_transform']['location']=[dst[a]+(old['world_transform']['location'][a]-src[a])*ORE_SCALE for a in range(3)]
        node['world_transform']['scale']=[v*ORE_SCALE for v in old['world_transform']['scale']]
    actors=[]
    dimensions=read(OUT/'dimensions.json')
    demo_sizes={'BP_ResourceProcessingFactory_C':next(d for d in dimensions['buildings'] if d['id']==6),
                'BP_ConstructionVehicle_3Tier_C':next(d for d in dimensions['units'] if d['id']==4)}
    for a in editor['actors']:
        cls=a['class']
        if cls in ('Landscape','GuLiMapMarker','GuLiMapDensityMap','RecastNavMesh'):continue
        target=copy.deepcopy(a['transform']);target['location'][:2]=[v*SCALE for v in target['location'][:2]]
        if cls in ('StaticMeshActor','NavModifierVolume','NavMeshBoundsVolume','GuLiFlightNavigationVolume','InstancedFoliageActor'):
            target['scale'][:2]=[v*SCALE for v in target['scale'][:2]]
        if cls in demo_sizes:
            d=demo_sizes[cls];target['scale']=[v*d['scale'] for v in a['transform']['scale']]
            p=grid.center(grid.anchor(target['location'],d['footprint_cells']),d['footprint_cells'])
            target['location']=[*p,grid.z(p)]
        actors.append({'path':a['path'],'class':cls,'source':a['transform'],'target':target})
    changes.extend(align_blocked_deployment(actors,dimensions))
    dens,stats=density(json.loads(editor['density']['json']))
    save(OUT/'layout_resource.json',resource);save(OUT/'layout_markers.json',markers);save(OUT/'layout_density.json',dens)
    save(OUT/'layout_actors.json',actors)
    minimum_actual=min(math.dist(a,b) for i,a in placed.items() for j,b in placed.items() if i<j)
    report={'schema':'guli-grid300/layout-v1','map':editor['map'],'layout_scale_xy':SCALE,'origin_cm':[-HALF,-HALF],
            'source_hashes':{p.name:sha(p) for p in [BASE/'editor.json',BASE/'definitions.json',BASE/'foliage.json',OUT/'dimensions.json',OUT/'Terrain/manifest.json']},
            'output_hashes':{p.name:sha(p) for p in [OUT/'layout_resource.json',OUT/'layout_markers.json',OUT/'layout_density.json',OUT/'layout_actors.json']},
            'invariants':{'territories':len(resource['territories']),'clusters':len(resource['clusters']),
                          'nodes':len(resource['nodes']),'total_ore':sum(n['initial_amount'] for n in resource['nodes']),
                          'minimum_cluster_spacing_cm':minimum_actual,'required_cluster_spacing_cm':minimum,
                          'density_actor_cell_cm':2500,'density_identity_preserved':True},
            'density_resampling':stats,'changes':changes}
    assert report['invariants']['total_ore']==9600 and len(resource['nodes'])==6240 and len(resource['clusters'])==240
    save(OUT/'layout_manifest.json',report)
    print(json.dumps(report['invariants'],ensure_ascii=False))

if __name__=='__main__':main()
