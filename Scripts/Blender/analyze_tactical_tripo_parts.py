import bpy,bmesh,json,math,sys
from pathlib import Path
from mathutils import Matrix,Vector
root=Path('D:/UE5.7/test1/ArtSource/TacticalStyle_20260916/Tripo')
unit=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'Sweeper'
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(root/(unit+'_Multiview_Raw.glb')))
parts=[]
for o in list(bpy.context.scene.objects):
    if o.type!='MESH':continue
    o.data.transform(Matrix.Rotation(math.pi,4,'Z')@o.matrix_world);o.matrix_world=Matrix.Identity(4)
    bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.00005)
    seen=set()
    for v in bm.verts:
        if v in seen:continue
        stack=[v];seen.add(v);island=[]
        while stack:
            t=stack.pop();island.append(t)
            for e in t.link_edges:
                n=e.other_vert(t)
                if n not in seen:seen.add(n);stack.append(n)
        faces=set(f for vert in island for f in vert.link_faces)
        parts.append({'vertices':len(island),'triangles':sum(len(f.verts)-2 for f in faces),'bounds':[[min(v.co[i] for v in island) for i in range(3)],[max(v.co[i] for v in island) for i in range(3)]]})
    bm.free()
(root/(unit+'_islands.json')).write_text(json.dumps(parts,indent=2),encoding='utf8');print('PART_ANALYSIS',json.dumps(parts),flush=True)
