import bpy
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v1')
bpy.ops.wm.open_mainfile(filepath=str(R/'ControlRigMech_B_v1_Production.blend'))
s=bpy.context.scene; b=bpy.data.objects['ControlRigMech_LOD0_Body']; outline=bpy.data.objects['ControlRigMech_LOD0_Outline']
line=b.data.materials[0].node_tree.nodes['InternalLineStrength'].inputs[1]
line.default_value=0; outline.hide_render=True
s.render.filepath=str(R/'LineDiagnostic_CleanBody.png'); bpy.ops.render.render(write_still=True)
line.default_value=1
s.render.filepath=str(R/'LineDiagnostic_InternalOnly.png'); bpy.ops.render.render(write_still=True)
line.default_value=0; outline.hide_render=False
s.render.filepath=str(R/'LineDiagnostic_OutlineOnly.png'); bpy.ops.render.render(write_still=True)
print('LINE_DIAGNOSTICS_OK',flush=True)
