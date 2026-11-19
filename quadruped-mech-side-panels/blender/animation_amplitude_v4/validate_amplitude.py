import bpy,ast,json,hashlib,math,numpy as np
from pathlib import Path
from mathutils import Vector

ROOT=Path(__file__).resolve().parent
REPORT=json.loads((ROOT/'amplitude-report.json').read_text(encoding='utf-8'))
SPECS=json.loads((ROOT/'motion-specs.json').read_text(encoding='utf-8'))
base=ast.parse((ROOT.parent/'reduction_10k_v3'/'validate_reduced_asset.py').read_text(encoding='utf-8'))
helpers={}
exec(compile(ast.Module(body=[n for n in base.body if isinstance(n,ast.FunctionDef) and n.name in {'value','material_fingerprint','curves','action_fingerprint'}],type_ignores=[]),'validation_helpers','exec'),helpers)

def coords(data,matrix):
    values=np.empty(len(data)*3,dtype=np.float32);data.foreach_get('co',values);m=np.array(matrix)
    return values.reshape(-1,3)@m[:3,:3].T+m[:3,3]

def fit(rest,current):
    a=rest.mean(axis=0);b=current.mean(axis=0);u,s,v=np.linalg.svd((rest-a).T@(current-b));r=u@v
    if np.linalg.det(r)<0:u[:,-1]*=-1;r=u@v
    m=np.eye(4);m[:3,:3]=r.T;m[:3,3]=b-a@r
    return m

def point(p,m):return np.asarray(p)@m[:3,:3].T+m[:3,3]
def topology(mesh):return [tuple(p.vertices) for p in mesh.polygons]

def snapshot():
    materials={m.name:helpers['material_fingerprint'](m) for m in bpy.data.materials}
    objects={};scenes={};static={};poses={}
    for scene in bpy.data.scenes:
        bpy.context.window.scene=scene;bpy.context.view_layer.update();scenes[scene.name]=(scene.frame_start,scene.frame_end,scene.render.fps)
        for obj in scene.objects:
            objects[obj.name]={'parent':obj.parent.name if obj.parent else None,'matrix':np.asarray(obj.matrix_world).tolist(),'materials':[s.material.name if s.material else None for s in obj.material_slots]}
            if obj.type!='MESH':continue
            objects[obj.name]['topology']=topology(obj.data)
            if scene.name.startswith('00') and not obj.get('is_lineart'):static[obj.name]=coords(obj.data.vertices,obj.matrix_world)
            keys=obj.data.shape_keys
            if keys:
                objects[obj.name]['key_layout']=[(k.name,k.frame,k.interpolation) for k in keys.key_blocks]
                objects[obj.name]['action']=helpers['action_fingerprint'](keys)
                if not obj.get('is_lineart'):poses[obj.name]=[coords(k.data,obj.matrix_world) for k in keys.key_blocks]
    return materials,objects,scenes,static,poses

assert Path(bpy.data.filepath)==Path(REPORT['source'])
old_materials,old_objects,old_scenes,static,old_poses=snapshot()
foot_names={};foot_ids={};body_name='Body_Chassis · 机甲 · 黑色主体框架'
static_scene=next(s for s in bpy.data.scenes if s.name.startswith('00'))
body_obj=static_scene.objects[body_name]
body_ids=np.flatnonzero(np.array([a.value for a in body_obj.data.attributes['source_component'].data])==0)
for leg in ('Leg_Front_L','Leg_Front_R','Leg_Rear_L','Leg_Rear_R'):
    o=next(o for o in static_scene.objects if o.name.startswith(leg) and '深灰' in o.name)
    foot_names[leg]=o.name;foot_ids[leg]=np.flatnonzero(np.array([a.value for a in o.data.attributes['source_component'].data])==0)
rest_ankles={leg:np.array(((-1 if leg.endswith('_L') else 1)*(1.55 if 'Front' in leg else 1.98),-2.06 if 'Front' in leg else 1.96,.25)) for leg in foot_names}

bpy.ops.wm.open_mainfile(filepath=REPORT['output'],load_ui=False)
new_materials,new_objects,new_scenes,new_static,new_poses=snapshot()
result={'checks':{},'scenes':{},'measurements':{}}
checks=result['checks'];measure=result['measurements']
checks['materials_and_colors_unchanged']=old_materials==new_materials
checks['object_hierarchy_transforms_topology_key_timing_unchanged']=old_objects==new_objects and old_scenes==new_scenes
checks['static_model_geometry_unchanged']=all(np.array_equal(p,new_static[n]) for n,p in static.items())
checks['v3_source_file_unchanged']=hashlib.sha256(Path(REPORT['source']).read_bytes()).hexdigest()==REPORT['source_sha256']
max_body_error=0.;max_lift_error=0.;min_z=100.;max_rigid_error=0.;max_lamp_error=0.;max_loop_error=0.;max_laser_error=0.;max_laser_radius_error=0.;max_mining_foot_drift=0.;outline_failures=[];degenerate=0;nonfinite=0
sampled_lift_old=0.;sampled_lift_new=0.
for scene in bpy.data.scenes:
    bpy.context.window.scene=scene;bpy.context.view_layer.update()
    if scene.name.startswith('00'):continue
    clip='Walk' if scene.name.startswith('01') else 'Mining'
    body=next(o for o in scene.objects if not o.get('is_lineart') and o.get('asset_source_name')==body_name)
    body_matrices=[fit(static[body_name][body_ids],p[body_ids]) for p in new_poses[body.name]]
    old_body_matrices=[fit(static[body_name][body_ids],p[body_ids]) for p in old_poses[body.name]]
    for a,b in zip(old_body_matrices,body_matrices):
        pivot=np.array((0.,0.,1.55));old_offset=point(pivot,a)-pivot;new_offset=point(pivot,b)-pivot
        if clip=='Walk':expected=old_offset*3+np.array((0.,0.,-.24))
        else:expected=old_offset*2
        max_body_error=max(max_body_error,float(np.max(np.abs(new_offset-expected))))
    for obj in scene.objects:
        if obj.type!='MESH' or obj.get('is_lineart') or obj.name not in new_poses:continue
        name=obj.get('asset_source_name',obj.name);poses=new_poses[obj.name]
        max_loop_error=max(max_loop_error,float(np.max(np.linalg.norm(poses[0]-poses[-1],axis=1))))
        nonfinite+=sum(not np.isfinite(p).all() for p in poses)
        if 'Laser' not in obj.name:min_z=min(min_z,min(float(p[:,2].min()) for p in poses))
        if name in SPECS:
            labels=np.array([a.value for a in obj.data.attributes['source_component'].data])
            for part in SPECS[name]:
                if part['span'] is not None:continue
                ids=np.flatnonzero(labels==part['component']);ref=static[name][ids]
                anchors=sorted(set(int(f(ref[:,a])) for a in range(3) for f in (np.argmin,np.argmax)))
                distances=np.linalg.norm(ref[:,None,:]-ref[anchors][None,:,:],axis=2)
                for pose in poses:
                    p=pose[ids];error=float(np.max(np.abs(np.linalg.norm(p[:,None,:]-p[anchors][None,:,:],axis=2)-distances)))
                    max_rigid_error=max(max_rigid_error,error)
        if obj.get('is_team_light'):
            parent=obj.parent;labels=np.array([a.value for a in parent.data.attributes['source_component'].data]);ids=np.flatnonzero(labels==1)
            ref=static[parent['asset_source_name']][ids];lamp=static[name]
            base_distance=np.linalg.norm(ref[::max(1,len(ref)//6),None,:]-lamp[None,:,:],axis=2)
            for hp,lp in zip(new_poses[parent.name],poses):
                d=np.linalg.norm(hp[ids][::max(1,len(ref)//6),None,:]-lp[None,:,:],axis=2)
                max_lamp_error=max(max_lamp_error,float(np.max(np.abs(d-base_distance))))
        if 'Laser' in obj.name:
            for i,p in enumerate(poses):
                old=old_poses[obj.name][i];start=p[:16].mean(axis=0);end=p[16:].mean(axis=0)
                expected=point((0.,-2.055,1.712),body_matrices[i]);old_radius=np.linalg.norm(old[0]-old[:16].mean(axis=0));radius=np.linalg.norm(p[0]-start)
                max_laser_error=max(max_laser_error,float(np.linalg.norm(start-expected)),float(np.linalg.norm(end-(0.,-6.5,.15))))
                max_laser_radius_error=max(max_laser_radius_error,abs(float(old_radius-radius)))
    for leg,name in foot_names.items():
        o=next(o for o in scene.objects if o.get('asset_source_name')==name and not o.get('is_lineart'))
        ids=foot_ids[leg];ref=static[name][ids]
        if clip=='Walk':
            for before,after in zip(old_poses[o.name],new_poses[o.name]):
                ankle_before=point(rest_ankles[leg],fit(ref,before[ids]));ankle_after=point(rest_ankles[leg],fit(ref,after[ids]))
                old_lift=ankle_before[2]-.25;new_lift=ankle_after[2]-.25
                max_lift_error=max(max_lift_error,abs(new_lift-3*old_lift));sampled_lift_old=max(sampled_lift_old,old_lift);sampled_lift_new=max(sampled_lift_new,new_lift)
        else:
            reference=new_poses[o.name][0][ids]
            max_mining_foot_drift=max(max_mining_foot_drift,max(float(np.max(np.linalg.norm(p[ids]-reference,axis=1))) for p in new_poses[o.name]))
    all_counts=[]
    for frame in range(scene.frame_start,scene.frame_end+1):
        scene.frame_set(frame);dg=bpy.context.evaluated_depsgraph_get();count=0
        for obj in scene.objects:
            if obj.type!='MESH':continue
            ev=obj.evaluated_get(dg);mesh=ev.to_mesh();mesh.calc_loop_triangles();count+=len(mesh.loop_triangles)
            if obj.get('is_lineart'):
                source_ids=[a.value for a in mesh.attributes['source_face'].data] if mesh.polygons else []
                pe=obj.parent.evaluated_get(dg);pm=pe.to_mesh()
                for f,index in zip(mesh.polygons,source_ids):
                    if f.normal.dot(pm.polygons[index].normal)>=0 and len(outline_failures)<10:outline_failures.append((scene.name,frame,obj.name,f.index))
                pe.to_mesh_clear()
            elif obj.name in new_poses and 'Laser' not in obj.name:degenerate+=sum(p.area<1e-12 for p in mesh.polygons)
            ev.to_mesh_clear()
        all_counts.append(count)
    result['scenes'][scene.name]={'frames_checked':len(all_counts),'scene_triangle_count':all_counts[0],'constant':len(set(all_counts))==1,'within_10000':max(all_counts)<=10000}
    print('AMPLITUDE_SCENE_CHECKED',scene.name,flush=True)

measure.update(body_amplitude_max_error_m=max_body_error,foot_lift_3x_max_error_m=float(max_lift_error),old_sampled_foot_lift_m=float(sampled_lift_old),new_sampled_foot_lift_m=float(sampled_lift_new),minimum_animated_body_z_m=min_z,maximum_rigid_error_m=max_rigid_error,maximum_lamp_mount_error_m=max_lamp_error,maximum_loop_error_m=max_loop_error,maximum_laser_attachment_error_m=max_laser_error,maximum_laser_radius_change_m=max_laser_radius_error,maximum_mining_foot_drift_m=max_mining_foot_drift,degenerate_body_faces_across_frames=degenerate,nonfinite_states=nonfinite)
checks.update(body_motion_multiplier_correct=max_body_error<.000005,walk_foot_lift_three_times=max_lift_error<.000005,feet_above_ground=min_z>=-.0001,rigid_components_preserved=max_rigid_error<.000005,lamps_stay_mounted=max_lamp_error<.000005,loops_closed=max_loop_error<.000005,laser_attached_timing_and_radius_preserved=max_laser_error<.000005 and max_laser_radius_error<.000005,mining_feet_planted=max_mining_foot_drift<.000005,no_outline_inversions=not outline_failures,no_degenerate_or_nonfinite_geometry=degenerate==0 and nonfinite==0)
result['outline_failures']=outline_failures;result['all_checks_passed']=all(checks.values()) and all(s['constant'] and s['within_10000'] for s in result['scenes'].values())
result['checks']={k:bool(v) for k,v in checks.items()}
result['visual_acceptance']='待用户复审；未渲染、未截图、未由助手读图。'
(ROOT/'validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False),flush=True)
assert result['all_checks_passed']
