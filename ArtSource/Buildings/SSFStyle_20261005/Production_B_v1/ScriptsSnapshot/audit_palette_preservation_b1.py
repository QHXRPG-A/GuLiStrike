"""Verify real opaque corner colors and default parameters against approved A_v7 HEX."""
import bpy,json,hashlib
import numpy as np
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1';file=O/'SSF_Production_B_v1.blend'
bpy.ops.wm.open_mainfile(filepath=str(file));d=json.loads((O/'construction_report.json').read_text(encoding='utf8'));refs={a['key']:a for a in json.loads((R/'References_A_v7/render_manifest.json').read_text(encoding='utf8'))['assets']}
def linear(code):
 rgb=[int(code[i:i+2],16)/255 for i in (1,3,5)]
 return [v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb]
results=[]
for a in d['assets']:
 if a['key']=='Light':continue
 allowed=np.array([linear(code) for code in set(refs[a['key']]['material_palette'].values())])
 for e in a['lods']:
  m=bpy.data.objects[e['body']].data;pal=m.color_attributes['SSF_PaletteLinear'];values=np.array([pal.data[i].color[:3] for p in m.polygons if p.material_index==0 for i in p.loop_indices]);diff=np.min(np.linalg.norm(values[:,None,:]-allowed[None,:,:],axis=2),axis=1)
  assert np.max(diff)<.00001,(a['key'],e['LOD'],float(np.max(diff)))
  nodes=m.materials[0].node_tree.nodes;assert max(abs(nodes['Team Color'].outputs[0].default_value[i]-linear(refs[a['key']]['theme']['Accent'])[i]) for i in range(3))<.00001
  assert all(abs(nodes['Base Color'].outputs[0].default_value[i]-1)<1e-6 for i in range(3))
  results.append({'asset':a['key'],'LOD':e['LOD'],'opaque_corners_checked':len(values),'maximum_linear_RGB_distance_from_approved_colors':float(np.max(diff)),'Base_Color_and_Team_Color_default_values_preserved':True})
(O/'palette_preservation_validation.json').write_text(json.dumps({'source_blend_sha256':hashlib.sha256(file.read_bytes()).hexdigest(),'all_approved_opaque_colors_and_defaults_preserved':True,'checks':results},indent=2),encoding='utf8');print('SSF_ACTUAL_PALETTE_PRESERVATION_OK',max(r['maximum_linear_RGB_distance_from_approved_colors'] for r in results),flush=True)
