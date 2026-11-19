"""Increase articulation through rigid transforms, never scale per-vertex deltas."""
import bpy,ast,json,math,hashlib,numpy as np
from pathlib import Path
from mathutils import Vector,Matrix

ROOT=Path(__file__).resolve().parent
SOURCE=ROOT.parent/'quadruped-mech-vertex-animation-10k-v3.blend'
OUTPUT=ROOT.parent/'quadruped-mech-vertex-animation-10k-v4.blend'
PLAN=json.loads((ROOT.parent/'reduction_10k_v3'/'optimized-components.json').read_text(encoding='utf-8'))
anim_path=ROOT.parent/'create_vertex_animations.py'
A={'__file__':str(anim_path),'__name__':'amplitude_reference'}
exec(compile(anim_path.read_text(encoding='utf-8').split('\nbase_scene = bpy.context.scene',1)[0],str(anim_path),'exec'),A)
static=next(s for s in bpy.data.scenes if s.name.startswith('00'));bpy.context.window.scene=static;bpy.context.view_layer.update()
objects={o.name:o for o in static.objects}
helper={'ANIM':A,'REST_OBJECTS':objects,'ORIGINAL':{n:o.data for n,o in objects.items() if o.type=='MESH'},'np':np,'Vector':Vector}
builder=ast.parse((ROOT.parent/'reduction_10k_v3'/'build_reduced_asset.py').read_text(encoding='utf-8'))
defs=[n for n in builder.body if isinstance(n,ast.FunctionDef) and n.name in {'classify','pose_points','transform_points','outline_safety_weights'}]
exec(compile(ast.Module(body=defs,type_ignores=[]),'amplitude_helpers','exec'),helper)
specs={}
for part in PLAN['parts']:
    leg,group,span=helper['classify'](part['object'],part)
    if part['constraints']['rigid_bearing'] or part['constraints'].get('rigid_armor'):span=None
    group=part['constraints'].get('motion_group',group)
    specs.setdefault(part['object'],[]).append({'component':part['component'],'leg':leg,'group':group,'span':span})

source_hash=hashlib.sha256(SOURCE.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(SOURCE),load_ui=True)
SCENES=list(bpy.data.scenes);STATIC=next(s for s in SCENES if s.name.startswith('00'))
bpy.context.window.scene=STATIC;bpy.context.view_layer.update()
REST={o.name:o for o in STATIC.objects if o.type=='MESH' and not o.get('is_lineart')}
REST_POINTS={name:np.asarray([o.matrix_world@v.co for v in o.data.vertices],dtype=np.float64) for name,o in REST.items()}
REPORT={'source':str(SOURCE),'source_sha256':source_hash,'output':str(OUTPUT),'user_priority':'四足抬高；行进以3倍抬脚为主，步距稳定；采矿后坐及下压2倍','walk':{'foot_lift_m':.72,'old_foot_lift_m':.24,'foot_lift_multiplier':3.,'stride_m':.72,'body_sway_multiplier':3.,'body_vertical_amplitude_m':.126,'body_lateral_amplitude_m':.078,'pitch_amplitude_degrees':3.3,'roll_amplitude_degrees':2.25,'body_support_offset_z_m':-.24,'stance_recenter_fraction':.20},'mining':{'motion_multiplier':2.,'authored_peak_back_m':.68,'authored_peak_down_m':.64,'authored_peak_pitch_degrees':7.,'front_foot_support_adjustment_y_m':.18,'feet_planted':True},'scenes':{},'maximum_joint_endpoint_error_m':0.,'minimum_joint_reach_margin_m':100.,'visual_acceptance':'待用户在Blender复审；未渲染、未由助手读图。'}

def clip_body(clip,frame):
    if clip=='Walk':
        phase=((frame-1)%28)/28
        body=A['body_matrix']((.078*math.sin(math.tau*phase),0.,-.24+.126*math.cos(2*math.tau*phase)),math.radians(3.3)*math.sin(math.tau*phase),math.radians(2.25)*math.sin(math.tau*phase))
        return body,phase,None
    old,offset,phase,laser=A['mining_pose'](frame)
    return A['body_matrix'](tuple(2*x for x in offset),old.to_euler('XYZ').x*2,0.),phase,laser

def joints(clip,frame,leg):
    body,phase,laser=clip_body(clip,frame);d=A['LEGS'][leg];hip=body@d['hip']
    if clip=='Walk':
        ankle,alignment=A['walk_foot'](leg,phase)
        ankle.z=d['ankle'].z+3*(ankle.z-d['ankle'].z)
        ankle.y+=(d['hip'].y-d['ankle'].y)*.20
    else:
        ankle=d['ankle'].copy();alignment=0.
        if 'Front' in leg:ankle.y+=.18
    axis=ankle-hip;distance=axis.length;l1=d['upper_length'];l2=d['lower_length']
    margin=l1+l2-distance
    REPORT['minimum_joint_reach_margin_m']=min(REPORT['minimum_joint_reach_margin_m'],margin)
    assert abs(l1-l2)+.001<distance<l1+l2-.001,(clip,frame,leg,distance,l1+l2)
    direction=axis.normalized();along=(l1*l1-l2*l2+distance*distance)/(2*distance)
    height=math.sqrt(max(0,l1*l1-along*along))
    bend=d['bend']-direction*d['bend'].dot(direction);bend.normalize()
    knee=hip+direction*along+bend*height
    return body,hip,knee,ankle,alignment

CACHE={}
def matrices(clip,frame,leg):
    key=(clip,frame,leg)
    if key in CACHE:return CACHE[key]
    if leg is None:result={'body':clip_body(clip,frame)[0]}
    else:
        d=A['LEGS'][leg];body,hip,knee,ankle,alignment=joints(clip,frame,leg)
        upper=A['segment_matrix'](d['hip'],d['knee'],hip,knee)
        lower=A['segment_matrix'](d['knee'],d['ankle'],knee,ankle)
        foot=A['translation'](ankle-d['ankle'])
        if alignment>0:
            rotation=lower.to_quaternion();identity=rotation.copy();identity.identity();rotation=identity.slerp(rotation,alignment)
            foot=A['translation'](ankle)@rotation.to_matrix().to_4x4()@A['translation'](-d['ankle'])
        err=max((upper@d['hip']-hip).length,(upper@d['knee']-knee).length,(lower@d['knee']-knee).length,(lower@d['ankle']-ankle).length,(foot@d['ankle']-ankle).length)
        REPORT['maximum_joint_endpoint_error_m']=max(REPORT['maximum_joint_endpoint_error_m'],err)
        result={'body':body,'hip':body,'upper':upper,'lower':lower,'foot':foot}
    CACHE[key]=result;return result

# Verify reach continuously between the authored frame samples, not only at keys.
for clip,period in (('Walk',28),('Mining',72)):
    for step in range(period*16+1):
        frame=1+step/16
        for leg in A['LEGS']:joints(clip,frame,leg)

for scene in SCENES:
    if scene==STATIC:continue
    bpy.context.window.scene=scene;bpy.context.view_layer.update();clip='Walk' if scene.name.startswith('01') else 'Mining'
    for obj in scene.objects:
        if obj.type!='MESH' or obj.get('is_lineart') or not obj.data.shape_keys:continue
        keys=obj.data.shape_keys.key_blocks;name=obj.get('asset_source_name',obj.name);inverse=obj.matrix_world.inverted()
        if obj.get('is_team_light'):
            rest=REST_POINTS[name]
            for frame,key in enumerate(keys,1):
                points=helper['transform_points'](rest,matrices(clip,frame,obj['rigid_leg'])['upper'])
                key.data.foreach_set('co',helper['transform_points'](points,inverse).astype(np.float32).ravel())
        elif name in specs:
            rest=REST_POINTS[name];labels=np.asarray([a.value for a in obj.data.attributes['source_component'].data])
            for frame,key in enumerate(keys,1):
                points=np.empty_like(rest)
                for part in specs[name]:
                    ids=np.flatnonzero(labels==part['component']);m=matrices(clip,frame,part['leg'])
                    points[ids]=helper['pose_points'](rest[ids],part['leg'],part['group'],part['span'],m)
                key.data.foreach_set('co',helper['transform_points'](points,inverse).astype(np.float32).ravel())
        elif 'Laser' in obj.name:
            # Preserve each original activation/radius sample and endpoint; only the
            # muzzle follows the amplified body motion.
            for frame,key in enumerate(keys,1):
                old=np.array([obj.matrix_world@v.co for v in key.data]);sides=len(old)//2
                assert sides==16
                radius=float(np.linalg.norm(old[0]-old[:sides].mean(axis=0)))
                start=clip_body('Mining',frame)[0]@Vector((0.,-2.055,1.712));target=Vector((0.,-6.50,.15))
                direction=(target-start).normalized();u=direction.cross(Vector((1,0,0))).normalized();v=direction.cross(u).normalized()
                points=[center+radius*(u*math.cos(math.tau*i/sides)+v*math.sin(math.tau*i/sides)) for center in (start,target) for i in range(sides)]
                key.data.foreach_set('co',helper['transform_points'](np.asarray(points),inverse).astype(np.float32).ravel())
        else:raise RuntimeError(('Unexpected animated mesh',obj.name))
        obj.data.update();obj['动画幅度版本']='v4 · 行进抬足3倍 / 采矿2倍'
    for obj in scene.objects:
        if not obj.get('is_lineart'):continue
        modifier=next(m for m in obj.modifiers if m.type=='SOLIDIFY')
        weights=helper['outline_safety_weights'](obj.data,modifier.thickness)
        group=obj.vertex_groups[modifier.vertex_group]
        for index,weight in enumerate(weights):group.add([index],float(weight),'REPLACE')
    scene['动作幅度']=3. if clip=='Walk' else 2.
    scene['动作说明']='抬足高度0.72m，机身摆动3倍，步距0.72m' if clip=='Walk' else '后坐与下压2倍，四足固定支撑，激光随炮口'
    scene.frame_set(23 if clip=='Walk' else 18);bpy.context.view_layer.update()
    counts={'body':0,'outline':0,'laser':0,'studio':0};dg=bpy.context.evaluated_depsgraph_get()
    for obj in scene.objects:
        if obj.type!='MESH':continue
        category='outline' if obj.get('is_lineart') else 'laser' if 'Laser' in obj.name else 'studio' if 'Studio ground' in obj.name else 'body'
        ev=obj.evaluated_get(dg);mesh=ev.to_mesh();mesh.calc_loop_triangles();counts[category]+=len(mesh.loop_triangles);ev.to_mesh_clear()
    counts['asset_total']=counts['body']+counts['outline']+counts['laser'];counts['scene_total']=counts['asset_total']+counts['studio']
    assert counts['scene_total']<=10000
    REPORT['scenes'][scene.name]={'frames':[scene.frame_start,scene.frame_end],'fps':scene.render.fps,'triangles':counts}
    print('AMPLIFIED',scene.name,counts,flush=True)

notes=bpy.data.texts.get('减面交付说明');notes.write('''

v4 动画幅度修订：行进抬足 0.24m → 0.72m，机身摆动3倍；步距维持0.72m。
采矿后坐、下压和俯仰2倍；动作时长及24fps不变。
腿段长度固定，重算关节与支撑姿态；灯具跟随，激光仍连接炮口。
进入行进场景停在第23帧，空格播放；采矿场景停在第18帧便于审核。
''')
walk=next(s for s in SCENES if s.name.startswith('01'));bpy.context.window.scene=walk;walk.frame_set(23);bpy.context.view_layer.update()
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT),check_existing=False,compress=True)
REPORT['source_unchanged']=hashlib.sha256(SOURCE.read_bytes()).hexdigest()==source_hash;assert REPORT['source_unchanged']
REPORT['output_sha256']=hashlib.sha256(OUTPUT.read_bytes()).hexdigest();REPORT['output_bytes']=OUTPUT.stat().st_size
(ROOT/'motion-specs.json').write_text(json.dumps(specs,ensure_ascii=False,indent=2),encoding='utf-8')
(ROOT/'amplitude-report.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
print('AMPLITUDE_COMPLETE',REPORT['maximum_joint_endpoint_error_m'],REPORT['minimum_joint_reach_margin_m'],REPORT['output_sha256'],flush=True)
