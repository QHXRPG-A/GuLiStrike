"""Read saved native sources in background Blender; never touch the interactive session."""
import bpy
import hashlib
import json
from pathlib import Path

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT/'ArtSource/SweeperTeamColor_v1_20261008'
OUT.mkdir(parents=True, exist_ok=True)
report = []
for file in [ROOT/'ArtSource/CommanderLOD_20261005/SweeperSummon/SweeperSummon_3Tier.blend',
             ROOT/'ArtSource/MechanicalAnimation_20260929/Sweeper/Sweeper_RigidEditable.blend']:
    bpy.ops.wm.open_mainfile(filepath=str(file))
    meshes = []
    for obj in bpy.data.objects:
        if obj.type != 'MESH':
            continue
        mesh = obj.data
        mesh.calc_loop_triangles()
        mats = []
        for material in mesh.materials:
            mats.append({'name': material.name, 'nodes': [{'type': n.type, 'name': n.name,
                'image': n.image.filepath if n.type=='TEX_IMAGE' and n.image else None}
                for n in material.node_tree.nodes] if material.use_nodes else []})
        meshes.append({'name': obj.name, 'vertices': len(mesh.vertices), 'faces': len(mesh.polygons),
                       'triangles': len(mesh.loop_triangles), 'uv': [u.name for u in mesh.uv_layers],
                       'colors': [{'name': a.name, 'domain': a.domain} for a in mesh.color_attributes],
                       'attributes': [{'name': a.name, 'domain': a.domain, 'type': a.data_type} for a in mesh.attributes],
                       'materials': mats, 'scale': list(obj.scale), 'location': list(obj.location),
                       'modifiers': [{'type': m.type, 'name': m.name} for m in obj.modifiers]})
    report.append({'file': str(file), 'sha256': hashlib.sha256(file.read_bytes()).hexdigest(),
                   'scenes': [s.name for s in bpy.data.scenes], 'objects': len(bpy.data.objects), 'meshes': meshes})
(OUT/'blender-source-inspection.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('SWEEPER_SOURCE_READ', json.dumps([{'file': r['file'], 'objects': r['objects'],
      'meshes': [{'name': m['name'], 'tris': m['triangles'], 'uv': m['uv'], 'materials': [x['name'] for x in m['materials']]} for m in r['meshes']]} for r in report]))
