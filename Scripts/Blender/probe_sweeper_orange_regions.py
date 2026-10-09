"""Read the current three-LOD Sweeper UVs against its original color atlas."""
import bpy
import json
import numpy as np
from collections import Counter
from pathlib import Path

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT/'ArtSource/SweeperTeamColor_v1_20261008'
SOURCE = ROOT/'ArtSource/CommanderLOD_20261005/SweeperSummon/SweeperSummon_3Tier.blend'
ATLAS = ROOT/'ArtSource/LocalTeamColorReview_20261008/SourceTextures/T_Sweeper_BaseColor.png'
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
image = bpy.data.images.load(str(ATLAS), check_existing=False)
pixels = np.empty(image.size[0]*image.size[1]*4, dtype=np.float32)
image.pixels.foreach_get(pixels)
pixels = pixels.reshape(image.size[1], image.size[0], 4)
report = {'source': str(SOURCE), 'atlas': str(ATLAS), 'objects': []}
for obj in bpy.data.objects:
    if obj.type != 'MESH':
        continue
    mesh = obj.data
    uv = mesh.uv_layers[0].data
    colors = Counter()
    mixed = []
    orange = []
    for face in mesh.polygons:
        uvs = np.array([uv[i].uv[:] for i in face.loop_indices])
        center = uvs.mean(axis=0)
        samples = [center] + [p*.8+center*.2 for p in uvs]
        samples += [((uvs[i]+uvs[(i+1)%len(uvs)])*.5)*.8+center*.2 for i in range(len(uvs))]
        values = [pixels[min(image.size[1]-1,max(0,int(v*image.size[1]))),min(image.size[0]-1,max(0,int(u*image.size[0]))),:3] for u,v in samples]
        flags = [float(c[0]) > .35 and c[0] > c[1]*1.5 and c[0] > c[2]*2 for c in values]
        colors[tuple(int(round(float(c)*255)) for c in values[0])] += 1
        if all(flags):
            orange.append(face.index)
        elif any(flags):
            mixed.append({'face': face.index, 'colors': [[round(float(c),4) for c in v] for v in values]})
    bounds = [obj.matrix_world @ __import__('mathutils').Vector(c) for c in obj.bound_box]
    report['objects'].append({'name': obj.name, 'vertices': len(mesh.vertices), 'faces': len(mesh.polygons),
        'bounds': [[min(p[i] for p in bounds) for i in range(3)],[max(p[i] for p in bounds) for i in range(3)]],
        'world_matrix': [list(row) for row in obj.matrix_world], 'orange_faces': len(orange),
        'orange_indices': orange, 'mixed_faces': mixed, 'center_colors': colors.most_common(12),
        'uv_ranges': [[min(x.uv[i] for x in uv),max(x.uv[i] for x in uv)] for i in range(2)]})
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'orange-region-probe.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('SWEEPER_ORANGE_PROBE',json.dumps([{k:r[k] for k in ['name','bounds','faces','orange_faces','center_colors','uv_ranges']} | {'mixed': len(r['mixed_faces'])} for r in report['objects']]),flush=True)
