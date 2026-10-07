"""Show actual production shader and skinned shell contributions, without saving edits."""
import bpy,json,hashlib
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1';file=O/'SSF_Production_B_v1.blend'
bpy.ops.wm.open_mainfile(filepath=str(file));d=json.loads((O/'construction_report.json').read_text(encoding='utf8'));a=next(a for a in d['assets'] if a['key']=='MilitaryFactory');ref=next(a for a in json.loads((R/'References_A_v7/render_manifest.json').read_text(encoding='utf8'))['assets'] if a['key']=='MilitaryFactory')
s=bpy.data.scenes[a['scene']];bpy.context.window.scene=s;e=a['lods'][0];body=bpy.data.objects[e['body']];outline=bpy.data.objects[e['outline']]
for lod in a['lods']:
 for name in (lod['body'],lod['outline']):
  if name:bpy.data.objects[name].hide_render=lod['LOD']!=0
cam=ref['views']['Hero'];s.camera.location=cam['camera_location_m'];s.camera.rotation_euler=cam['rotation_rad'];s.camera.data.ortho_scale=cam['ortho_scale_m'];s.render.resolution_percentage=100;s.render.use_freestyle=False
nt=body.data.materials[0].node_tree;ramp=nt.nodes['Approved_A7_ThreeTone'].color_ramp;strength=nt.nodes['InternalLineStrength'];original=[el.color[:] for el in ramp.elements];records=[]
for name,lines,tone in [('FlatColor',False,False),('LineAndOutline',True,False),('ThreeTone',False,True),('Combined',True,True)]:
 strength.inputs[1].default_value=float(lines);outline.hide_render=not lines
 for el,color in zip(ramp.elements,original):el.color=color if tone else (1,1,1,1)
 s.render.filepath=str(O/'Renders'/('Style_'+name+'.png'));bpy.ops.render.render(write_still=True);records.append({'mode':name,'lines':lines,'three_tone':tone,'file':'Renders/Style_'+name+'.png','real_native_geometry_and_shader':True})
(O/'style_breakdown_manifest.json').write_text(json.dumps({'source_blend_sha256':hashlib.sha256(file.read_bytes()).hexdigest(),'asset':'MilitaryFactory','freestyle':False,'records':records},indent=2),encoding='utf8')
print('SSF_ACTUAL_STYLE_BREAKDOWN_OK',flush=True)
