"""Saved Blender readback: lossless body surface/skin, real shader lines, shell cost and layer comparisons."""
import bpy,json,hashlib
from pathlib import Path
import numpy as np
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004'); O=R/'Production_B_v3'
with bpy.data.libraries.load(str(R/'Production_B_v2/ControlRigMech_B_v2_Production.blend')) as (src,dst):
    dst.objects=[f'ControlRigMech_LOD{i}_Body' for i in range(4)]+['Armature']
baseline=dst.objects
source=[]
for ob in baseline[:4]:
    m=ob.data; m.calc_loop_triangles(); ids=np.array([t.loops[:] for t in m.loop_triangles],dtype=np.int32).ravel()
    source.append({'coords':np.array([v.co[:] for v in m.vertices],dtype=np.float32),'triangles':[t.vertices[:] for t in m.loop_triangles],
        'normals':np.array([x.vector[:] for x in m.corner_normals],dtype=np.float32)[ids],
        'uv':np.array([x.uv[:] for x in m.uv_layers[0].data],dtype=np.float32)[ids],
        'skin':[[[g.group,float(g.weight)] for g in v.groups] for v in m.vertices],'groups':[g.name for g in ob.vertex_groups]})
sourcebones=[(b.name,b.parent.name if b.parent else None,np.array(b.matrix_local)) for b in baseline[4].data.bones]
bpy.ops.wm.open_mainfile(filepath=str(O/'ControlRigMech_B_v3_Production.blend'))
s=bpy.data.scenes['STYLE_REALTIME_B_v3']; bpy.context.window.scene=s
rig=bpy.data.objects['Armature']; rows=[]
assert [(b.name,b.parent.name if b.parent else None) for b in rig.data.bones]==[(n,p) for n,p,_ in sourcebones]
assert all(np.array_equal(np.array(b.matrix_local),sourcebones[i][2]) for i,b in enumerate(rig.data.bones))
for lod in range(4):
    ob=bpy.data.objects[f'ControlRigMech_LOD{lod}_Body']; m=ob.data; m.calc_loop_triangles(); old=source[lod]
    assert np.array_equal(np.array([v.co[:] for v in m.vertices],dtype=np.float32),old['coords'])
    assert [t.vertices[:] for t in m.loop_triangles]==old['triangles']
    assert np.array_equal(np.array([x.uv[:] for x in m.uv_layers[0].data],dtype=np.float32),old['uv'])
    assert [g.name for g in ob.vertex_groups]==old['groups']
    assert [[[g.group,float(g.weight)] for g in v.groups] for v in m.vertices]==old['skin']
    normal=np.array([x.vector[:] for x in m.corner_normals],dtype=np.float32)
    valid=np.linalg.norm(old['normals'],axis=1)>.99
    assert np.all(np.linalg.norm(normal,axis=1)>.99)
    error=float(np.max(np.abs(normal[valid]-old['normals'][valid])))
    delta=np.linalg.norm(normal[valid]-old['normals'][valid],axis=1)
    angle=2*np.rad2deg(np.arcsin(np.clip(delta/2,0,1)))
    print('NORMAL_READBACK',lod,'max_component',error,'angle_degrees_percentiles',np.percentile(angle,[50,95,99,100]).tolist(),flush=True)
    # Custom normals are re-encoded against the explicit triangular smoothing fans.
    # Original zero corner normals have no direction to preserve; Blender recalculates them.
    # Require valid, unit length output and bound valid-source angular drift to one degree.
    assert float(angle.max())<1.
    weights=[sum(g.weight for g in v.groups) for v in m.vertices]
    ramp=next(n for n in ob.active_material.node_tree.nodes if n.type=='VALTORGB')
    assert ramp.color_ramp.interpolation=='CONSTANT'
    assert np.allclose([e.color[0] for e in ramp.color_ramp.elements],[.42,.74,1.])
    rows.append({'lod':lod,'body_surface_uv0_skin_names_weights_preserved_exactly':True,'max_corner_normal_delta':error,
        'max_corner_normal_angle_degrees':float(angle.max()),'normal_comparison_tolerance_degrees':1.,
        'original_zero_corner_normals_recalculated':int((~valid).sum()),
        'body_triangles':len(m.loop_triangles),'unweighted_vertices':sum(w==0 for w in weights),
        'max_weight_sum_error':max(abs(w-1) for w in weights),'body_material_sections':len(m.materials),
        'uv_channels':[u.name for u in m.uv_layers],'three_tone':True,'internal_line_strength':ob.active_material.node_tree.nodes['InternalLineStrength'].inputs[1].default_value})
body=bpy.data.objects['ControlRigMech_LOD0_Body']; outline=bpy.data.objects['ControlRigMech_B_v3_LOD0_Outline']
D=O/'LineDiagnostics'; D.mkdir(exist_ok=True)
strength=body.active_material.node_tree.nodes['InternalLineStrength']
for label,inside,outside in [('NoLines',0,False),('InternalOnly',1,False),('OutlineOnly',0,True),('AllLines',1,True)]:
    strength.inputs[1].default_value=inside; outline.hide_render=not outside
    s.render.filepath=str(D/f'ControlRigMech_B_v3_{label}.png'); bpy.ops.render.render(write_still=True)
strength.inputs[1].default_value=1.; outline.hide_render=False
L=O/'LODComparison'; L.mkdir(exist_ok=True)
for lod in range(4):
    for ob in bpy.data.collections['PRODUCTION_LODS_SINGLE_BODY_SECTION'].objects: ob.hide_render=ob.get('LOD')!=lod
    s.render.filepath=str(L/f'ControlRigMech_B_v3_LOD{lod}_SameCamera.png'); bpy.ops.render.render(write_still=True)
report={'version':'B-v3','bone_count':len(rig.data.bones),'bone_names_parents_and_rest_matrices_exactly_match_B2':True,
    'freestyle_in_actual_render':s.render.use_freestyle,'body_checks':rows,
    'line_diagnostics':'NoLines/InternalOnly/OutlineOnly/AllLines actual same geometry/shader scene; no compositing or image editing',
    'limitations':['Budget not met','UE equivalent material/import/readback not performed','Commander FPS not measured','Small interior pieces use surface lines rather than outline geometry']}
(O/'realtime_readback_audit.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('B3_REALTIME_READBACK_OK',json.dumps(rows),flush=True)
