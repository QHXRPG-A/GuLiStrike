"""Refine actual reduced topology's hard edges and weighted planar normals."""
import bpy,bmesh,math,json
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004'); O=R/'Production_B_v1'
bpy.ops.wm.open_mainfile(filepath=str(O/'ControlRigMech_B_v1_EditableParts.blend'))
col=bpy.data.collections['EDITABLE_MECHANICAL_PARTS']
for ob in col.objects:
    bm=bmesh.new(); bm.from_mesh(ob.data); bm.normal_update()
    for f in bm.faces: f.smooth=True
    for e in bm.edges:
        e.smooth=not (e.is_manifold and e.calc_face_angle()>math.radians(35))
    bm.to_mesh(ob.data); bm.free()
    mod=ob.modifiers.new('ControlledPlanarWeightedNormals','WEIGHTED_NORMAL')
    mod.mode='FACE_AREA_WITH_ANGLE'; mod.keep_sharp=True; mod.weight=50
    ob.modifiers.move(len(ob.modifiers)-1,len(ob.modifiers)-2)
bpy.context.view_layer.update()
bpy.context.scene.render.filepath=str(O/'NormalRefinement_Hero.png')
bpy.ops.wm.save_as_mainfile(filepath=str(O/'ControlRigMech_B_v1_NormalRefined.blend'))
bpy.ops.render.render(write_still=True)
print('PRODUCTION_NORMAL_REFINEMENT_OK',flush=True)
