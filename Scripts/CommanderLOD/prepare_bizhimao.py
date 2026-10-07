"""Make the editable three-tier candidate and remap retained vertex animation data."""
import bpy
import copy
import hashlib
import json
import shutil
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"Scripts/CommanderLOD"))
from common import require_unapproved_candidate
require_unapproved_candidate()

ROOT=Path('D:/UE5.7/test1')
ART=ROOT/'ArtSource/CommanderLOD_20261005/BiZhiMao'
SOURCE=ROOT/'ArtSource/Mechs/BiZhiMao_20261005/LOD_v2'
for name in ('FBX','Textures','Reports','Review'):(ART/name).mkdir(parents=True,exist_ok=True)
old=json.loads((SOURCE/'vertex_metadata.json').read_text(encoding='utf8'))
source=SOURCE/'BiZhiMao_LOD_v2.blend'
source_hash=hashlib.sha256(source.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(source))
keep=['ControlRigMech_LOD0_Body','ControlRigMech_B_v4_LOD0_Outline',
    'ControlRigMech_LOD1_Body','ControlRigMech_B_v4_LOD1_Outline',
    'ControlRigMech_LOD'+str(len(old['lods'])-1)+'_Body']
for ob in list(bpy.data.objects):
    if ob.type=='MESH' and ob.name not in keep:
        bpy.data.objects.remove(ob,do_unlink=True)
bpy.data.objects[keep[-1]].name='ControlRigMech_LOD2_Body'
scene=bpy.context.scene
scene.name='BiZhiMao_CommanderLOD3Tier'
for ob in scene.objects:
    if ob.type=='MESH':
        show='LOD0_' in ob.name
        ob.hide_render=not show;ob.hide_set(not show);ob.hide_viewport=False
rig=bpy.data.objects['Armature']
rig.animation_data.action=bpy.data.actions['BiZhiMao_Idle_Deployed']
scene.frame_set(1)
candidate=ART/'BiZhiMao_3Tier.blend'
bpy.ops.wm.save_as_mainfile(filepath=str(candidate))
meta={key:copy.deepcopy(value) for key,value in old.items() if key not in ('lods','gpu_animation_bytes','authoring_blend','authoring_blend_sha256')}
meta.update(version='BiZhiMao_CommanderLOD_3Tier_v1',authoring_blend=str(candidate),
    authoring_blend_sha256=hashlib.sha256(candidate.read_bytes()).hexdigest(),lod_count=3,
    lods=[],actual_version_approval='pending',screen_sizes=[1.0,0.4,0.06])
for target,index in enumerate((0,1,len(old['lods'])-1)):
    item=copy.deepcopy(old['lods'][index]);item['lod']=target
    for key,folder,extension in [('position','Textures','.exr'),('rotation','Textures','.png'),('fbx','FBX','.fbx')]:
        original=Path(item[key]);stem='SM_BiZhiMao_Vertex' if key=='fbx' else 'T_BiZhiMao_Vertex'+key.capitalize()
        path=ART/folder/(stem+'_LOD'+str(target)+extension)
        if key!='fbx' or target==index:
            shutil.copy2(original,path)
        else:
            bpy.ops.wm.read_factory_settings(use_empty=True)
            bpy.ops.import_scene.fbx(filepath=str(original),use_custom_normals=True,use_anim=False)
            scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
            meshes=[ob for ob in scene.objects if ob.type=='MESH']
            assert len(meshes)==1
            for ob in meshes:
                layer=ob.data.uv_layers[5]
                for datum in layer.data:datum.uv.x=target
                ob.select_set(True)
            bpy.context.view_layer.objects.active=meshes[0]
            bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH'},global_scale=1,
                apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',
                use_mesh_modifiers=False,mesh_smooth_type='FACE',use_tspace=True,colors_type='SRGB',bake_anim=False,path_mode='STRIP')
        item[key]=str(path)
    item['sha256']={Path(item[key]).name:hashlib.sha256(Path(item[key]).read_bytes()).hexdigest() for key in ('position','rotation','fbx')}
    item['animation_samples_reused_without_geometry_change']=True
    meta['lods'].append(item)
meta['gpu_animation_bytes']=sum(item['position_gpu_bytes']+item['rotation_gpu_bytes'] for item in meta['lods'])
assert hashlib.sha256(source.read_bytes()).hexdigest()==source_hash
assert len(meta['lods'])==3
(ART/'vertex_metadata.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2),encoding='utf8')
(ART/'Reports/source_preservation.json').write_text(json.dumps(dict(success=True,source=str(source),sha256=source_hash,
    source_unchanged=True,lod0_fbx_unchanged=Path(meta['lods'][0]['fbx']).read_bytes()==Path(old['lods'][0]['fbx']).read_bytes(),
    retained_triangles=[sum(section['triangles'] for section in item['sections']) for item in meta['lods']]),indent=2),encoding='utf8')
print('BIZHIMAO_THREE_TIER_READY',meta['gpu_animation_bytes'],flush=True)
