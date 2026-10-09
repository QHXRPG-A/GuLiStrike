import bpy,json,sys,hashlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
import build_palette_only as c
base=c.R/'ArtSource/Buildings/SSFStyle_20261005'
paths=[base/'Production_B_v1/SSF_Production_B_v1.blend',base/'TeamPalette_B_v2_20261007/SSF_TeamPalette_B_v2.blend']
bpy.ops.wm.open_mainfile(filepath=str(paths[0]),load_ui=False)
before={f'{k}_LOD{n}_{part}':c.signature(bpy.data.objects[f'{k}_LOD{n}_{part}']) for k in c.SSF_KEYS for n in range(3) for part in (['Body','Outline'] if n<2 else ['Body'])}
bpy.ops.wm.open_mainfile(filepath=str(paths[1]),load_ui=False)
rows=[]
for name,digest in before.items():
    ob=bpy.data.objects['Blue_'+name]
    assert c.signature(ob)==digest,(name,'current B_v2 geometry differs')
    rows.append({'current_B2_mesh':ob.name,'canonical_B1_mesh':name,'identical_geometry_normals_UVs_weights_transform':True,'signature':digest})
(c.O/'Reports/ssf-current-geometry-parity.json').write_text(json.dumps({'current_approved_B2_source':str(paths[1]),'source_sha256':[hashlib.sha256(p.read_bytes()).hexdigest() for p in paths],'all_30_current_B2_meshes_identical_to_canonical_geometry':True,'checks':rows},indent=2),encoding='utf8')
print('SSF_CURRENT_B2_CANONICAL_GEOMETRY_30_IDENTICAL')
