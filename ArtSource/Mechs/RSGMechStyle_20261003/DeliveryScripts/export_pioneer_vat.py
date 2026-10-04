"""Run in background Blender; B-v1 is read-only. Export + rigid bone VAT at 30 fps."""
import bpy
import hashlib
import json
import math
import struct
from pathlib import Path
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'Delivery_UE_v1'
B1 = ROOT / 'Production_B_v1/RSGMech_Production_B_v1.blend'
EXPECTED = '3b330e2d2e84bb464b904aa2bb77e2043e79489c226c8d39815c8fe11f19a887'
assert hashlib.sha256(B1.read_bytes()).hexdigest() == EXPECTED, 'Approved B-v1 changed'
assert Path(bpy.data.filepath).resolve() == B1.resolve()
for directory in ['FBX', 'Textures', 'Reports']:
    (OUT / directory).mkdir(parents=True, exist_ok=True)

rig = bpy.data.objects['Armature']
assert len(rig.data.bones) == 44
source = json.loads((ROOT / 'Source/source_manifest.json').read_text(encoding='utf8'))
bones = [b['name'] for b in source['bones']]
assert set(bones) == {b.name for b in rig.data.bones}
bone_index = {n: i for i, n in enumerate(bones)}
upper_descendants=[n for n in bones if n=='Top_M' or any(p.name=='Top_M' for p in rig.data.bones[n].parent_recursive)]
rig.animation_data.action = None
for track in rig.animation_data.nla_tracks:
    track.mute = True
bpy.context.scene.frame_set(1)

# A mesh exported in Blender world coordinates becomes (x,-y,z) in native UE FBX.
# Rotate Blender's source forward -Y into +X, then account for UE handedness in VAT.
turn = Matrix.Rotation(math.pi / 2, 4, 'Z')
flip = Matrix.Diagonal(Vector((1, -1, 1, 1)))
body = bpy.data.objects['RSGMech_LOD0_Body']
outline = bpy.data.objects['RSGMech_LOD0_Contour']
points = [o.matrix_world @ v.co for o in [body, outline] for v in o.data.vertices]
source_width = max(p.x for p in points) - min(p.x for p in points)
scale = 6.25 / source_width
to_ue = Matrix.Diagonal(Vector((100*scale,100*scale,100*scale,1))) @ flip @ turn
to_export = Matrix.Diagonal(Vector((scale,scale,scale,1))) @ turn
rest = {n: rig.matrix_world @ rig.data.bones[n].matrix_local for n in bones}
rest_ue = {n: to_ue @ rest[n] @ to_ue.inverted() for n in bones}
weights, runtime = {}, []
lod_reports = []

def select(objects):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.hide_set(False)
        obj.hide_viewport = False
        for collection in obj.users_collection:
            collection.hide_viewport = False
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[-1]

def export_fbx(path, objects, animation=False):
    select(objects)
    bpy.ops.export_scene.fbx(filepath=str(path), use_selection=True,
        object_types={'MESH','ARMATURE'}, axis_forward='-Y', axis_up='Z',
        apply_unit_scale=True, global_scale=1.0, add_leaf_bones=False,
        use_armature_deform_only=False, use_mesh_modifiers=False,
        mesh_smooth_type='FACE', use_triangles=True, bake_anim=animation, bake_anim_use_all_actions=False,
        bake_anim_use_nla_strips=False, bake_anim_step=1.0, bake_anim_simplify_factor=0.0,
        bake_anim_force_startend_keying=True, bake_anim_use_all_bones=True,
        path_mode='AUTO')

def only_rigid_weights(obj):
    result = []
    for v in obj.data.vertices:
        assigned = [(obj.vertex_groups[g.group].name, g.weight) for g in v.groups if g.weight > 1e-6]
        assert len(assigned) == 1 and abs(assigned[0][1]-1) < 1e-6, (obj.name,v.index,assigned)
        assert assigned[0][0] in bone_index
        result.append(bone_index[assigned[0][0]])
    return result

for lod in range(4):
    parts = [bpy.data.objects[f'RSGMech_LOD{lod}_Body']]
    if lod < 3:
        parts.append(bpy.data.objects[f'RSGMech_LOD{lod}_Contour'])
    weights.update({o.name:only_rigid_weights(o) for o in parts})
    # Native SK preserves all original bone names/hierarchy/rest and authored cm.
    export_fbx(OUT/f'FBX/SK_Pioneer_LOD{lod}.fbx', [rig]+parts)
    static_parts = []
    for obj in parts:
        mesh = obj.data.copy()
        copy = bpy.data.objects.new(f'VAT_LOD{lod}_{"Contour" if obj.name.endswith("Contour") else "Body"}', mesh)
        bpy.context.scene.collection.objects.link(copy)
        mesh.transform(to_export @ obj.matrix_world)
        # UV0 structural mask, UV1 color atlas for every LOD; UV2 nearest bone pixel.
        if len(mesh.uv_layers) == 0:
            mesh.uv_layers.new(name='UV0_LineMask')
        if len(mesh.uv_layers) == 1:
            uv1 = mesh.uv_layers.new(name='UV1_ColorAtlas')
            for a,b in zip(uv1.data,mesh.uv_layers[0].data):
                a.uv = b.uv
        while len(mesh.uv_layers) > 2:
            mesh.uv_layers.remove(mesh.uv_layers[-1])
        uv2 = mesh.uv_layers.new(name='UV2_BoneIndex')
        for loop in mesh.loops:
            bi=weights[obj.name][loop.vertex_index]
            bn=bones[bi]
            flag=2 if bn=='ShotgunTop_L' else 3 if bn=='ShotgunTop_R' else 1 if bn in upper_descendants else 0
            uv2.data[loop.index].uv = ((bi+0.5)/44.0,float(flag))
        copy['source_approved_object'] = obj.name
        static_parts.append(copy)
        runtime.append((obj.name,mesh,weights[obj.name]))
    export_fbx(OUT/f'FBX/SM_Pioneer_VAT_LOD{lod}.fbx', static_parts)
    lod_reports.append({'lod':lod,'body_triangles':sum(len(p.vertices)-2 for p in parts[0].data.polygons),
        'outline_triangles':sum(len(p.vertices)-2 for p in parts[1].data.polygons) if lod<3 else 0,
        'material_sections':len(parts),'vertices':sum(len(o.data.vertices) for o in parts),
        'single_bone_weights':True,'uv_channels':3})

actions = [('Idle','A_FPS_Mech_Idle_01',True),
    ('Forward','A_FPS_Mech_WalkForward_01',True),
    ('Backward','A_FPS_Mech_WalkBackward_01',True),
    ('Left','A_FPS_Mech_Walk_L_01',True),('Right','A_FPS_Mech_Walk_R_01',True),
    ('Death','A_FPS_Mech_Death_01',False),('Landing','A_FPS_Mech_Landing_01',False)]
frames, clips = [], []
runtime_min = Vector((math.inf,math.inf,math.inf))
runtime_max = Vector((-math.inf,-math.inf,-math.inf))
landing_min = runtime_min.copy()
landing_max = runtime_max.copy()
lod0 = [(o,to_ue@o.matrix_world,weights[o.name]) for o in [body,outline]]
max_error_cm, max_scale_error = 0.0, 0.0
for clip_name,action_name,loop in actions:
    action = bpy.data.actions[action_name]
    rig.animation_data.action = action
    rig.animation_data.action_slot = action.slots[0]
    start,end = map(int,action.frame_range)
    bpy.context.scene.frame_start,bpy.context.scene.frame_end = start,end
    export_fbx(OUT/f'FBX/{action_name}.fbx',[rig],animation=True)
    first = len(frames)
    for frame in range(start,end+1):
        bpy.context.scene.frame_set(frame)
        deltas = []
        for n in bones:
            delta = to_ue @ (rig.matrix_world@rig.pose.bones[n].matrix) @ rest[n].inverted() @ to_ue.inverted()
            location,quat,s = delta.decompose()
            max_scale_error = max(max_scale_error,max(abs(x-1) for x in s))
            quat.normalize()
            deltas.append({'translation':[float(x) for x in location],
                'rotation':[float(quat.x),float(quat.y),float(quat.z),float(quat.w)]})
        frames.append(deltas)
        # Envelope from real rigid vertices; shader envelope includes runtime Death, not showcase Landing.
        bmin,bmax = (landing_min,landing_max) if clip_name=='Landing' else (runtime_min,runtime_max)
        for obj,world,indices in lod0:
            for v,i in zip(obj.data.vertices,indices):
                delta = to_ue @ (rig.matrix_world@rig.pose.bones[bones[i]].matrix) @ rest[bones[i]].inverted() @ to_ue.inverted()
                p = delta @ (world@v.co)
                for axis in range(3):
                    bmin[axis] = min(bmin[axis],p[axis]); bmax[axis] = max(bmax[axis],p[axis])
        if frame in [start,(start+end)//2,end]:
            evaluated = body.evaluated_get(bpy.context.evaluated_depsgraph_get())
            evaluated_mesh = evaluated.to_mesh()
            for vi in range(0,len(body.data.vertices),43):
                bi = weights[body.name][vi]
                delta = to_ue @ (rig.matrix_world@rig.pose.bones[bones[bi]].matrix) @ rest[bones[bi]].inverted() @ to_ue.inverted()
                expected = to_ue @ evaluated.matrix_world @ evaluated_mesh.vertices[vi].co
                max_error_cm = max(max_error_cm,(expected - delta@(to_ue@body.matrix_world@body.data.vertices[vi].co)).length)
            evaluated.to_mesh_clear()
    clips.append({'name':clip_name,'source_action':action_name,'first_frame':first,
        'frame_count':end-start+1,'duration_seconds':(end-start)/30.0,'loop':loop,
        'stride_cm':1440.0 if clip_name in ['Forward','Backward','Left','Right'] else 0.0})
    if clip_name in ['Forward','Backward','Left','Right']:
        axis=0 if clip_name in ['Forward','Backward'] else 1
        spans=[]
        for n in bones:
            if 'Leg4_' not in n:
                continue
            reference=to_ue@rest[n].translation
            samples=[]
            from mathutils import Quaternion
            for f in frames[first:]:
                d=f[bone_index[n]]; q=d['rotation']
                p=Quaternion((q[3],q[0],q[1],q[2]))@reference+Vector(d['translation'])
                samples.append(p[axis])
            spans.append(max(samples)-min(samples))
        assert len(spans)==6 and min(spans)>1, (clip_name,spans)
        clips[-1]['stride_cm']=2*sum(spans)/len(spans)
        clips[-1]['stride_measurement']='twice mean six-foot axial stance excursion, source alternating tripod cycle'
assert max_scale_error < 1e-4, ('Animation includes scale',max_scale_error)
assert max_error_cm < 0.05, ('Rigid skinning does not match source',max_error_cm)

def save_float_exr(name,field):
    image = bpy.data.images.new(name,width=44,height=len(frames),alpha=True,float_buffer=True)
    image.colorspace_settings.name = 'Non-Color'
    pixels = []
    # UE import reads the first scanline at v=0; EXR/Blender image origin is bottom-left.
    # Reverse Blender's rows so the texture's top row is animation frame zero.
    for frame in reversed(frames):
        for bone in frame:
            values = bone[field]
            pixels.extend(values+[1.0] if field=='translation' else values)
    image.pixels.foreach_set(pixels)
    scene=bpy.context.scene
    scene.render.image_settings.file_format='OPEN_EXR'
    scene.render.image_settings.color_mode='RGBA'
    scene.render.image_settings.color_depth='32'
    scene.render.image_settings.exr_codec='ZIP'
    image.save_render(str(OUT/f'Textures/{name}.exr'),scene=scene)

save_float_exr('T_Pioneer_BonePosition','translation')
save_float_exr('T_Pioneer_BoneRotation','rotation')
rig.animation_data.action=None
bpy.context.scene.frame_set(1)
gameplay_points=[to_ue@p for p in points]
gameplay_min=[min(p[a] for p in gameplay_points) for a in range(3)]
gameplay_max=[max(p[a] for p in gameplay_points) for a in range(3)]

# Determine real upper gun muzzle planes from weighted geometry in the reference pose.
muzzles=[]
for name in ['ShotgunTop_L','ShotgunTop_R']:
    vertices=[to_ue@body.matrix_world@v.co for v,i in zip(body.data.vertices,weights[body.name]) if bones[i]==name]
    front=max(v.x for v in vertices)
    plane=[v for v in vertices if v.x>=front-1.5*scale]
    position=sum(plane,Vector())/len(plane)
    pivot=to_ue@(rest[name].translation)
    muzzles.append({'bone_index':bone_index[name],'bone_name':name,'reference_position_cm':list(position),
        'reference_direction':[1.0,0.0,0.0],'pitch_pivot_cm':list(pivot)})
top_pivot=to_ue@(rest['Top_M'].translation)
upper_descendants=[n for n in bones if n=='Top_M' or any(p.name=='Top_M' for p in rig.data.bones[n].parent_recursive)]
metadata={'revision':'Delivery_UE_v1','approved_version':'B-v1','source_sha256':EXPECTED,
    'frames_per_second':30,'bone_count':44,'texture_width':44,'texture_height':len(frames),
    'texture_format':'RGBA32F','vertex_bone_uv_channel':2,'color_uv_channel':1,'line_uv_channel':0,
    'source_to_runtime_scale':scale,'authored_presentation_scale':1.0,'runtime_width_cm':625.0,
    'coordinate_system':'UE centimeters, +X forward, +Y right, +Z up',
    'bones':[{'name':n,'parent_index':bone_index.get(rig.data.bones[n].parent.name,-1) if rig.data.bones[n].parent else -1,
        'upper_aim':n in upper_descendants,'gun_pitch': 1 if n=='ShotgunTop_L' else 2 if n=='ShotgunTop_R' else 0} for n in bones],
    'clips':clips,'frames':frames,'muzzles':muzzles,'upper_pivot_cm':list(top_pivot),
    'gameplay_bounds_cm':{'min':gameplay_min,'max':gameplay_max},
    'runtime_render_bounds_cm':{'min':list(runtime_min),'max':list(runtime_max)},
    'landing_showcase_bounds_cm':{'min':list(landing_min),'max':list(landing_max)},
    'lods':lod_reports,'screen_sizes':[1.0,0.40,0.16,0.06],
    'rigid_skinning_max_error_cm':max_error_cm,'maximum_scale_error':max_scale_error,
    'stride_policy':'phase = measured planar travel / source six-foot stride; alternating tripod stance excursion measured per clip',
    'source_ground_contact_bias_preserved':True}
(OUT/'vat_metadata.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2),encoding='utf8')
metadata.pop('frames')
(OUT/'Reports/export_report.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2),encoding='utf8')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'RSGMech_Delivery_UE_v1.blend'))
assert hashlib.sha256(B1.read_bytes()).hexdigest()==EXPECTED
print('PIONEER_EXPORT_COMPLETE '+json.dumps({'frames':len(frames),'bones':44,'scale':scale,'rigid_error_cm':max_error_cm,'lods':lod_reports}))
