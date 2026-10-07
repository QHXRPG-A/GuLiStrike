import bpy,json
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004'); O=R/'Production_B_v2'
bpy.ops.wm.open_mainfile(filepath=str(O/'ControlRigMech_B_v2_Production.blend'))
s=bpy.data.scenes['PORTABLE_SHADER_LODS']; bpy.context.window.scene=s
rig=bpy.data.objects['Armature']; rig.animation_data.action=None; rig.data.pose_position='REST'
setup=json.loads((R/'References_A_v2/reference_setup.json').read_text(encoding='utf-8')); cam=setup['cameras']['Hero']
s.camera.location=cam['location_m']; s.camera.rotation_euler=cam['rotation_radians']; s.camera.data.ortho_scale=cam['ortho_scale_m']
s.render.resolution_x=s.render.resolution_y=2048; s.render.resolution_percentage=100; s.render.use_freestyle=False
D=O/'LODComparison'; D.mkdir(exist_ok=True)
for lod in range(4):
    for ob in bpy.data.collections['PRODUCTION_LODS_SINGLE_BODY_SECTION'].objects:
        ob.hide_render=ob['LOD']!=lod
    s.render.filepath=str(D/f'ControlRigMech_B_v2_LOD{lod}_SameCamera.png'); bpy.ops.render.render(write_still=True,scene=s.name)
    s.render.resolution_x=s.render.resolution_y=1280
    s.camera.data.ortho_scale=cam['ortho_scale_m']/(1,.40,.16,.06)[lod]
    s.render.filepath=str(D/f'ControlRigMech_B_v2_LOD{lod}_ScreenIllustration.png'); bpy.ops.render.render(write_still=True,scene=s.name)
    s.camera.data.ortho_scale=cam['ortho_scale_m']; s.render.resolution_x=s.render.resolution_y=2048
print('B_LOD_COMPARISON_OK',flush=True)
