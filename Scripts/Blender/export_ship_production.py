"""Run in background Blender against the saved current-study copy."""
import bpy,json
from pathlib import Path
root=Path('D:/UE5.7/test1/ArtSource/Ships/ShipStylizedStudy_20260916/Production')
bpy.ops.wm.open_mainfile(filepath=str(root/'Ship_Production_Source.blend'))
bpy.context.window.scene=bpy.data.scenes['Ship_Stylized_Study']
if bpy.context.object and bpy.context.object.mode!='OBJECT': bpy.ops.object.mode_set(mode='OBJECT')
out={}
for name,dest in [('SS_Dreadnought_Stylized','SM_Ship_AnimeHull'),('SS_Ink_Outer_Contour','SM_Ship_AnimeContour')]:
    for o in bpy.context.view_layer.objects:o.select_set(False)
    o=bpy.data.objects[name];o.hide_set(False);o.select_set(True);bpy.context.view_layer.objects.active=o
    bpy.ops.export_scene.fbx(filepath=str(root/(dest+'.fbx')),use_selection=True,object_types={'MESH'},global_scale=1.0,apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',axis_forward='-Y',axis_up='Z',bake_anim=False,use_mesh_modifiers=True,mesh_smooth_type='FACE',use_tspace=True)
    out[dest]={'dimensions_m':list(o.dimensions),'triangles':sum(len(p.vertices)-2 for p in o.data.polygons),'uvs':[u.name for u in o.data.uv_layers],'materials':[m.name for m in o.data.materials]}
(root/'export_manifest.json').write_text(json.dumps(out,indent=2),encoding='utf8')
print('SHIP_EXPORT_COMPLETE',json.dumps(out))
