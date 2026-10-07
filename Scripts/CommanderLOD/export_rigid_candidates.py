"""Export each current rigid mesh tier independently; preserves pivot/part UVs."""
import bpy,json,sys,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART
rows=[]
for name in ['WM01','SweeperSummon']:
 bpy.ops.wm.open_mainfile(filepath=str(ART/name/(name+'_3Tier.blend')))
 for lod in range(3):
  # The editable scene displays the unit at its gameplay scale. Interchange
  # meshes must stay at authored size; Soldiers retains the presentation scale.
  root=bpy.data.objects[name+'_LOD'+str(lod)+'_Presentation']
  presentation_scale=list(root.scale);root.scale=(1,1,1);bpy.context.view_layer.update()
  bpy.ops.object.select_all(action='DESELECT')
  meshes=[o for o in bpy.data.collections[name+'_LOD'+str(lod)].objects if o.type=='MESH']
  for obj in meshes:obj.hide_set(False);obj.select_set(True);assert len(obj.data.uv_layers)==3
  bpy.context.view_layer.objects.active=meshes[0]
  target=ART/name/'FBX'/f'SM_{name}_Rigid_LOD{lod}.fbx'
  bpy.ops.export_scene.fbx(filepath=str(target),use_selection=True,object_types={'MESH'},axis_forward='-Y',axis_up='Z',
   apply_unit_scale=True,use_mesh_modifiers=False,bake_anim=False,use_tspace=True,colors_type='SRGB')
  rows.append(dict(name=name,lod=lod,file=str(target),authored_scale=1,
   presentation_scale=presentation_scale,sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
(ART/'Reports/rigid_exports.json').write_text(json.dumps(dict(success=True,lod_count=3,exports=rows),indent=2),encoding='utf8')
print('RIGID_TIER_EXPORTS',len(rows),flush=True)
