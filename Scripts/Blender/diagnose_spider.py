"""Compare source and rejected Spider v1 in a separate Blender process."""
import bpy,bmesh,sys,json,math,collections,time
from pathlib import Path
from mathutils import Vector,Quaternion
sys.path.insert(0,'D:/UE5.7/test1/Scripts/Blender')
import mech_production_common as m
OUT=m.ROOT/'Production_v2_Spider'
OUT.mkdir(exist_ok=True)
m.OUT=OUT
bpy.context.window.scene=bpy.data.scenes['Mechs_Production_v1']
m.stage()
scene=bpy.context.scene
scene.cycles.samples=16
scene.render.resolution_x=1200;scene.render.resolution_y=1050
report={}
for source,label in [(True,'Source'),(False,'Rejected_v1')]:
    m.visible(['SpiderMech'],source)
    o=m.meshes('SpiderMech',source)[0]
    mesh=o.data
    bm=bmesh.new();bm.from_mesh(mesh)
    layer=bm.verts.layers.deform.active
    weight_counts=collections.Counter(len(v[layer]) for v in bm.verts)
    remaining=set(bm.verts);parts=[]
    while remaining:
        seed=remaining.pop();verts={seed};todo=[seed]
        while todo:
            v=todo.pop()
            for edge in v.link_edges:
                q=edge.other_vert(v)
                if q in remaining:remaining.remove(q);verts.add(q);todo.append(q)
        faces={f for v in verts for f in v.link_faces}
        groups=collections.Counter(max(v[layer].keys(),key=lambda i:v[layer][i]) for v in verts if len(v[layer]))
        parts.append({'vertices':len(verts),'faces':len(faces),'area':sum(f.calc_area() for f in faces),'lo':[min(v.co[i] for v in verts) for i in range(3)],'hi':[max(v.co[i] for v in verts) for i in range(3)],'bones':{o.vertex_groups[i].name:n for i,n in groups.items()}})
    report[label]={'vertices':len(bm.verts),'faces':len(bm.faces),'boundary_edges':sum(e.is_boundary for e in bm.edges),'nonmanifold_edges':sum(not e.is_manifold for e in bm.edges),'weight_counts':dict(weight_counts),'parts':sorted(parts,key=lambda x:x['vertices'],reverse=True),'matrix_world':[list(r) for r in o.matrix_world],'materials':[]}
    for mat in mesh.materials:
        p=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
        report[label]['materials'].append({'name':mat.name,'normal_links':len(p.inputs['Normal'].links),'color_links':[l.from_node.type for l in p.inputs['Base Color'].links]})
    bm.free()
(OUT/'diagnosis.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
gray=bpy.data.materials.new('Diagnosis_Gray');gray.use_nodes=True
p=next(n for n in gray.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
p.inputs['Base Color'].default_value=(.24,.26,.28,1);p.inputs['Roughness'].default_value=.72
scene.view_layers[0].material_override=gray
for source,label in [(True,'Source'),(False,'Rejected_v1')]:
    m.visible(['SpiderMech'],source)
    cam=scene.camera
    center=Vector((-.381766,.508270,1.572287));q=Quaternion((.64234668,.72117835,.19371103,.17253549))
    cam.location=center+q@Vector((0,0,4.980691));cam.rotation_euler=q.to_euler();cam.data.type='PERSP';cam.data.lens=45
    scene.render.filepath=str(OUT/(label+'_Gray_Close.png'));bpy.ops.render.render(write_still=True)
    m.render(['SpiderMech'],label+'_Gray','Hero',source)
scene.view_layers[0].material_override=None
(OUT/'diagnosis_complete.json').write_text(json.dumps({'success':True}),encoding='utf-8')
