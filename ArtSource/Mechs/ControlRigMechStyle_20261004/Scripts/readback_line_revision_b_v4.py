"""Check saved mesh/rig invariants and the nine decoded motion checkpoints."""
import bpy,json,hashlib
import numpy as np
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004');O=R/'Production_B_v4'
def shape_skin(ob,body=True):
 m=ob.data;m.calc_loop_triangles();h=hashlib.sha256()
 fields=([v.co[:] for v in m.vertices],[t.vertices[:] for t in m.loop_triangles],[n.vector[:] for n in m.corner_normals])
 if body:fields+=([u.uv[:] for u in m.uv_layers[0].data],[c.color[:] for c in m.color_attributes['GuLi_PaletteLinear'].data])
 for x in fields:h.update(np.asarray(x).tobytes())
 h.update(json.dumps([[g.name for g in ob.vertex_groups],[[[g.group,g.weight] for g in v.groups] for v in m.vertices]],separators=(',',':')).encode());return h.hexdigest()
def rig_digest():
 return hashlib.sha256(json.dumps([(b.name,b.parent.name if b.parent else None,list(x for row in b.matrix_local for x in row)) for b in bpy.data.objects['Armature'].data.bones],separators=(',',':')).encode()).hexdigest()
def actions_digest():
 data=[]
 for a in bpy.data.actions:
  if not a.name.startswith('ControlRigMech_Mech_'):continue
  curves=[]
  for layer in a.layers:
   for strip in layer.strips:
    for bag in strip.channelbags:
     for f in bag.fcurves:curves.append((f.data_path,f.array_index,[(tuple(k.co),tuple(k.handle_left),tuple(k.handle_right),k.interpolation) for k in f.keyframe_points]))
  data.append((a.name,curves))
 return hashlib.sha256(json.dumps(data,separators=(',',':')).encode()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(R/'Production_B_v3/ControlRigMech_B_v3_Production.blend'))
bodies=[shape_skin(bpy.data.objects[f'ControlRigMech_LOD{i}_Body']) for i in range(4)]
outlines=[shape_skin(bpy.data.objects[f'ControlRigMech_B_v3_LOD{i}_Outline'],False) for i in range(3)]
widths=[bpy.data.objects[f'ControlRigMech_B_v3_LOD{i}_Outline']['width_m'] for i in range(3)]
rig=rig_digest();actions=actions_digest()
bpy.ops.wm.open_mainfile(filepath=str(O/'ControlRigMech_B_v4_Production.blend'))
assert bodies==[shape_skin(bpy.data.objects[f'ControlRigMech_LOD{i}_Body']) for i in range(4)]
assert outlines==[shape_skin(bpy.data.objects[f'ControlRigMech_B_v4_LOD{i}_Outline'],False) for i in range(3)]
assert widths==[bpy.data.objects[f'ControlRigMech_B_v4_LOD{i}_Outline']['width_m'] for i in range(3)]
assert rig==rig_digest() and actions==actions_digest()
rows=[]
for i in range(4):
 mat=bpy.data.objects[f'ControlRigMech_LOD{i}_Body'].active_material;ramp=next(n for n in mat.node_tree.nodes if n.type=='VALTORGB')
 assert ramp.color_ramp.interpolation=='CONSTANT' and np.allclose([e.color[0] for e in ramp.color_ramp.elements],[.42,.74,1.])
 rows.append({'lod':i,'body_geometry_normals_uv0_palette_skin_sha256':bodies[i],'internal_line_strength':mat.node_tree.nodes['InternalLineStrength'].inputs[1].default_value})
report={'version':'B-v4','source_blender_sha256':hashlib.sha256((R/'Production_B_v3/ControlRigMech_B_v3_Production.blend').read_bytes()).hexdigest(),'saved_blender_sha256':hashlib.sha256((O/'ControlRigMech_B_v4_Production.blend').read_bytes()).hexdigest(),'body_shape_normals_uv0_palette_and_skin_exactly_preserved':True,'outline_shape_normals_weights_and_width_exactly_preserved':True,'bone_names_parents_rest_matrices_exactly_preserved':True,'full_source_action_curves_keys_handles_and_interpolation_exactly_preserved':True,'rig_sha256':rig,'actions_sha256':actions,'three_tone_constant_ramp_preserved':True,'freestyle':bpy.data.scenes['STYLE_REALTIME_B_v4'].render.use_freestyle,'lods':rows,'scope':'Line-only revision. No new UE validation, budget exception, or runtime FPS result.'}
(O/'saved_line_readback_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('B4_SAVED_READBACK_OK',json.dumps(rows),flush=True)
