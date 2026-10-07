"""Compare all retained source tiers and independent rigid FBX exports."""
import bpy,json,sys,ast,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART
tree=ast.parse((ROOT/'Scripts/CommanderLOD/prepare_blender.py').read_text(encoding='utf8'))
scope=globals()
for node in tree.body:
 if isinstance(node,ast.FunctionDef) and node.name in ['import_fbx','lod_index','signature']:
  exec(compile(ast.Module(body=[node],type_ignores=[]),'<geometry-helpers>','exec'),scope)
before=json.loads((ART/'Reports/formal_before.json').read_text(encoding='utf8'))
staged=json.loads((ART/'Reports/stage_native.json').read_text(encoding='utf8'))
rows=[]
for unit in staged['units']:
 if unit['components']:continue
 bpy.ops.wm.read_factory_settings(use_empty=True)
 original=next(r for r in before['units'] if r['name']==unit['name'])['meshes'][0]
 source=import_fbx(original['fbx']);candidate=import_fbx(unit['meshes'][0]['fbx'])
 for lod,source_lod in enumerate(original['selected_indices']):
  source_objects=[o for o in source if o.type=='MESH' and lod_index(o)==source_lod]
  target_objects=[o for o in candidate if o.type=='MESH' and lod_index(o)==lod]
  a=signature(source_objects);b=signature(target_objects);assert a==b,(unit['name'],lod,'selected geometry changed')
  row=dict(name=unit['name'],lod=lod,source_lod=source_lod,source_corner_sha256=a,candidate_corner_sha256=b,equal=True)
  if unit['name'] in ['WM01','SweeperSummon']:
   rigid=import_fbx(ART/unit['name']/'FBX'/f'SM_{unit["name"]}_Rigid_LOD{lod}.fbx')
   c=signature([o for o in rigid if o.type=='MESH'])
   assert c==b,(unit['name'],lod,'independent export changed authored geometry or UV')
   row['independent_export_corner_sha256']=c
   for ob in rigid:bpy.data.objects.remove(ob,do_unlink=True)
  rows.append(row)
(ART/'Reports/selected_geometry_readback.json').write_text(json.dumps(dict(success=True,lod_count=3,tiers=rows),indent=2),encoding='utf8')
print('SELECTED_SOURCE_GEOMETRY_EQUAL',len(rows),flush=True)
