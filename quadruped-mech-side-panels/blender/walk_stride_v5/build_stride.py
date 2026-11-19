"""Longer walking stroke, preserving the raised swing and the entire mining clip."""
import bpy,ast,json,math,hashlib,numpy as np
from pathlib import Path
from mathutils import Vector

ROOT=Path(__file__).resolve().parent
SOURCE=ROOT.parent/'quadruped-mech-vertex-animation-10k-v4.blend'
OUTPUT=ROOT.parent/'quadruped-mech-vertex-animation-10k-v5.blend'
assert Path(bpy.data.filepath)==SOURCE
source_hash=hashlib.sha256(SOURCE.read_bytes()).hexdigest()
SPECS=json.loads((ROOT.parent/'animation_amplitude_v4'/'motion-specs.json').read_text(encoding='utf-8'))
p=ROOT.parent/'create_vertex_animations.py';A={'__file__':str(p),'__name__':'stride_reference'}
exec(compile(p.read_text(encoding='utf-8').split('\nbase_scene = bpy.context.scene',1)[0],str(p),'exec'),A)
tree=ast.parse((ROOT.parent/'reduction_10k_v3'/'build_reduced_asset.py').read_text(encoding='utf-8'))
helpers={'np':np,'ANIM':A,'Vector':Vector}
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in {'pose_points','transform_points','outline_safety_weights'}],type_ignores=[]),'stride_helpers','exec'),helpers)
static=next(s for s in bpy.data.scenes if s.name.startswith('00'));bpy.context.window.scene=static;bpy.context.view_layer.update()
rest={o.name:np.array([o.matrix_world@v.co for v in o.data.vertices],dtype=np.float64) for o in static.objects if o.type=='MESH' and not o.get('is_lineart')}
REPORT={'source':str(SOURCE),'source_sha256':source_hash,'output':str(OUTPUT),'stride_before_m':.72,'stride_after_m':1.2,'stride_multiplier':1.2/.72,'foot_lift_m':.72,'body_forward_lean_degrees':1.5,'body_support_offset_z_m':-.37,'stance_recenter_fraction':.35,'minimum_reach_margin_m':100.,'maximum_joint_endpoint_error_m':0.,'review_frame':23,'visual_acceptance':'待用户复审，未渲染、未截图、未由助手读图。'}

def body_matrix(frame):
    phase=((frame-1)%28)/28
    return A['body_matrix']((.078*math.sin(math.tau*phase),0.,-.37+.126*math.cos(2*math.tau*phase)),math.radians(1.5+3.3*math.sin(math.tau*phase)),math.radians(2.25)*math.sin(math.tau*phase)),phase

def leg_matrices(frame,leg):
    body,phase=body_matrix(frame)
    if leg is None:return {'body':body}
    data=A['LEGS'][leg];hip=body@data['hip'];ankle,alignment=A['walk_foot'](leg,phase)
    ankle.y=data['ankle'].y+(ankle.y-data['ankle'].y)*(1.2/.72)+(data['hip'].y-data['ankle'].y)*.35
    ankle.z=data['ankle'].z+(ankle.z-data['ankle'].z)*3
    axis=ankle-hip;distance=axis.length;direction=axis.normalized();l1=data['upper_length'];l2=data['lower_length']
    REPORT['minimum_reach_margin_m']=min(REPORT['minimum_reach_margin_m'],l1+l2-distance)
    assert abs(l1-l2)+.001<distance<l1+l2-.001,(frame,leg,distance,l1+l2)
    along=(l1*l1-l2*l2+distance*distance)/(2*distance);height=math.sqrt(max(0,l1*l1-along*along))
    bend=data['bend']-direction*data['bend'].dot(direction);bend.normalize();knee=hip+direction*along+bend*height
    upper=A['segment_matrix'](data['hip'],data['knee'],hip,knee);lower=A['segment_matrix'](data['knee'],data['ankle'],knee,ankle)
    foot=A['translation'](ankle-data['ankle'])
    if alignment>0:
        rotation=lower.to_quaternion();identity=rotation.copy();identity.identity();rotation=identity.slerp(rotation,alignment)
        foot=A['translation'](ankle)@rotation.to_matrix().to_4x4()@A['translation'](-data['ankle'])
    error=max((upper@data['knee']-knee).length,(lower@data['knee']-knee).length,(lower@data['ankle']-ankle).length,(foot@data['ankle']-ankle).length)
    REPORT['maximum_joint_endpoint_error_m']=max(REPORT['maximum_joint_endpoint_error_m'],error)
    return {'body':body,'hip':body,'upper':upper,'lower':lower,'foot':foot}

for step in range(28*16+1):
    for leg in A['LEGS']:leg_matrices(1+step/16,leg)
cache={(frame,leg):leg_matrices(frame,leg) for frame in range(1,30) for leg in [None]+list(A['LEGS'])}
walk=next(s for s in bpy.data.scenes if s.name.startswith('01'));bpy.context.window.scene=walk;bpy.context.view_layer.update()
for obj in walk.objects:
    if obj.type!='MESH' or obj.get('is_lineart') or not obj.data.shape_keys:continue
    name=obj.get('asset_source_name',obj.name);inverse=obj.matrix_world.inverted();points=rest[name]
    labels=np.array([a.value for a in obj.data.attributes['source_component'].data]) if name in SPECS else None
    for frame,key in enumerate(obj.data.shape_keys.key_blocks,1):
        if obj.get('is_team_light'):posed=helpers['transform_points'](points,cache[frame,obj['rigid_leg']]['upper'])
        else:
            posed=np.empty_like(points)
            for part in SPECS[name]:
                indices=np.flatnonzero(labels==part['component'])
                posed[indices]=helpers['pose_points'](points[indices],part['leg'],part['group'],part['span'],cache[frame,part['leg']])
        key.data.foreach_set('co',helpers['transform_points'](posed,inverse).astype(np.float32).ravel())
    obj.data.update();obj['动画幅度版本']='v5 · 1.2米迈步 / 0.72米抬足'
for obj in walk.objects:
    if not obj.get('is_lineart'):continue
    modifier=next(m for m in obj.modifiers if m.type=='SOLIDIFY');weights=helpers['outline_safety_weights'](obj.data,modifier.thickness)
    group=obj.vertex_groups[modifier.vertex_group]
    for index,weight in enumerate(weights):group.add([index],float(weight),'REPLACE')
walk['动作说明']='前后步幅1.2m，抬足0.72m，机身前倾1.5度；原周期和速度'
walk['步幅_米']=1.2;walk['抬足_米']=.72
walk.frame_set(23);bpy.context.view_layer.update()
notes=bpy.data.texts.get('减面交付说明');notes.write('''

v5 行进步幅修订：四足前后行程0.72m → 1.20m，增加约67%；抬足仍为0.72m。
机身轻微前倾1.5度，重新校正支撑姿态；腿段长度、周期和帧率不变。
仅更新行进动作，静态模型、采矿动作、配色、阵营灯控制及面数保持v4。
''')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT),check_existing=False,compress=True)
REPORT['source_unchanged']=hashlib.sha256(SOURCE.read_bytes()).hexdigest()==source_hash;assert REPORT['source_unchanged']
REPORT['output_sha256']=hashlib.sha256(OUTPUT.read_bytes()).hexdigest();REPORT['output_bytes']=OUTPUT.stat().st_size
(ROOT/'stride-report.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(REPORT,ensure_ascii=False),flush=True)
