import bpy,json
from pathlib import Path
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parent
PLAN=json.loads((ROOT/'optimized-components.json').read_text(encoding='utf-8'))
SOURCE=Path(PLAN['source'])
all_results={}
for filename in (SOURCE.name,'quadruped-mech-vertex-animation-10k.blend','quadruped-mech-vertex-animation-10k-v2.blend'):
    if Path(bpy.data.filepath).name!=filename:bpy.ops.wm.open_mainfile(filepath=str(SOURCE.with_name(filename)),load_ui=False)
    s=next(s for s in bpy.data.scenes if s.name.startswith('00'));bpy.context.window.scene=s;bpy.context.view_layer.update()
    obj=next(o for o in s.objects if o.name.startswith('Body_Chassis') and '黑色' in o.name)
    coords=[obj.matrix_world@v.co for v in obj.data.vertices]
    parts=[p for p in PLAN['parts'] if p['object']==obj.name]
    groups={}
    if filename==SOURCE.name:
        for p in parts:groups[p['component']]=[tuple(obj.data.polygons[i].vertices) for i in p['source_polygon_ids']]
    else:
        labels=[a.value for a in obj.data.attributes['source_component'].data]
        for p in obj.data.polygons:groups.setdefault(labels[p.vertices[0]],[]).append(tuple(p.vertices))
    main=BVHTree.FromPolygons(coords,groups[0])
    results=[]
    for cid,faces in groups.items():
        if cid<2:continue
        tree=BVHTree.FromPolygons(coords,faces)
        points=[coords[i] for i in set(i for f in faces for i in f)]
        results.append({'component':cid,'minimum_vertex_surface_gap_m':min(main.find_nearest(p)[3] for p in points),'surface_intersections':len(main.overlap(tree))})
    all_results[filename]=results
(ROOT/'mount-contacts.json').write_text(json.dumps(all_results,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(all_results,ensure_ascii=False),flush=True)
