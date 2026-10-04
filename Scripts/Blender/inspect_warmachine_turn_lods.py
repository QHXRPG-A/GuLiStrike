"""Inspect exported render-LOD UV partitions and pivots; no gameplay is executed."""
import bpy
import json
import math
from collections import Counter
from pathlib import Path
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'ArtSource/WarMachineTurn_20260930'
spec=json.loads((ROOT/'ArtSource/MechanicalAnimation_20260929/WarMachine/manifest.json').read_text(encoding='utf-8'))
s=spec['sockets_cm'];pivots={0:[0,0,0],1:s['Rigid_UpperYaw'],10:s['Rigid_UpperYaw'],11:s['Rigid_UpperYaw']}
for side in range(2):pivots[2+side]=pivots[4+side]=s['Rigid_GunPitch_'+str(side+1).zfill(2)]
for i,label in enumerate(('FL','FR','RL','RR')):
    pivots[6+i]=s['Rigid_Disc_'+label];pivots[12+i]=s['Rigid_LegRoot_'+label];pivots[16+i]=s['Rigid_LegEnd_'+label]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=(OUT/'UE_RenderLODs.fbx').as_posix(),use_custom_normals=True)
rows=[]
for obj in bpy.context.scene.objects:
    if obj.type!='MESH':continue
    mesh=obj.data;counts=Counter();invalid=0;crossing=0;max_error=0
    assert len(mesh.uv_layers)==3,(obj.name,len(mesh.uv_layers))
    for face in mesh.polygons:
        ids=set()
        for i in face.loop_indices:
            xy=mesh.uv_layers[1].data[i].uv;zp=mesh.uv_layers[2].data[i].uv
            encoded=1-zp.y;part=round(encoded);ids.add(part);counts[part]+=1
            if abs(encoded-part)>.001 or part not in pivots:invalid+=1;continue
            max_error=max(max_error,max(abs(a-b) for a,b in zip((xy.x,1-xy.y,zp.x),pivots[part])))
        if len(ids)!=1:crossing+=1
    rows.append({'object':obj.name,'vertices':len(mesh.vertices),'faces':len(mesh.polygons),'uv_channels':len(mesh.uv_layers),
        'part_corner_counts':dict(sorted(counts.items())),'invalid_parts':invalid,'cross_part_faces':crossing,
        'max_pivot_error_cm':max_error,'valid':invalid==0 and crossing==0 and max_error<.01 and set(counts)==set(range(20))})
report={'success':len(rows)==4 and all(row['valid'] for row in rows),'render_lods':rows,'skeletons':sum(o.type=='ARMATURE' for o in bpy.context.scene.objects)}
# Bound the local deformation by triangle inequalities, including unrestricted gun
# compensation. This pads only render proxies, never the mesh bounds used by gameplay.
bpy.ops.wm.open_mainfile(filepath=(ROOT/'ArtSource/MechanicalAnimation_20260929/WarMachine/WarMachine_RigidEditable.blend').as_posix())
upper=Vector(s['Rigid_UpperYaw']);maximum=0
for obj in bpy.context.scene.objects:
    if obj.type!='MESH':continue
    parts={loop.vertex_index:round(1-obj.data.uv_layers[2].data[loop.index].uv.y) for loop in obj.data.loops}
    for v in obj.data.vertices:
        p=(obj.matrix_world@v.co)*100;part=parts[v.index];original=(p-upper).length
        radius=original
        if 2<=part<=5:
            pivot=Vector(pivots[part])
            radius=(pivot-upper).length+(p-pivot).length+(175 if part>=4 else 0)
        elif 12<=part<=15:
            root=Vector(pivots[part]);radius=(root-upper).length+(p-root).length
        elif 6<=part<=9 or 16<=part<=19:
            leg=part-6 if part<=9 else part-16
            travel=2*(Vector(pivots[16+leg])-Vector(pivots[12+leg])).length*math.sin(math.radians(8)*.5)
            radius+=travel
            if part<=9:radius+=2*(p-Vector(pivots[part])).length*math.sin(math.radians(15)*.5)
        maximum=max(maximum,(radius+original+660)*.2)
report['render_padding']={'max_displacement_bound_cm':maximum,'material_limit_cm':2000,
    'model_scale':.2,'includes_hover_height_and_bob_cm':132,'mesh_gameplay_bounds_changed':False}
report['success'] &= maximum<2000
(OUT/'ue-lod-readback.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('WM_TURN_LOD_READBACK',json.dumps(report))
assert report['success'] and report['skeletons']==0
