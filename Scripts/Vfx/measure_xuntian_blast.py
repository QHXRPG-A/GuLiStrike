"""Numerical calibration of Xuntian's three main explosion layers. No renderer or image capture.

Call capture(label) in idle Unreal Editor, then run this file with the same label
using the project Python environment (NumPy). Intermediate particle arrays live in the OS temp folder.
"""
from array import array
import json
import math
from pathlib import Path
import statistics
import tempfile

SCRATCH = Path(tempfile.gettempdir())/'GuLiXuntianBlastGeometry'
ROOT = Path(__file__).resolve().parents[2]
SYSTEMS = (
    '/Game/Stylized_Explosion_Pack_Vol1/VFX/N_Stylized_Explosion_1A_Classic_Explosion',
    '/Game/_VFXResources/Niagara_System/NS_Explosion_04',
    '/Game/_VFXResources/Niagara_System/NS_Explosion_07',
)
PRIMARY = (
    ('Explosion_Flipbook',),
    ('NE_Cloud_01', 'NE_Cloud_02'),
    ('NS_Cloud03', 'NE_Cloud_01', 'NE_Cloud_02', 'NE_Cloud_01B'),
)
MESHES = ('SM_CloudSphere_03','SM_CloudSphere_04','SM_CloudSphere_02')
HZ = 30
# The primary envelope is measured during the first expansion. Late smoke advection is reported separately.
EXPANSION = .5


def capture(label, scales=(1, 1, 1), runs=8, from_fields=True, frames=24):
    import unreal
    assert not unreal.WidgetService.is_pie_running()
    assert runs > 0 and len(scales) == len(SYSTEMS) and frames >= HZ * EXPANSION
    SCRATCH.mkdir(parents=True,exist_ok=True)
    for name in MESHES:
        mesh=unreal.load_asset('/Game/_VFXResources/Static_Mesh/'+name)
        vertices=set()
        for section in range(mesh.get_num_sections(0)):
            pts=unreal.ProceduralMeshLibrary.get_section_from_static_mesh(mesh,0,section)[0]
            vertices.update(p.to_tuple() for p in pts)
        with (SCRATCH/(name+'.f32')).open('wb') as out:
            array('f',(v for p in sorted(vertices) for v in p)).tofile(out)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    cache_lib=unreal.NiagaraSimCacheFunctionLibrary
    params=unreal.NiagaraSimCacheCreateParameters()
    params.set_editor_property('attribute_capture_mode',unreal.NiagaraSimCacheAttributeCaptureMode.ALL)
    # Each sample stores position, sprite/mesh size, velocity, orientation, mesh index, and opacity.
    meta={'label':label,'runs':runs,'fps':HZ,'frames':frames,'scale':[],'fields':[],'stride':15,'slices':[]}
    blank_rot=unreal.Quat(0,0,0,1)
    registry = json.loads((ROOT/'Data/Json/DT_GuLiStrikeVfx_Effects.json').read_text(encoding='utf8'))
    with (SCRATCH/(label+'.f32')).open('wb') as output:
        offset=0
        for tier,(path,names,scale) in enumerate(zip(SYSTEMS,PRIMARY,scales),1):
            impact=unreal.load_asset('/Game/GuLiStrike/FX/Xuntian/Belly/DA_XuntianBellyImpact'+str(tier))
            var=impact.activation_variants[0]
            row = next(row for row in registry if row['Id'] == var.vfx_id)
            assert row['ResourcePath'].split('.', 1)[0] == path and set(row['Scale'].values()) == {1}, row
            asset = unreal.load_asset(row['ResourcePath'])
            parameter=str(var.scale_parameter_name) if from_fields else 'User.Area_Scale'
            scale=float(impact.radius)/impact.visual_reference_radius if from_fields else scale
            component_scale=scale if parameter=='None' else 1
            meta['fields'].append({'radius_cm':impact.radius,'visual_reference_cm':impact.visual_reference_radius,
                                   'vfx_id':var.vfx_id,'scale_parameter':parameter,'system':path})
            meta['scale'].append(scale)
            for run in range(runs):
                comp=unreal.NiagaraFunctionLibrary.spawn_system_at_location(world,asset,unreal.Vector(0,0,18000),
                    scale=unreal.Vector(component_scale,component_scale,component_scale),auto_destroy=False,auto_activate=False,pre_cull_check=False)
                try:
                    comp.set_visibility(False)
                    comp.set_random_seed_offset(3707+run*43)
                    if parameter!='None': comp.set_variable_float(parameter,scale)
                    comp.activate(True)
                    for frame in range(1,frames+1):
                        comp.advance_simulation(2,1/(2*HZ))
                        if not comp.is_active(): break
                        cache=cache_lib.create_niagara_sim_cache(world)
                        if not cache_lib.capture_niagara_sim_cache_immediate(cache,params,comp,False): continue
                        for name in names:
                            positions=cache.read_position_attribute('Position',name,False)
                            count=len(positions)
                            if not count: continue
                            data=array('f')
                            if tier==1:
                                sizes=cache.read_vector2_attribute('SpriteSize',name)
                                assert len(sizes)==count
                                for pos,size in zip(positions,sizes):
                                    data.extend((pos.x*component_scale, pos.y*component_scale, pos.z*component_scale,
                                                 size.x, size.y, 0, 0, 0, 0, 0, 0, 0, 1, -1, 1))
                            else:
                                sizes=cache.read_vector_attribute('Scale',name)
                                velocities=cache.read_vector_attribute('Velocity',name)
                                orientations=cache.read_quat_attribute('MeshOrientation',name,False)
                                mesh_ids=cache.read_int_attribute('MeshIndex',name)
                                colors=cache.read_color_attribute('Color',name)
                                assert len(sizes)==len(velocities)==count
                                for i,(pos,size,vel) in enumerate(zip(positions,sizes,velocities)):
                                    q=orientations[i] if len(orientations) else blank_rot
                                    color=colors[i] if len(colors) else None
                                    mesh_index=mesh_ids[i] if len(mesh_ids) else 0
                                    assert 0<=mesh_index<len(MESHES)
                                    data.extend((pos.x*component_scale,pos.y*component_scale,pos.z*component_scale,
                                        size.x*component_scale,size.y*component_scale,size.z*component_scale,vel.x,vel.y,vel.z,
                                        q.x,q.y,q.z,q.w,mesh_index,color.a if color else 1))
                            data.tofile(output)
                            meta['slices'].append([tier,run,frame,name,offset,count])
                            offset+=count
                finally:
                    comp.deactivate()
                    comp.destroy_component(comp.get_owner() or comp)
    (SCRATCH/(label+'.json')).write_text(json.dumps(meta),encoding='utf8')
    return {'label':label,'particles_sampled':offset,'runs':runs,'scale':meta['scale']}


def _cloud_edge(rows, meshes, headings):
    """Support of actual LOD0 vertices in 16 horizontal directions after the velocity-facing renderer transform."""
    import numpy as np
    rows=rows[rows[:,14]>.05]
    if not len(rows): return np.zeros(len(headings))
    def normalize(v):
        return v/np.maximum(np.linalg.norm(v,axis=-1,keepdims=True),1.e-12)
    forward=normalize(rows[:,6:9])
    stalled=np.linalg.norm(forward,axis=1)<.01
    forward[stalled]=(0,0,1)
    up=np.tile([0.,0.,1.],(len(rows),1))
    poles=np.abs(forward[:,2])>.99
    up[poles]=np.column_stack((-np.sign(forward[poles,2]),np.zeros((int(poles.sum()),2))))
    right=normalize(np.cross(up,forward))
    basis=np.stack((forward,right,np.cross(forward,right)),axis=-1)
    # The renderer first applies mesh scale and particle orientation, then faces velocity.
    q=rows[:,9:13].copy()
    q[np.linalg.norm(q,axis=1)<1.e-8]=[0,0,0,1]
    q=normalize(q)
    directions=np.einsum('nij,hi->nhj',basis,headings)
    v=-q[:,None,:3]
    t=2*np.cross(v,directions)
    directions=(directions+q[:,None,3:4]*t+np.cross(v,t))*rows[:,None,3:6]
    centers=rows[:,:3]@headings.T
    which=rows[:,13].astype(int)
    radii=np.array([m[1] for m in meshes])[which]
    upper=centers+np.linalg.norm(directions,axis=-1)*radii[:,None]
    edges=[]
    for h in range(len(headings)):
        best=0.
        for i in np.argsort(upper[:,h])[::-1]:
            if upper[i,h]<=best: break
            verts=meshes[which[i]][0]
            best=max(best,centers[i,h]+float(np.max(verts@directions[i,h])))
        edges.append(best)
    return np.array(edges)


def analyze(label):
    import numpy as np
    meta=json.loads((SCRATCH/(label+'.json')).read_text(encoding='utf8'))
    rows=np.memmap(SCRATCH/(label+'.f32'),dtype='float32',mode='r').reshape(-1,meta['stride'])
    meshes=[]
    for name in MESHES:
        v=np.fromfile(SCRATCH/(name+'.f32'),dtype='float32').reshape(-1,3).astype('float64')
        meshes.append((v,float(np.linalg.norm(v,axis=1).max())))
    angles=2*math.pi*np.arange(16)/16
    headings=np.array([np.cos(angles),np.sin(angles),np.zeros(16)]).T
    slices={}
    for tier,run,frame,name,start,n in meta['slices']:
        slices.setdefault((tier,run,frame),[]).append(rows[start:start+n])
    shapes={}
    for (tier,run,frame),pieces in slices.items():
        data=np.concatenate(pieces)
        if tier==1:
            radius=float(np.median(data[:,3:5]))*.5
            edge=np.full(16,radius)
        else:
            edge=_cloud_edge(data,meshes,headings)
        shapes[(tier,run,frame)]=[round(float(np.median(edge)),3),round(float(max(edge)),3)]
    tiers=[]
    for tier in (1,2,3):
        peaks=[]
        for run in range(meta['runs']):
            samples=[(f/HZ,v) for (t,r,f),v in shapes.items() if t==tier and r==run and f/HZ<=EXPANSION]
            assert samples
            time,edge=max(samples,key=lambda s:s[1][0])
            peaks.append({'time_s':round(time,3),'radial_median_cm':edge[0],'farthest_heading_cm':edge[1]})
        tiers.append({**meta['fields'][tier-1], 'scale':meta['scale'][tier-1], 'runs':peaks,
            'median_peak_cm':round(statistics.median(p['radial_median_cm'] for p in peaks),2),
            'max_heading_cm':max(p['farthest_heading_cm'] for p in peaks),
            'first_run_time_series':[(round(f/HZ,3),v) for (t,r,f),v in shapes.items() if t==tier and r==0]})
    return {'label':label,'runs':meta['runs'],'time_step_s':1/HZ,'first_expansion_s':EXPANSION,
        'metric':'Tier 1 half width of camera-facing flipbook. Tiers 2 and 3 median of 16 horizontal supports of the main cloud meshes at maximum first expansion, then median across bursts. Excludes secondary sparks, ground decals and late drift. Geometric footprint, not texture alpha.',
        'tiers':tiers}


if __name__=='__main__':
    import sys
    summary=analyze(sys.argv[1])
    target=ROOT/'TestResults/XuntianBelly'/('blast-radius-'+sys.argv[1]+'.json')
    target.parent.mkdir(parents=True, exist_ok=True)
    if sys.argv[1]=='calibrated':
        for t in summary['tiers']:
            t['relative_radius_error'] = round(t['median_peak_cm']/t['radius_cm'] - 1, 4)
        summary['within_five_percent'] = all(abs(t['relative_radius_error']) <= .05 for t in summary['tiers'])
    target.write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    assert summary.get('within_five_percent', True), 'Blast geometry outside calibration tolerance'
    print([(t['median_peak_cm'], t['max_heading_cm']) for t in summary['tiers']])
