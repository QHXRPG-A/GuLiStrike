"""Preserve the complete source Spider geometry; paint continuous source parts only."""
import bpy,sys,json,math,time,hashlib,collections
import numpy as np
from pathlib import Path
from mathutils import Vector,Quaternion
sys.path.insert(0,'D:/UE5.7/test1/Scripts/Blender')
import mech_production_common as m
OUT=m.ROOT/'Production_v2_Spider';OUT.mkdir(exist_ok=True);m.OUT=OUT
bpy.context.window.scene=bpy.data.scenes['Mechs_Production_v1'];m.stage();m.visible(['SpiderMech'])
scene=bpy.context.scene;scene.cycles.samples=32
source=m.meshes('SpiderMech',True)[0];o=m.meshes('SpiderMech')[0]
assert [g.name for g in source.vertex_groups]==[g.name for g in o.vertex_groups]
o.data=source.data.copy();o.data.name='SpiderMech_OriginalGeometry_839778_Style_v2'
for modifier in list(o.modifiers):
    if modifier.type!='ARMATURE':o.modifiers.remove(modifier)
mesh=o.data
def linear_hex(h):
    vals=[int(h[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in vals)+(1,)
palette=[linear_hex('283135'),linear_hex('435360'),linear_hex('B98B35')]
# Source FBX vertex islands follow hard-surface and UV splits. The labels are
# analysis only: positions, indices, UVs, weights and imported normals stay intact.
parent=list(range(len(mesh.vertices)))
def find(a):
    while parent[a]!=a:parent[a]=parent[parent[a]];a=parent[a]
    return a
edges=np.empty(len(mesh.edges)*2,dtype=np.int32);mesh.edges.foreach_get('vertices',edges)
for a,b in edges.reshape((-1,2)):
    a,b=find(int(a)),find(int(b))
    if a!=b:parent[b]=a
roots=np.array([find(i) for i in range(len(mesh.vertices))],dtype=np.int32)
loop_verts=np.empty(len(mesh.loops),dtype=np.int32);mesh.loops.foreach_get('vertex_index',loop_verts)
starts=np.empty(len(mesh.polygons),dtype=np.int32);mesh.polygons.foreach_get('loop_start',starts)
counts=np.empty(len(mesh.polygons),dtype=np.int32);mesh.polygons.foreach_get('loop_total',counts)
matids=np.empty(len(mesh.polygons),dtype=np.int32);mesh.polygons.foreach_get('material_index',matids)
areas=np.empty(len(mesh.polygons),dtype=np.float64);mesh.polygons.foreach_get('area',areas)
uv=np.empty(len(mesh.loops)*2,dtype=np.float32);mesh.uv_layers[0].data.foreach_get('uv',uv);uv=uv.reshape((-1,2))
uv_centers=np.add.reduceat(uv,starts,axis=0)/counts[:,None]
rgb=np.zeros((len(mesh.polygons),3),dtype=np.float32)
for i,mat in enumerate(source.data.materials):
    image=mat.node_tree.nodes.get('SOURCE_BASE_COLOR').image
    pixels=np.empty(len(image.pixels),dtype=np.float32);image.pixels.foreach_get(pixels);pixels=pixels.reshape((image.size[1],image.size[0],4))
    faces=np.flatnonzero(matids==i)
    coords=uv_centers[faces]
    rgb[faces]=pixels[(np.mod(coords[:,1],1)*(image.size[1]-1)).astype(int),(np.mod(coords[:,0],1)*(image.size[0]-1)).astype(int),:3]
r,g,b=rgb.T
classes=np.zeros(len(mesh.polygons),dtype=np.int32)
classes[(b>r*1.12)&(b>.004)]=1
classes[(r>g*1.13)&(g>b*1.25)&(r>.012)]=2
face_roots=roots[loop_verts[starts]]
votes=np.zeros((len(mesh.vertices),3),dtype=np.float64)
np.add.at(votes,(face_roots,classes),areas)
chosen=votes.argmax(axis=1)
face_colors=np.array(palette,dtype=np.float32)[chosen[face_roots]]
colors=np.repeat(face_colors,counts,axis=0)
attr=mesh.color_attributes.get('SourceRegion_CleanPaint') or mesh.color_attributes.new(name='SourceRegion_CleanPaint',type='FLOAT_COLOR',domain='CORNER')
attr.data.foreach_set('color',colors.reshape(-1))
for i,old in enumerate(source.data.materials):
    mat=old.copy();mat.name='Spider_v2_OriginalParts_'+str(i);mesh.materials[i]=mat
    n=mat.node_tree.nodes;l=mat.node_tree.links;p=next(x for x in n if x.type=='BSDF_PRINCIPLED')
    for socket,value in [('Base Color',None),('Normal',None),('Metallic',.18),('Roughness',.58),('Specular IOR Level',.35)]:
        for link in list(p.inputs[socket].links):l.remove(link)
        if value is not None:p.inputs[socket].default_value=value
    color=n.new('ShaderNodeVertexColor');color.layer_name=attr.name;l.new(color.outputs['Color'],p.inputs['Base Color'])
    mat['source_shading_model']='Default Lit / Principled preview';mat['paint_method']='Area-dominant original texture palette for each intact source surface island'
o['style_version']='v2 original geometry, no reduction';o['source_triangles']=839778
for k in ('reduced_triangles','reduction_method','armor_cleanup'):
    if k in o:del o[k]
mesh.update();mesh.calc_loop_triangles()
def buffer_hash(data,prop,size,dtype):
    a=np.empty(size,dtype=dtype);data.foreach_get(prop,a);return hashlib.sha256(a.tobytes()).hexdigest()
fields=[('positions',mesh.vertices,source.data.vertices,'co',len(mesh.vertices)*3,np.float32),('loop_indices',mesh.loops,source.data.loops,'vertex_index',len(mesh.loops),np.int32),('uv',mesh.uv_layers[0].data,source.data.uv_layers[0].data,'uv',len(mesh.loops)*2,np.float32),('material_indices',mesh.polygons,source.data.polygons,'material_index',len(mesh.polygons),np.int32)]
equality={name:buffer_hash(a,prop,size,dtype)==buffer_hash(b,prop,size,dtype) for name,a,b,prop,size,dtype in fields}
assert all(equality.values())
report={'success':True,'geometry':'source mesh copy, no Decimate / weld / dissolve / remesh','triangles':len(mesh.loop_triangles),'vertices':len(mesh.vertices),'source_contract':equality,'parts':len(set(roots)),'palette_srgb':['283135','435360','B98B35'],'paint_faces':dict(collections.Counter(int(x) for x in chosen[face_roots])),'bone_count':len(o.parent.data.bones),'uv_layers':[u.name for u in mesh.uv_layers]}
(OUT/'source_style_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('STYLE_REPORT',json.dumps(report),flush=True)
scene.render.resolution_x=1400;scene.render.resolution_y=1200
cam=scene.camera;center=Vector((-.381766,.508270,1.572287));q=Quaternion((.64234668,.72117835,.19371103,.17253549))
cam.location=center+q@Vector((0,0,4.980691));cam.rotation_euler=q.to_euler();cam.data.type='PERSP';cam.data.lens=45
scene.render.filepath=str(OUT/'SpiderMech_OriginalStyle_Close.png');bpy.ops.render.render(write_still=True)
for view in ['Hero','Front','Side','Rear']:m.render(['SpiderMech'],'SpiderMech_OriginalStyle',view)
# Review scene shares the updated WORK mesh; retain the full editable source file
# as a new version so the ground-player light mech source is never overwritten.
bpy.context.window.scene=bpy.data.scenes['Review_SpiderMech']
bpy.context.view_layer.update()
for area in bpy.context.screen.areas:
    if area.type=='VIEW_3D':
        area.spaces.active.region_3d.view_location=Vector((0,0,1.8));area.spaces.active.region_3d.view_distance=9
        area.spaces.active.region_3d.view_rotation=Quaternion((.64234668,.72117835,.19371103,.17253549))
        area.spaces.active.shading.type='MATERIAL'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'SpiderMech_SourceStyle_v2.blend'),check_existing=False)
