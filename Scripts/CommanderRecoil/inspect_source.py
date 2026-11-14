"""Read formal FBX geometry/UV contracts in background Blender; no rendering."""
import bpy,json
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/CommanderRecoil_20261114'
report={}
for unit in ('Pioneer','WarMachine'):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(OUT/'Sources'/f'{unit}.fbx'),use_anim=False)
    rows=[]
    for o in bpy.data.objects:
        if o.type!='MESH':continue
        m=o.data;m.calc_loop_triangles();p=[o.matrix_world@Vector(v) for v in o.bound_box]
        rows.append({'name':o.name,'vertices':len(m.vertices),'triangles':len(m.loop_triangles),
          'matrix':[list(r) for r in o.matrix_world],
          'bounds':[[min(v[i] for v in p) for i in range(3)],[max(v[i] for v in p) for i in range(3)]],
          'uvs':[{'name':u.name,'min':[min(v.uv[i] for v in u.data) for i in range(2)],'max':[max(v.uv[i] for v in u.data) for i in range(2)]} for u in m.uv_layers],
          'colors':[(a.name,a.domain,a.data_type) for a in m.color_attributes],
          'materials':[x.name if x else None for x in m.materials]})
    report[unit]=rows
(OUT/'Sources/mesh-contract.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('RECOIL_SOURCE_CONTRACT',json.dumps(report),flush=True)
