"""Production re-export from static editable sources; no historic rig is required.

The export_mass_rigid_animation.py script is the one-time legacy migration only.
Maintain the RigidPart groups/UV pivot contract in these editable blend files.
"""
import bpy,json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/MechanicalAnimation_20260929'
for unit in ['WarMachine','Sweeper']:
    folder=OUT/unit
    bpy.ops.wm.open_mainfile(filepath=str(folder/(unit+'_RigidEditable.blend')))
    assert not any(o.type=='ARMATURE' for o in bpy.data.objects), 'Production source must be static'
    bpy.ops.object.select_all(action='DESELECT')
    meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
    for obj in meshes:
        assert not obj.modifiers and not obj.animation_data
        assert len(obj.data.uv_layers)==3
        for face in obj.data.polygons:
            metadata=[tuple(obj.data.uv_layers[1].data[i].uv)+tuple(obj.data.uv_layers[2].data[i].uv) for i in face.loop_indices]
            assert len(set(metadata))==1,'A face cannot straddle mechanical parts'
        obj.select_set(True)
    assert meshes
    bpy.context.view_layer.objects.active=meshes[0]
    bpy.ops.export_scene.fbx(filepath=str(folder/('SM_'+unit+'_Rigid.fbx')),use_selection=True,object_types={'MESH'},
        global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',axis_forward='-Y',axis_up='Z',
        mesh_smooth_type='FACE',bake_anim=False,path_mode='STRIP',use_custom_props=True)
    print(json.dumps({'unit':unit,'source':str(folder/(unit+'_RigidEditable.blend')),'skeletons':0,'exported':True}))
