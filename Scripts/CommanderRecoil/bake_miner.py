"""New miner action source and three LOD VATs; leaves the approved v5 untouched."""
import bpy,ast,json,math,hashlib
import numpy as np
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2]
ART=ROOT/'quadruped-mech-side-panels/blender'
SOURCE=ART/'quadruped-mech-vertex-animation-10k-v5.blend'
OUT=ROOT/'ArtSource/CommanderRecoil_20261114/Miner';OUT.mkdir(exist_ok=True)
OUTPUT=OUT/'QuadrupedMiner_Action_v6.blend'
source_hash=hashlib.sha256(SOURCE.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(SOURCE),load_ui=False)
A={'__file__':str(ART/'create_vertex_animations.py'),'__name__':'miner_recoil_reference'}
exec(compile((ART/'create_vertex_animations.py').read_text(encoding='utf8').split('\nbase_scene = bpy.context.scene',1)[0],A['__file__'],'exec'),A)
static=next(s for s in bpy.data.scenes if s.name.startswith('00'));bpy.context.window.scene=static
objects={o.name:o for o in static.objects};rest={n:np.asarray([o.matrix_world@v.co for v in o.data.vertices]) for n,o in objects.items() if o.type=='MESH' and not o.get('is_lineart')}
helper={'ANIM':A,'REST_OBJECTS':objects,'ORIGINAL':{n:o.data for n,o in objects.items() if o.type=='MESH'},'np':np,'Vector':Vector}
builder=ast.parse((ART/'reduction_10k_v3/build_reduced_asset.py').read_text(encoding='utf8'))
exec(compile(ast.Module(body=[n for n in builder.body if isinstance(n,ast.FunctionDef) and n.name in {'classify','pose_points','transform_points','outline_safety_weights'}],type_ignores=[]),'miner_helpers','exec'),helper)
specs=json.loads((ART/'animation_amplitude_v4/motion-specs.json').read_text(encoding='utf8'))
REPORT={'minimum_joint_reach_margin_m':100.,'maximum_joint_endpoint_error_m':0.}

def clip_body(clip,frame):
    time=(frame-1)/72*2.46
    if time<.18:
        kick=A['smooth01'](time/.18);offset=(0,.68*kick,-.64*kick);pitch=math.radians(-7)*kick;laser=kick
    else:
        old,offset,phase,laser=A['mining_pose'](1+(time+.54)*24)
        offset=tuple(2*x for x in offset);pitch=old.to_euler('XYZ').x*2
    return A['body_matrix'](offset,pitch,0),time/2.46,laser

# Reuse the reviewed rigid IK and per-part mapping, changing only the body timing.
scope={'A':A,'REPORT':REPORT,'clip_body':clip_body,'math':math,'CACHE':{}}
tree=ast.parse((ART/'animation_amplitude_v4/build_amplitude.py').read_text(encoding='utf8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in {'joints','matrices'}],type_ignores=[]),'miner_rigid_ik','exec'),scope)
matrices=scope['matrices']
for step in range(72*16+1):
    for leg in A['LEGS']:scope['joints']('Mining',1+step/16,leg)
scene=next(s for s in bpy.data.scenes if s.name.startswith('02'));bpy.context.window.scene=scene
scene.render.fps=30;scene.render.fps_base=1.025;scene.frame_end=72
changed=[]
for obj in scene.objects:
    if obj.type!='MESH' or obj.get('is_lineart') or not obj.data.shape_keys:continue
    keys=obj.data.shape_keys.key_blocks;name=obj.get('asset_source_name',obj.name);inverse=obj.matrix_world.inverted()
    assert len(keys)==73,(obj.name,len(keys))
    for frame,key in enumerate(keys,1):
        if obj.get('is_team_light'):
            points=helper['transform_points'](rest[name],matrices('Mining',frame,obj['rigid_leg'])['upper'])
        elif name in specs:
            points=np.empty_like(rest[name]);labels=np.asarray([a.value for a in obj.data.attributes['source_component'].data])
            for part in specs[name]:
                ids=np.flatnonzero(labels==part['component']);m=matrices('Mining',frame,part['leg'])
                points[ids]=helper['pose_points'](rest[name][ids],part['leg'],part['group'],part['span'],m)
        elif 'Laser' in obj.name:
            start=clip_body('Mining',frame)[0]@Vector((0,-2.055,1.712));target=Vector((0,-6.50,.15))
            direction=(target-start).normalized();u=direction.cross(Vector((1,0,0))).normalized();v=direction.cross(u).normalized();radius=.025*clip_body('Mining',frame)[2]
            sides=len(key.data)//2;points=np.array([c+radius*(u*math.cos(math.tau*i/sides)+v*math.sin(math.tau*i/sides)) for c in (start,target) for i in range(sides)])
        else:raise RuntimeError(obj.name)
        key.data.foreach_set('co',helper['transform_points'](points,inverse).astype(np.float32).ravel())
    obj.data.update();changed.append(name)
scene['动作说明']='v6：移除0.54秒准备；中立直接后坐；2.46秒循环；幅度保留v5。'
scene.frame_set(1)
walk=next(s for s in bpy.data.scenes if s.name.startswith('01'));walk.render.fps=24;walk.render.fps_base=1.;walk.frame_end=28
walk['动作说明']='v5抬足与前伸保持；固定28/24秒循环，移动与转向只决定启停。'
bpy.context.window.scene=scene;bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT),check_existing=False,compress=True)
assert hashlib.sha256(SOURCE.read_bytes()).hexdigest()==source_hash
REPORT.update(source=str(SOURCE),source_sha256=source_hash,output=str(OUTPUT),changed_objects=changed,mining_duration=2.46,walk_duration=28/24,source_unchanged=True,visual_review='Pending user B')
(OUT/'action-authoring.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
# The reviewed bake code preserves integer UV addresses, winding, normals and lamps.
code=(ROOT/'Scripts/QuadrupedMiner/bake_v5.py').read_text(encoding='utf8')
code=code.replace("SOURCE = ROOT / 'quadruped-mech-side-panels/blender/quadruped-mech-vertex-animation-10k-v5.blend'",'SOURCE = Path('+repr(str(OUTPUT))+')')
code=code.replace("SHA = '9b2fd48e472117d6c2dfed25e437305137b1b1b2a1c9c2f5ca8c4e9cb3ca454c'",'SHA = '+repr(hashlib.sha256(OUTPUT.read_bytes()).hexdigest()))
code=code.replace("OUT = ROOT / 'ArtSource/QuadrupedMiner_10k_v5'",'OUT = Path('+repr(str(OUT))+')')
code=code.replace("dict(name='Mining', first=30, count=73, duration=3)","dict(name='Mining', first=30, count=73, duration=2.46)")
exec(compile(code,str(ROOT/'Scripts/QuadrupedMiner/bake_v5.py'),'exec'),{'__file__':str(ROOT/'Scripts/QuadrupedMiner/bake_v5.py'),'__name__':'miner_v6_bake'})
