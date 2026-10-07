import bpy
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v1')
bpy.ops.wm.open_mainfile(filepath=str(R/'ControlRigMech_B_v1_Production.blend'))
s=bpy.context.scene; s.render.resolution_percentage=75
b=bpy.data.objects['ControlRigMech_LOD0_Body']; o=bpy.data.objects['ControlRigMech_LOD0_Outline']
nt=b.data.materials[0].node_tree; line=nt.nodes['InternalLineStrength']
mask=next(n for n in nt.nodes if n.type=='TEX_IMAGE' and 'InternalLine' in n.image.name)
ramp=nt.nodes.new('ShaderNodeMapRange'); ramp.inputs['From Min'].default_value=.12; ramp.inputs['From Max'].default_value=.35
nt.links.new(mask.outputs['Color'],ramp.inputs['Value']); nt.links.new(ramp.outputs[0],line.inputs[0])
s.render.filepath=str(R/'FilteredLineDiagnostic_Crisp.png'); bpy.ops.render.render(write_still=True)
line.inputs[1].default_value=0; o.hide_render=True; s.render.use_freestyle=True
s.render.filepath=str(R/'FilteredLineDiagnostic_FreestyleActualLOD0.png'); bpy.ops.render.render(write_still=True)
print('FILTERED_LINES_DIAG_OK',flush=True)
