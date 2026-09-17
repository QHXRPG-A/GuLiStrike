"""Bake deliberately authored paint regions and real creases to one 2K atlas."""
import bpy,bmesh,json,math,sys
from pathlib import Path
from mathutils import Vector
import numpy as np
sys.path.insert(0,str(Path(__file__).parent))
import finish_tactical_ship_style as common
ROOT=Path('D:/UE5.7/test1/ArtSource/TacticalStyle_20260916/Production_AI')
report=json.loads((ROOT/'Sweeper_clean_report.json').read_text(encoding='utf8'))
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Sweeper_CleanParts.blend'))
parts=[o for o in bpy.context.scene.objects if o.type=='MESH'];scale=report['scale']
offset=np.array(report['source_offset'])
colors=['EC5D2D','F1E9D7','53697A','243544','FFC65C','8195A0']
palette=np.array([[int(c[i:i+2],16)/255 for i in (0,2,4)] for c in colors],dtype=np.float32)
for o in parts:
    lo,hi=common.bounds([o]);lo=np.array(lo)/scale-offset;hi=np.array(hi)/scale-offset;dim=hi-lo;center=(hi+lo)*.5
    tag=1 if dim[0]>.6 and dim[2]>.3 else 5
    if o.get('motion')=='Gun_Pitch':
        tag=4 if lo[0]>.15 else 3 if dim[1]<.035 else 2
    if o.name.startswith('Wheel_') or 'aerial' in o.name:tag=100
    tag=o.get('paint_override',tag)
    attr=o.data.attributes.new('paint_category','INT','FACE')
    for p,a in zip(o.data.polygons,attr.data):a.value=tag+p.material_index if tag==100 else tag
    group=o.vertex_groups.get(o.get('motion','Body')) or o.vertex_groups.new(name=o.get('motion','Body'))
    group.add(list(range(len(o.data.vertices))),1,'REPLACE')
    o.select_set(True)
bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join();mesh_obj=bpy.context.object;mesh_obj.name='SM_Sweeper_Crowd'
mesh=mesh_obj.data
while mesh.uv_layers:mesh.uv_layers.remove(mesh.uv_layers[0])
mesh.uv_layers.new(name='Atlas')
bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.uv.smart_project(angle_limit=math.radians(65),island_margin=.006,area_weight=0,scale_to_bounds=False);bpy.ops.object.mode_set(mode='OBJECT')
uv=mesh.uv_layers.active;mesh.calc_loop_triangles()
# Only geometrically meaningful creases get ink. Triangulation edges are ignored.
bm=bmesh.new();bm.from_mesh(mesh);bm.faces.ensure_lookup_table()
source_face=bm.faces.layers.int.new('source_polygon')
for f in bm.faces:f[source_face]=f.index
bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.0001)
bm.normal_update();edges={}
for edge in bm.edges:
    if len(edge.link_faces)!=2 or edge.calc_face_angle()<math.radians(60):continue
    if edge.calc_length()<scale*.045:continue
    if any(f.calc_area()<scale*scale*.00012 for f in edge.link_faces):continue
    a,b=[np.array(v.co,dtype=np.float32) for v in edge.verts]
    for face in edge.link_faces:edges.setdefault(face[source_face],[]).append((a,b))
bm.free()
SIZE=2048;atlas=np.ones((SIZE,SIZE,4),dtype=np.float32);atlas[...,:3]=palette[0]
filled=np.zeros((SIZE,SIZE),dtype=bool);label=np.zeros((SIZE,SIZE),dtype=np.int16)
def paint(points,tag,normal):
    p=points/scale-offset;x,y,z=p[:,0],np.abs(p[:,1]),p[:,2]
    c=np.zeros(len(points),dtype=np.int16)
    if tag>=100:return np.full(len(points),tag-100,dtype=np.int16)
    if tag==1:
        # Broad deck/nose panels and one coherent swept flank insert.
        c[(x<-.075)&(z>.070)&(y<.105)]=1
        c[(x>.20)&(z>-.130)&(y<.090)]=1
        band=x+.8*z
        c[(y>.10)&(band>-.18)&(band<-.070)&(z>-.19)]=1
        c[(y>.12)&(z<-.155)&(x>-.24)&(x<.10)]=1
        if normal[2]<-.6:c[:]=2
    elif tag==2:
        c[z>.072]=1;c[(x>.17)|(z<-.012)]=2
    elif tag==3:
        c[:]=2;r=np.sqrt((x-.047)**2+(z-.034)**2);c[(r>.023)&(r<.030)]=4
    elif tag==4:c[:]=2
    else:
        c[(y>.11)&(y<.17)]=1
        if normal[2]>.65:c[:]=1
        if normal[2]<-.6:c[:]=2
        if normal[0]>.85:
            c[(y>.11)&(y<.21)&(z>-.21)&(z<-.17)]=4
        c[(y<.1)&(z<.035)&(x<-.10)]=2
    return c
for tri in mesh.loop_triangles:
    coords=np.array([uv.data[i].uv[:] for i in tri.loops],dtype=np.float32)*SIZE
    verts=np.array([mesh.vertices[i].co[:] for i in tri.vertices],dtype=np.float32)
    a,b=coords[1]-coords[0],coords[2]-coords[0];det=a[0]*b[1]-a[1]*b[0]
    if abs(det)<1e-7:continue
    low=np.maximum(np.floor(coords.min(0)).astype(int),0);high=np.minimum(np.ceil(coords.max(0)).astype(int),SIZE)
    x0,y0=low;x1,y1=high
    if x1<=x0 or y1<=y0:continue
    dx=np.arange(x0,x1,dtype=np.float32)[None,:]+.5-coords[0,0];dy=np.arange(y0,y1,dtype=np.float32)[:,None]+.5-coords[0,1]
    s=(dx*b[1]-dy*b[0])/det;t=(a[0]*dy-a[1]*dx)/det;inside=(s>=0)&(t>=0)&(s+t<=1)
    if not inside.any():continue
    points=verts[0]+s[inside,None]*(verts[1]-verts[0])+t[inside,None]*(verts[2]-verts[0])
    tag=mesh.attributes['paint_category'].data[tri.polygon_index].value
    cls=paint(points,tag,mesh.polygons[tri.polygon_index].normal)
    values=palette[cls].copy()
    for start,end in edges.get(tri.polygon_index,[]):
        v=end-start;f=np.clip(np.sum((points-start)*v,axis=1)/np.dot(v,v),0,1)
        ink=np.linalg.norm(points-(start+f[:,None]*v),axis=1)<scale*.0014
        values[ink]=palette[3]
    atlas[y0:y1,x0:x1,:3][inside]=values;filled[y0:y1,x0:x1][inside]=True;label[y0:y1,x0:x1][inside]=cls
# A thin, consistent dark separator follows authored paint panel boundaries.
boundary=np.zeros_like(filled)
for axis,shift in [(0,1),(0,-1),(1,1),(1,-1)]:
    boundary|=filled&np.roll(filled,shift,axis)&(label!=np.roll(label,shift,axis))
atlas[boundary,:3]=palette[3]
for iteration in range(6):
    count=np.zeros((SIZE,SIZE),np.float32);values=np.zeros((SIZE,SIZE,3),np.float32)
    for axis,shift in [(0,1),(0,-1),(1,1),(1,-1)]:
        n=np.roll(filled,shift,axis);count+=n;values+=np.roll(atlas[...,:3],shift,axis)*n[...,None]
    new=(~filled)&(count>0);atlas[new,:3]=values[new]/count[new,None];filled|=new
img=bpy.data.images.new('T_Sweeper_Atlas',width=SIZE,height=SIZE,alpha=True);img.colorspace_settings.name='Non-Color';img.pixels.foreach_set(atlas.ravel());img.filepath_raw=str(ROOT/'T_Sweeper_Atlas.png');img.file_format='PNG';img.save()
img=bpy.data.images.load(str(ROOT/'T_Sweeper_Atlas.png'),check_existing=False);img.colorspace_settings.name='sRGB'
mat=bpy.data.materials.new('M_Sweeper_Production');mat.use_nodes=True;nodes,links=mat.node_tree.nodes,mat.node_tree.links;nodes.clear()
out=nodes.new('ShaderNodeOutputMaterial');em=nodes.new('ShaderNodeEmission');links.new(em.outputs[0],out.inputs[0]);tex=nodes.new('ShaderNodeTexImage');tex.image=img
diff=nodes.new('ShaderNodeBsdfDiffuse');diff.inputs[0].default_value=(1,1,1,1);rgb=nodes.new('ShaderNodeShaderToRGB');links.new(diff.outputs[0],rgb.inputs[0]);ramp=nodes.new('ShaderNodeValToRGB');ramp.color_ramp.interpolation='CONSTANT';ramp.color_ramp.elements.remove(ramp.color_ramp.elements[1])
for i,(pos,color) in enumerate([(0,(.52,.58,.66,1)),(.26,(.78,.83,.89,1)),(.56,(1,1,1,1))]):
    e=ramp.color_ramp.elements[0] if i==0 else ramp.color_ramp.elements.new(pos);e.position=pos;e.color=color
links.new(rgb.outputs[0],ramp.inputs[0]);mix=nodes.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=1;links.new(tex.outputs[0],mix.inputs[1]);links.new(ramp.outputs[0],mix.inputs[2]);links.new(mix.outputs[0],em.inputs[0])
mesh.materials.clear();mesh.materials.append(mat)
for p in mesh.polygons:p.material_index=0
common.OUT=ROOT
for view in ('three_quarter','front','side'):common.preview([mesh_obj],'Sweeper_Atlas',view)
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Sweeper_Production.blend'))
print('ATLAS_PRODUCTION_READY',common.tris(mesh_obj),flush=True)
