import bpy,json
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004'); O=R/'Production_B_v1'
bpy.ops.wm.open_mainfile(filepath=str(O/'ControlRigMech_B_v1_Production.blend'))
s=bpy.context.scene; s.render.resolution_percentage=75
body=bpy.data.objects['ControlRigMech_LOD0_Body']; out=bpy.data.objects['ControlRigMech_LOD0_Outline']
nt=body.data.materials[0].node_tree; strength=nt.nodes['InternalLineStrength']
threshold=nt.nodes.new('ShaderNodeMath'); threshold.operation='GREATER_THAN'; threshold.inputs[1].default_value=.38
mask=next(n for n in nt.nodes if n.type=='TEX_IMAGE' and 'InternalLine' in n.image.name)
nt.links.new(mask.outputs['Color'],threshold.inputs[0]); nt.links.new(threshold.outputs[0],strength.inputs[0])
s.render.filepath=str(O/'SurfaceDiagnostic_ThresholdLine.png'); bpy.ops.render.render(write_still=True)
for ob in bpy.data.collections['PRODUCTION_LODS_SINGLE_BODY_SECTION'].objects: ob.hide_render=True
col=bpy.data.collections['EDITABLE_MECHANICAL_PARTS']; col.hide_render=False; col.hide_viewport=False
s.render.use_freestyle=True
for ob in col.objects:
    for m in ob.modifiers:
        if m.type=='WEIGHTED_NORMAL': m.show_render=False; m.show_viewport=False
s.render.filepath=str(O/'SurfaceDiagnostic_SourceNormalsOnly.png'); bpy.ops.render.render(write_still=True)
for ob in col.objects:
    for m in ob.modifiers:
        if m.type=='WEIGHTED_NORMAL': m.show_render=True; m.show_viewport=True
s.render.filepath=str(O/'SurfaceDiagnostic_WeightedSource.png'); bpy.ops.render.render(write_still=True)
print('SURFACE_DIAG_OK',flush=True)
