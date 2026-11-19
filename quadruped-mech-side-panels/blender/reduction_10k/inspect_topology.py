import bpy,bmesh,collections,sys
s=next(s for s in bpy.data.scenes if s.name.startswith('00'))
bpy.context.window.scene=s
bpy.context.view_layer.update()
if '--repair-outline' in sys.argv:
    for o in s.objects:
        if not o.get('is_lineart'):continue
        m=next(m for m in o.modifiers if m.type=='SOLIDIFY')
        m.use_even_offset=False
        m.use_quality_normals=False
        m.thickness_clamp=0.25
        m.use_thickness_angle_clamp=True
    bpy.context.view_layer.update()
for o in s.objects:
    if o.type!='MESH' or o.get('is_lineart') or 'Studio' in o.name:continue
    bm=bmesh.new()
    bm.from_mesh(o.data)
    bm.verts.ensure_lookup_table()
    deg=sum(f.calc_area()<1e-12 for f in bm.faces)
    bad=sum(not e.is_manifold for e in bm.edges)
    if deg or bad:
        print(o.name,'degenerate',deg,'boundary',sum(e.is_boundary for e in bm.edges),'wire',sum(e.is_wire for e in bm.edges),'multi',sum(len(e.link_faces)>2 for e in bm.edges),'degenerate_components',dict(collections.Counter(o.data.attributes['source_component'].data[f.verts[0].index].value for f in bm.faces if f.calc_area()<1e-12)),flush=True)
    bm.free()
dg=bpy.context.evaluated_depsgraph_get()
bad=[]
for o in s.objects:
    if not o.get('is_lineart'):continue
    oe=o.evaluated_get(dg);be=o.parent.evaluated_get(dg)
    om=oe.to_mesh();bm=be.to_mesh()
    vals=[(a.index,a.normal.dot(b.normal),a.area,b.area) for a,b in zip(om.polygons,bm.polygons) if a.normal.dot(b.normal)>=-0.5]
    if vals:bad.append((o.name,vals[:8],len(vals)))
    oe.to_mesh_clear();be.to_mesh_clear()
print('WINDING_ISSUES',bad,flush=True)
