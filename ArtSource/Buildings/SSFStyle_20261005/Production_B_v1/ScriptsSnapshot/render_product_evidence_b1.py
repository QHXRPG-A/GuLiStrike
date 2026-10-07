"""Render the actual frozen-in-memory Blender product, without Freestyle or image edits."""
import bpy,json,math,hashlib
from pathlib import Path
from mathutils import Vector
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1';V=O/'Renders';V.mkdir(exist_ok=True)
file=O/'SSF_Production_B_v1.blend';blend_sha=hashlib.sha256(file.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(file))
report=json.loads((O/'construction_report.json').read_text(encoding='utf8'))
refs={a['key']:a for a in json.loads((R/'References_A_v7/render_manifest.json').read_text(encoding='utf8'))['assets']}
records=[]
def show(asset,lod):
 for e in asset['lods']:
  for name in (e['body'],e['outline']):
   if not name:continue
   ob=bpy.data.objects[name];ob.hide_render=e['LOD']!=lod;ob.hide_set(e['LOD']!=lod)
def pose_camera(s,cam):s.camera.location=cam['camera_location_m'];s.camera.rotation_euler=cam['rotation_rad'];s.camera.data.ortho_scale=cam['ortho_scale_m']
gray=bpy.data.materials.new('B1_ActualGeometry_NormalDiagnostic');gray.use_nodes=True;nt=gray.node_tree;nt.nodes.clear()
out=nt.nodes.new('ShaderNodeOutputMaterial');geo=nt.nodes.new('ShaderNodeNewGeometry');dot=nt.nodes.new('ShaderNodeVectorMath');dot.operation='DOT_PRODUCT';dot.inputs[1].default_value=Vector((.35,-.55,.76)).normalized()
mul=nt.nodes.new('ShaderNodeMath');mul.operation='MULTIPLY_ADD';mul.inputs[1].default_value=.29;mul.inputs[2].default_value=.40;em=nt.nodes.new('ShaderNodeEmission')
nt.links.new(geo.outputs['Normal'],dot.inputs[0]);nt.links.new(dot.outputs['Value'],mul.inputs[0]);nt.links.new(mul.outputs[0],em.inputs['Color']);nt.links.new(em.outputs[0],out.inputs['Surface'])
for a in report['assets']:
 key=a['key'];s=bpy.data.scenes[a['scene']];bpy.context.window.scene=s;s.render.resolution_x=s.render.resolution_y=2048;s.render.resolution_percentage=100;s.render.use_freestyle=False
 show(a,0)
 for view,cam in refs[key]['views'].items():
  pose_camera(s,cam);s.render.filepath=str(V/f'{key}_{view}.png');bpy.ops.render.render(write_still=True)
  records.append({'asset':key,'view':view,'LOD':0,'file':str(Path(s.render.filepath).relative_to(O)),'resolution':[2048,2048],'blend_sha256':blend_sha,'camera_equal_approved_reference':True})
  print('SSF_PRODUCT_VIEW',key,view,flush=True)
 pose_camera(s,refs[key]['views']['Hero'])
 for lod in (1,2):
  show(a,lod);s.render.filepath=str(V/f'{key}_Hero_LOD{lod}.png');bpy.ops.render.render(write_still=True)
  records.append({'asset':key,'view':'Hero','LOD':lod,'file':str(Path(s.render.filepath).relative_to(O)),'resolution':[2048,2048],'blend_sha256':blend_sha,'camera_equal_approved_reference':True})
 show(a,0)
 for name in (a['lods'][0]['outline'],):
  if name:bpy.data.objects[name].hide_render=True
 s.view_layers[0].material_override=gray;s.render.filepath=str(V/f'{key}_GrayNormals.png');bpy.ops.render.render(write_still=True);s.view_layers[0].material_override=None;show(a,0)

s=bpy.data.scenes['Production_Assembly'];bpy.context.window.scene=s;data=report['assembly'];center=Vector(data['center_m']);extent=data['extent_m']
for view,direction in [('Hero',(.7,-1.2,1.4)),('Top',(0,0,1))]:
 s.camera.location=center+Vector(direction).normalized()*extent*4;s.camera.rotation_euler=(center-s.camera.location).to_track_quat('-Z','Y').to_euler();s.render.filepath=str(V/f'Assembly_{view}.png');bpy.ops.render.render(write_still=True)
 records.append({'view':'Assembly_'+view,'file':str(Path(s.render.filepath).relative_to(O)),'resolution':[4096,4096],'blend_sha256':blend_sha})
(O/'render_evidence_manifest.json').write_text(json.dumps({'source_blend_sha256':blend_sha,'freestyle':False,'actual_render_records':records},indent=2))
print('SSF_PRODUCT_ALL_STATIC_VIEWS_OK',len(records),flush=True)
