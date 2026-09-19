"""Source-derived hard-surface cleanup candidate; never overwrite the main blend."""
import bpy,bmesh,sys,math,json,collections
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.geometry import barycentric_transform
sys.path.insert(0,'D:/UE5.7/test1/Scripts/Blender')
import mech_production_common as m
bpy.context.window.scene=bpy.data.scenes['Mechs_Production_v1'];m.visible(['SpiderMech'])
o=m.meshes('SpiderMech')[0];original=o.data.copy();original.calc_loop_triangles()
tris=[tuple(t.vertices) for t in original.loop_triangles]
tree=BVHTree.FromPolygons([v.co for v in original.vertices],tris,all_triangles=True)
bm=bmesh.new();bm.from_mesh(o.data);bm.verts.ensure_lookup_table();bm.faces.ensure_lookup_table()
paint=bm.loops.layers.float_color['SourceRegion_CleanPaint'];uv=bm.loops.layers.uv.active;deform=bm.verts.layers.deform.active
remaining=set(bm.verts);parts=[]
while remaining:
    v=remaining.pop();group={v};todo=[v]
    while todo:
        v=todo.pop()
        for e in v.link_edges:
            q=e.other_vert(v)
            if q in remaining:remaining.remove(q);group.add(q);todo.append(q)
    faces=set(f for v in group for f in v.link_faces)
    if len(group)<35 or max(max(v.co[i] for v in group)-min(v.co[i] for v in group) for i in range(3))<40:continue
    # Preserve the open hydraulic struts below the upper armor; their concavities are functional.
    if min(v.co.z for v in group)<130:continue
    votes=collections.defaultdict(float)
    for f in faces:votes[tuple(round(c,5) for c in f.loops[0][paint])]+=f.calc_area()
    bluekeys=[c for c in votes if c[0]>.04 and c[2]>c[0]*1.4]
    if not bluekeys:continue
    dominant=max(bluekeys,key=votes.get)
    if sum(votes[c] for c in bluekeys)/sum(votes.values())<.23:continue
    groups=set(max(v[deform].keys(),key=lambda i:v[deform][i]) for v in group if len(v[deform]))
    if len(groups)>1:continue
    if min(v.co.x for v in group)<-10 and max(v.co.x for v in group)>10:
        cut=bmesh.ops.bisect_plane(bm,geom=list(group)+list(faces)+list({e for f in faces for e in f.edges}),dist=.0001,plane_co=Vector((0,0,0)),plane_no=Vector((1,0,0)),clear_outer=False,clear_inner=False)
        group.update(x for x in cut['geom_cut'] if isinstance(x,bmesh.types.BMVert))
        for sign in [-1,1]:
            half={v for v in group if v.is_valid and v.co.x*sign>=-.0001}
            halffaces={f for v in half for f in v.link_faces if all(q in half for q in f.verts)}
            if len(half)>4:parts.append((half,halffaces,dominant))
    else:parts.append((group,faces,dominant))
report=[]
for verts,faces,blue in parts:
    hull=bmesh.ops.convex_hull(bm,input=list(verts),use_existing_faces=False)
    hullfaces={x for x in hull['geom'] if isinstance(x,bmesh.types.BMFace)}
    bmesh.ops.delete(bm,geom=[f for f in faces if f.is_valid and f not in hullfaces],context='FACES_ONLY')
    interior=[v for v in verts if v.is_valid and not v.link_faces]
    if interior:bmesh.ops.delete(bm,geom=interior,context='VERTS')
    # Limited dissolve joins near-coplanar hull triangles into readable armor facets.
    edges=list({e for f in hullfaces if f.is_valid for e in f.edges})
    bmesh.ops.dissolve_limit(bm,angle_limit=math.radians(10),verts=[],edges=edges,delimit=set())
    kept={f for v in verts if v.is_valid for f in v.link_faces}
    for f in kept:
        near=tree.find_nearest(f.calc_center_median());tri=original.loop_triangles[near[2]];poly=original.polygons[tri.polygon_index];f.material_index=poly.material_index;f.smooth=False
        oldcolor=original.color_attributes['SourceRegion_CleanPaint'].data[poly.loop_start].color[:]
        facecolor=oldcolor if oldcolor[0]<.04 else blue
        for loop in f.loops:
            h=tree.find_nearest(loop.vert.co);t=original.loop_triangles[h[2]];coords=[original.vertices[i].co for i in t.vertices];uvs=[Vector((*original.uv_layers[0].data[i].uv,0)) for i in t.loops]
            mapped=barycentric_transform(h[0],*coords,*uvs);loop[uv].uv=mapped[:2];loop[paint]=facecolor
    report.append({'source_vertices':len(verts),'retained_vertices':sum(v.is_valid for v in verts),'armor_faces':len(kept)})
bm.normal_update();bm.to_mesh(o.data);bm.free();o.data.update();o.data.calc_loop_triangles()
for mod in o.modifiers:
    if mod.type=='WEIGHTED_NORMAL':mod.show_render=False;mod.show_viewport=False
o['armor_cleanup']='Convex envelope and 10 degree planar dissolve on single-bone blue armor islands only; original mesh vertices, weights and UV projection retained'
(m.OUT/'spider_armor_cleanup.json').write_text(json.dumps({'parts':report,'triangles':len(o.data.loop_triangles)},indent=2),encoding='utf-8')
bpy.data.libraries.write(str(m.OUT/'Construction/SpiderMech_ArmorCleanCandidate.blend'),{o,o.data},fake_user=True)
m.stage();m.render(['SpiderMech'],'SpiderMech_ArmorClean')
