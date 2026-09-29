"""Read UE-exported render LODs, checking the GPU's rigid-part UV contract."""
import bpy,json,math
from mathutils import Vector, Matrix
from pathlib import Path
from collections import Counter
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/MechanicalAnimation_20260929'
report={'passed':True,'units':{}}
for unit in ['WarMachine','Sweeper']:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(OUT/unit/'UE_RenderLODs.fbx'),use_custom_normals=True)
    rows=[];spec=json.loads((OUT/unit/'manifest.json').read_text())
    for obj in bpy.context.scene.objects:
        if obj.type!='MESH':continue
        mesh=obj.data;counts=Counter();bad=0;seams=0
        if len(mesh.uv_layers)<3:
            rows.append({'object':obj.name,'uv_channels':len(mesh.uv_layers),'passed':False});report['passed']=False;continue
        for face in mesh.polygons:
            ids=[]
            pivots=[]
            for li in face.loop_indices:
                xy=mesh.uv_layers[1].data[li].uv;zp=mesh.uv_layers[2].data[li].uv
                part=1-zp.y;pid=round(part)
                if abs(part-pid)>.001 or pid not in (range(10) if unit=='WarMachine' else [0,2,6,7,8,9]):bad+=1
                ids.append(pid);pivots.append((xy.x,1-xy.y,zp.x));counts[pid]+=1
            if len(set(ids))!=1 or any(sum((a-b)**2 for a,b in zip(p,pivots[0]))>.001 for p in pivots):seams+=1
        row={'object':obj.name,'vertices':len(mesh.vertices),'faces':len(mesh.polygons),'uv_channels':len(mesh.uv_layers),
             'invalid_part_samples':bad,'triangles_crossing_parts':seams,'parts':dict(counts),'passed':bad==0 and seams==0}
        report['passed'] &= row['passed'];rows.append(row)
        if obj.name.endswith('LOD0'):
            points={};pivot_by_id={}
            for li,loop in enumerate(mesh.loops):
                xy=mesh.uv_layers[1].data[li].uv;zp=mesh.uv_layers[2].data[li].uv
                pid=round(1-zp.y);pivot_by_id[pid]=Vector((xy.x,1-xy.y,zp.x))
                points.setdefault(pid,{})[loop.vertex_index]=(obj.matrix_world@mesh.vertices[loop.vertex_index].co)*100
            neutral=[p for part in points.values() for p in part.values()]
            mins=Vector(tuple(min(p[i] for p in neutral) for i in range(3)))
            maxs=Vector(tuple(max(p[i] for p in neutral) for i in range(3)))
            low=mins.copy();high=maxs.copy();upper=Vector(spec['upper_pivot_cm'])
            for pid,vertices in points.items():
                pivot=pivot_by_id[pid]
                pitches=[-15,0,45 if unit=='WarMachine' else 60] if pid in [2,3,4,5] else [0]
                yaws=range(-180,181,15) if unit=='WarMachine' and pid in range(1,6) else [0]
                for yaw in yaws:
                    for pitch in pitches:
                        for phase in ([0,15,-15] if unit=='WarMachine' and pid>=6 else range(0,360,30) if pid>=6 else [0]):
                            for recoil in ([0,175] if unit=='WarMachine' and pid in [4,5] else [0]):
                                ry=Matrix.Rotation(math.radians(-pitch),3,'Y');rz=Matrix.Rotation(math.radians(yaw),3,'Z')
                                disc=Matrix.Rotation(math.radians(phase),3,'Y')
                                for original in vertices.values():
                                    p=original.copy();p.x-=recoil
                                    if pid in [2,3,4,5]:p=pivot+ry@(p-pivot)
                                    if unit=='WarMachine' and pid in range(1,6):p=upper+rz@(p-upper)
                                    if pid>=6:p=pivot+disc@(p-pivot)
                                    for axis in range(3):low[axis]=min(low[axis],p[axis]);high[axis]=max(high[axis],p[axis])
            row['neutral_bounds_cm']=[list(mins),list(maxs)]
            row['sampled_motion_bounds_cm']=[list(low),list(high)]
            row['minimum_bounds_extension_cm']={'negative':[max(0,mins[i]-low[i])+10 for i in range(3)],'positive':[max(0,high[i]-maxs[i])+10 for i in range(3)]}
    report['units'][unit]=rows
(OUT/'lod-verification.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report))
