"""Bake a shared portable atlas per asset; use real shaders and skinned outline shells."""
import bpy,bmesh,json,math,sys,argparse
import numpy as np
from pathlib import Path
from collections import defaultdict
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1';T=O/'Textures';T.mkdir(exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--assets',default='');p.add_argument('--probe',action='store_true');args=p.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
assert not (O/'production_manifest.json').exists()
bpy.ops.wm.open_mainfile(filepath=str(O/('SSF_B1_GeometryProbe.blend' if args.probe else 'SSF_B1_Geometry.blend')))
report=json.loads((O/('geometry_probe_report.json' if args.probe else 'geometry_report.json')).read_text(encoding='utf8'))
reference=json.loads((R/'References_A_v7/render_manifest.json').read_text(encoding='utf8'));refs={a['key']:a for a in reference['assets']}
def linear(code):
 rgb=[int(code[i:i+2],16)/255 for i in (1,3,5)]
 return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb)
INK=linear('#1A182F')
def is_display(m):return bool(m and ('Logo' in m.name or (m.use_nodes and any(n.type=='BSDF_TRANSPARENT' for n in m.node_tree.nodes))))
def write_image(name,pixels,color,scene):
 h,w=pixels.shape[:2];im=bpy.data.images.new(name,width=w,height=h,alpha=True,float_buffer=True)
 im.colorspace_settings.name='Linear Rec.709' if color else 'Non-Color';im.pixels.foreach_set(pixels.ravel());im.update()
 path=T/(name+'.png')
 if color:im.save_render(filepath=str(path),scene=scene)
 else:im.filepath_raw=str(path);im.file_format='PNG';im.save()
 bpy.data.images.remove(im);dest=bpy.data.images.load(str(path),check_existing=False);dest.colorspace_settings.name='sRGB' if color else 'Non-Color';dest.pack();return dest
def stroke(canvas,a,b,width):
 size=canvas.shape[0];a=np.array(a)*(size-1);b=np.array(b)*(size-1)
 lo=np.maximum(np.floor(np.minimum(a,b)-width-1).astype(int),0);hi=np.minimum(np.ceil(np.maximum(a,b)+width+1).astype(int),size-1)
 if np.any(hi<lo):return
 X,Y=np.meshgrid(np.arange(lo[0],hi[0]+1)+.5,np.arange(lo[1],hi[1]+1)+.5);d=b-a
 f=np.clip(((X-a[0])*d[0]+(Y-a[1])*d[1])/max(float(d@d),1e-10),0,1)
 dist=np.sqrt((X-a[0]-f*d[0])**2+(Y-a[1]-f*d[1])**2)
 block=canvas[lo[1]:hi[1]+1,lo[0]:hi[0]+1];np.maximum(block,np.clip(width+.5-dist,0,1),out=block)
def atlas(body,key,scene,size):
 me=body.data;me.calc_loop_triangles();old=list(me.materials)
 original_uv=me.uv_layers['SourceUV'];source_uv=np.array([d.uv[:] for d in original_uv.data])
 display=[is_display(m) for m in old]
 face_display=np.array([display[p.material_index] for p in me.polygons],bool)
 original_colors=[tuple(old[p.material_index].diffuse_color[:3]) for p in me.polygons]
 bpy.ops.object.select_all(action='DESELECT');body.hide_set(False);body.select_set(True);bpy.context.view_layer.objects.active=body
 if 'SSF_AtlasUV' not in me.uv_layers:me.uv_layers.new(name='SSF_AtlasUV')
 me.uv_layers.active=me.uv_layers['SSF_AtlasUV']
 bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
 bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.007,area_weight=.7,correct_aspect=True,scale_to_bounds=True)
 bpy.ops.object.mode_set(mode='OBJECT');me.calc_loop_triangles();uv=me.uv_layers.active.data
 maskimages=[n.image for m in old if m and m.use_nodes for n in m.node_tree.nodes if n.type=='TEX_IMAGE' and n.image and 'LineMask' in n.image.name]
 mask=maskimages[0] if maskimages else None
 srcmask=np.array(mask.pixels[:],dtype=np.float32).reshape(mask.size[1],mask.size[0],4)[:,:,0] if mask else None
 base=np.zeros((size,size,4),np.float32);base[:,:,3]=1
 line=np.zeros((size,size),np.float32);structure=np.zeros((size,size),np.float32);functional=np.zeros((size,size,4),np.float32);functional[:,:,3]=1
 filled=np.zeros((size,size),bool);team=linear(refs[key]['theme']['Accent'])
 # Face colors remain unlit. The original source panel mask is transported
 # through the retained source UV; new geometry strokes are selected by angle.
 for tri in me.loop_triangles:
  if face_display[tri.polygon_index]:continue
  xy=np.array([uv[i].uv[:] for i in tri.loops])* (size-1)
  lo=np.maximum(np.floor(xy.min(0)).astype(int)-1,0);hi=np.minimum(np.ceil(xy.max(0)).astype(int)+1,size-1)
  if np.any(hi<lo):continue
  X,Y=np.meshgrid(np.arange(lo[0],hi[0]+1)+.5,np.arange(lo[1],hi[1]+1)+.5)
  den=(xy[1,1]-xy[2,1])*(xy[0,0]-xy[2,0])+(xy[2,0]-xy[1,0])*(xy[0,1]-xy[2,1])
  if abs(den)<1e-10:continue
  a=((xy[1,1]-xy[2,1])*(X-xy[2,0])+(xy[2,0]-xy[1,0])*(Y-xy[2,1]))/den
  b=((xy[2,1]-xy[0,1])*(X-xy[2,0])+(xy[0,0]-xy[2,0])*(Y-xy[2,1]))/den
  inside=(a>=-.001)&(b>=-.001)&(a+b<=1.001);color=original_colors[tri.polygon_index]
  base[lo[1]:hi[1]+1,lo[0]:hi[0]+1][inside,:3]=color
  filled[lo[1]:hi[1]+1,lo[0]:hi[0]+1][inside]=True
  block=functional[lo[1]:hi[1]+1,lo[0]:hi[0]+1]
  block[inside,2]=float(sum((color[i]-team[i])**2 for i in range(3))<1e-7)
  if key=='Lamp':block[inside,1]=block[inside,2]
  if srcmask is not None:
   coords=source_uv[list(tri.loops)];U=a*coords[0,0]+b*coords[1,0]+(1-a-b)*coords[2,0];V=a*coords[0,1]+b*coords[1,1]+(1-a-b)*coords[2,1]
   xx=np.clip(U*(srcmask.shape[1]-1),0,srcmask.shape[1]-1);yy=np.clip(V*(srcmask.shape[0]-1),0,srcmask.shape[0]-1)
   x0=xx.astype(int);y0=yy.astype(int);x1=np.minimum(x0+1,srcmask.shape[1]-1);y1=np.minimum(y0+1,srcmask.shape[0]-1)
   sample=(1-xx+x0)*(1-yy+y0)*srcmask[y0,x0]+(xx-x0)*(1-yy+y0)*srcmask[y0,x1]+(1-xx+x0)*(yy-y0)*srcmask[y1,x0]+(xx-x0)*(yy-y0)*srcmask[y1,x1]
   line[lo[1]:hi[1]+1,lo[0]:hi[0]+1][inside]=sample[inside]
  if key=='Floor':
   verts=np.array([me.vertices[i].co[:] for i in tri.vertices]);world=a[:,:,None]*verts[0]+b[:,:,None]*verts[1]+(1-a-b)[:,:,None]*verts[2]
   normal=me.polygons[tri.polygon_index].normal
   if normal.z>.8:
    distance=np.minimum(np.minimum(np.mod(world[:,:,0],6),6-np.mod(world[:,:,0],6)),np.minimum(np.mod(world[:,:,1],6),6-np.mod(world[:,:,1],6)))
    line[lo[1]:hi[1]+1,lo[0]:hi[0]+1][inside]=np.maximum(line[lo[1]:hi[1]+1,lo[0]:hi[0]+1][inside],np.clip((.026-distance)/.01,0,1)[inside])
 # Paint gutters carry palette and masks without pulling unrelated chart colors.
 for _ in range(4):
  todo=~filled
  for dy,dx in ((0,1),(0,-1),(1,0),(-1,0)):
   neighboring=np.roll(filled,(dy,dx),(0,1));take=todo&neighboring
   base[take,:3]=np.roll(base,(dy,dx),(0,1))[take,:3];functional[take,:3]=np.roll(functional,(dy,dx),(0,1))[take,:3]
   filled[take]=True;todo[take]=False
 edge_faces=defaultdict(list)
 groups=[tuple(sorted((g.group,round(g.weight,5)) for g in v.groups)) for v in me.vertices]
 coordinate=[(tuple(round(c,5) for c in v.co),groups[v.index]) for v in me.vertices]
 for p in me.polygons:
  if face_display[p.index]:continue
  loops=list(p.loop_indices)
  for i,li in enumerate(loops):
   nxt=loops[(i+1)%len(loops)];a,b=me.loops[li].vertex_index,me.loops[nxt].vertex_index
   edge_faces[tuple(sorted((coordinate[a],coordinate[b])))].append((p.index,li,nxt))
 world_width=refs[key]['views']['Hero']['ortho_scale_m']*2.2/2048
 strokes=0
 for edge,adj in edge_faces.items():
  length=(Vector(edge[0][0])-Vector(edge[1][0])).length
  if length<.002 or len(adj)>2:continue
  sharp=len(adj)==1
  if len(adj)==2:
   pa,pb=[me.polygons[x[0]] for x in adj];sharp=pa.normal.angle(pb.normal,0)>math.radians(45) or original_colors[pa.index]!=original_colors[pb.index]
  if not sharp:continue
  for pi,li,nxt in adj:
   if me.polygons[pi].area<world_width**2:continue
   ua,ub=uv[li].uv[:],uv[nxt].uv[:];density=np.linalg.norm(np.array(ua)-np.array(ub))*(size-1)/length
   stroke(structure,ua,ub,max(.15,min(2.5,density*world_width/2)));strokes+=1
 size_name=f'{size//1024}K';baseim=write_image(key+'_BaseColor_'+size_name,base,True,scene)
 mask_rgba=np.zeros((size,size,4),np.float32);mask_rgba[:,:,0]=line;mask_rgba[:,:,1]=structure;mask_rgba[:,:,3]=1
 lineim=write_image(key+'_InternalLineMask_'+size_name,mask_rgba,False,scene)
 funim=write_image(key+'_FunctionalTeamMask_'+size_name,functional,False,scene)
 orm=np.ones((size,size,4),np.float32);orm[:,:,1]=.8;orm[:,:,2]=.05
 ormim=write_image(key+'_ORM_'+size_name,orm,False,scene)
 return {'base':baseim,'line':lineim,'functional':funim,'orm':ormim},face_display,original_colors,strokes
def transfer_atlas(near,dst):
 nm=near.data;nm.calc_loop_triangles();dm=dst.data
 sourceUV=nm.uv_layers['SSF_AtlasUV'].data
 components=nm.attributes['SSF_ComponentID'];destcomponents=dm.attributes['SSF_ComponentID']
 bypart=defaultdict(list)
 for t in nm.loop_triangles:bypart[components.data[t.polygon_index].value].append(t)
 trees={k:BVHTree.FromPolygons([v.co for v in nm.vertices],[list(t.vertices) for t in tris],all_triangles=True) for k,tris in bypart.items()}
 uv=dm.uv_layers.get('SSF_AtlasUV') or dm.uv_layers.new(name='SSF_AtlasUV')
 for p in dm.polygons:
  k=destcomponents.data[p.index].value;tris=bypart[k];bv=trees[k]
  for li in p.loop_indices:
   point=dm.vertices[dm.loops[li].vertex_index].co
   probe=point*.998+p.center*.002;_,_,index,_=bv.find_nearest(probe);t=tris[index]
   v0,v1,v2=[nm.vertices[i].co for i in t.vertices];u=v1-v0;v=v2-v0;w=point-v0
   a,b,c,d,e=u.dot(u),u.dot(v),v.dot(v),w.dot(u),w.dot(v);den=a*c-b*b
   if abs(den)<1e-12:xy=sourceUV[t.loops[0]].uv.copy()
   else:
    f=(c*d-b*e)/den;g=(a*e-b*d)/den;xy=sourceUV[t.loops[0]].uv*(1-f-g)+sourceUV[t.loops[1]].uv*f+sourceUV[t.loops[2]].uv*g
   uv.data[li].uv=xy
 dm.uv_layers.active=uv
def body_material(key,images,lod):
 mat=bpy.data.materials.new(f'M_SSF_{key}_ThreeTone_LOD{lod}');mat.use_nodes=True;nt=mat.node_tree;nt.nodes.clear()
 output=nt.nodes.new('ShaderNodeOutputMaterial');em=nt.nodes.new('ShaderNodeEmission');geo=nt.nodes.new('ShaderNodeNewGeometry')
 dot=nt.nodes.new('ShaderNodeVectorMath');dot.operation='DOT_PRODUCT';dot.inputs[1].default_value=Vector((.35,-.55,.76)).normalized();dot.name='FixedArtLight'
 ramp=nt.nodes.new('ShaderNodeValToRGB');ramp.name='Approved_A7_ThreeTone';ramp.color_ramp.interpolation='CONSTANT';ramp.color_ramp.elements.remove(ramp.color_ramp.elements[1])
 for i,(pos,factor) in enumerate(((0,.40),(.38,.72),(.68,1.))):
  el=ramp.color_ramp.elements[0] if i==0 else ramp.color_ramp.elements.new(pos);el.position=pos;el.color=(factor,factor,factor,1)
 uv=nt.nodes.new('ShaderNodeUVMap');uv.uv_map='SSF_AtlasUV'
 tex=nt.nodes.new('ShaderNodeTexImage');tex.name='Portable_BaseColor';tex.image=images['base'];nt.links.new(uv.outputs[0],tex.inputs['Vector'])
 # The corner palette enforces the approved large color blocks across lower
 # LOD chart seams. BaseColor remains the portable texture equivalent.
 palette=nt.nodes.new('ShaderNodeVertexColor');palette.layer_name='SSF_PaletteLinear';palette.name='Approved_PaletteLinear'
 fun=nt.nodes.new('ShaderNodeTexImage');fun.image=images['functional'];nt.links.new(uv.outputs[0],fun.inputs['Vector']);separate=nt.nodes.new('ShaderNodeSeparateColor');nt.links.new(fun.outputs['Color'],separate.inputs[0])
 baseparam=nt.nodes.new('ShaderNodeRGB');baseparam.name='Base Color';baseparam.outputs[0].default_value=(1,1,1,1)
 teamparam=nt.nodes.new('ShaderNodeRGB');teamparam.name='Team Color';teamparam.outputs[0].default_value=(*linear(refs[key]['theme']['Accent']),1)
 # Corner palette ownership survives every distance mesh. Using a projected
 # lower-LOD UV to select the team region can sample an unrelated atlas chart.
 distance=nt.nodes.new('ShaderNodeVectorMath');distance.operation='DISTANCE';distance.inputs[1].default_value=linear(refs[key]['theme']['Accent']);nt.links.new(palette.outputs['Color'],distance.inputs[0])
 teammask=nt.nodes.new('ShaderNodeMath');teammask.operation='LESS_THAN';teammask.inputs[1].default_value=.0001;nt.links.new(distance.outputs['Value'],teammask.inputs[0])
 team=nt.nodes.new('ShaderNodeMixRGB');team.blend_type='MIX';nt.links.new(teammask.outputs[0],team.inputs[0]);nt.links.new(palette.outputs['Color'],team.inputs[1]);nt.links.new(teamparam.outputs[0],team.inputs[2])
 tint=nt.nodes.new('ShaderNodeMixRGB');tint.blend_type='MULTIPLY';tint.inputs[0].default_value=1;nt.links.new(team.outputs[0],tint.inputs[1]);nt.links.new(baseparam.outputs[0],tint.inputs[2])
 shade=nt.nodes.new('ShaderNodeMixRGB');shade.blend_type='MULTIPLY';shade.inputs[0].default_value=1;nt.links.new(tint.outputs[0],shade.inputs[1]);nt.links.new(ramp.outputs['Color'],shade.inputs[2])
 mask=nt.nodes.new('ShaderNodeTexImage');mask.name='Independent_InternalLineMask';mask.image=images['line'];nt.links.new(uv.outputs[0],mask.inputs['Vector'])
 linechannels=nt.nodes.new('ShaderNodeSeparateColor');nt.links.new(mask.outputs['Color'],linechannels.inputs[0])
 internal=nt.nodes.new('ShaderNodeMath');internal.operation='MULTIPLY';internal.name='SourcePanelLineStrength';internal.inputs[1].default_value=.65;nt.links.new(linechannels.outputs['Red'],internal.inputs[0])
 combine=nt.nodes.new('ShaderNodeMath');combine.operation='MAXIMUM';combine.name='FullStrengthMechanicalSeams';nt.links.new(internal.outputs[0],combine.inputs[0]);nt.links.new(linechannels.outputs['Green'],combine.inputs[1])
 strength=nt.nodes.new('ShaderNodeMath');strength.operation='MULTIPLY';strength.name='InternalLineStrength';strength.inputs[1].default_value=(1.,.70,0)[lod];nt.links.new(combine.outputs[0],strength.inputs[0])
 ink=nt.nodes.new('ShaderNodeMixRGB');ink.inputs[2].default_value=(*INK,1);nt.links.new(strength.outputs[0],ink.inputs[0]);nt.links.new(shade.outputs[0],ink.inputs[1]);nt.links.new(ink.outputs[0],em.inputs['Color'])
 nt.links.new(geo.outputs['Normal'],dot.inputs[0]);nt.links.new(dot.outputs['Value'],ramp.inputs[0]);nt.links.new(em.outputs[0],output.inputs['Surface'])
 mat['Base Color']=[1.,1.,1.];mat['Team Color']=refs[key]['theme']['Accent'];mat['reference']='A_v7';mat['shader']='Fixed world art light, 0.40/0.72/1.0, independent line mask';mat.diffuse_color=(*linear(refs[key]['theme']['Primary']),1)
 return mat
def outline(key,lod,record,scene,col,rig):
 cap=record['lods'][lod]['outline_cap']
 if cap==0:return None,{'outline_triangles':0,'outline_components':[]}
 choices=[]
 for part in record['parts']:
  if part['display'] or part['bone']=='SOURCE_FLEX':continue
  ob=bpy.data.objects[part['object']+(f'_LOD{lod}' if lod else '')];ev=ob.evaluated_get(bpy.context.evaluated_depsgraph_get());me=ev.to_mesh();me.calc_loop_triangles()
  bm=bmesh.new();bm.from_mesh(me);closed=all(e.is_manifold for e in bm.edges);bm.free()
  points=np.array([v.co[:] for v in me.vertices]);span=float(np.ptp(points,axis=0).max());area=sum(p.area for p in me.polygons);count=len(me.loop_triangles);ev.to_mesh_clear()
  if count>0:choices.append((area/max(count,1)**.55,area,count,ob))
 choices.sort(key=lambda x:x[0],reverse=True);chosen=[];used=0
 for score,area,count,ob in choices:
  if used+count<=cap:chosen.append(ob);used+=count
 if not chosen:return None,{'outline_triangles':0,'outline_components':[]}
 shells=[];names=[g.name for g in chosen[0].vertex_groups]
 for src in chosen:
  ev=src.evaluated_get(bpy.context.evaluated_depsgraph_get());me=bpy.data.meshes.new_from_object(ev,preserve_all_data_layers=True,depsgraph=bpy.context.evaluated_depsgraph_get())
  ob=bpy.data.objects.new('OutlinePart_'+src.name,me);col.objects.link(ob);ob.matrix_world=src.matrix_world.copy()
  for g in src.vertex_groups:ob.vertex_groups.new(name=g.name)
  shells.append(ob)
 bpy.ops.object.select_all(action='DESELECT')
 for ob in shells:ob.select_set(True)
 bpy.context.view_layer.objects.active=shells[0]
 if len(shells)>1:bpy.ops.object.join()
 ob=bpy.context.object;ob.name=key+f'_LOD{lod}_Outline';transform=ob.matrix_world.copy()
 for v in ob.data.vertices:v.co=transform@v.co
 ob.matrix_world=Matrix.Identity(4);width=refs[key]['views']['Hero']['ortho_scale_m']*2.2/2048*(1. if lod==0 else 1.25)
 mat=bpy.data.materials.get('M_SSF_Real_InvertedOutline')
 if not mat:
  mat=bpy.data.materials.new('M_SSF_Real_InvertedOutline');mat.use_nodes=True;mat.use_backface_culling=True
  nt=mat.node_tree;nt.nodes.clear();out=nt.nodes.new('ShaderNodeOutputMaterial');em=nt.nodes.new('ShaderNodeEmission');em.inputs[0].default_value=(*INK,1);nt.links.new(em.outputs[0],out.inputs['Surface'])
 ob.data.materials.clear();ob.data.materials.append(mat)
 for p in ob.data.polygons:p.material_index=0
 group=bpy.data.node_groups.new(f'SSF_{key}_EditableOutline_LOD{lod}','GeometryNodeTree');group.interface.new_socket(name='Geometry',in_out='INPUT',socket_type='NodeSocketGeometry')
 socket=group.interface.new_socket(name='WidthMeters',in_out='INPUT',socket_type='NodeSocketFloat');socket.default_value=width;group.interface.new_socket(name='Geometry',in_out='OUTPUT',socket_type='NodeSocketGeometry')
 inp=group.nodes.new('NodeGroupInput');out=group.nodes.new('NodeGroupOutput');normal=group.nodes.new('GeometryNodeInputNormal');scale=group.nodes.new('ShaderNodeVectorMath');scale.operation='SCALE';position=group.nodes.new('GeometryNodeSetPosition');flip=group.nodes.new('GeometryNodeFlipFaces')
 group.links.new(normal.outputs[0],scale.inputs[0]);group.links.new(inp.outputs['WidthMeters'],scale.inputs[3]);group.links.new(inp.outputs['Geometry'],position.inputs['Geometry']);group.links.new(scale.outputs[0],position.inputs['Offset']);group.links.new(position.outputs['Geometry'],flip.inputs['Mesh']);group.links.new(flip.outputs[0],out.inputs['Geometry'])
 mod=ob.modifiers.new('Editable_Outline_Width','NODES');mod.node_group=group
 if rig:
  ob.parent=rig;ob.matrix_parent_inverse=Matrix.Identity(4);arm=ob.modifiers.new('Original_SourceCompatible_Binding','ARMATURE');arm.object=rig
 ob['production_outline']=True;ob['LOD']=lod;ob['width_m']=width;ob.hide_render=lod!=0;ob.hide_set(lod!=0)
 return ob,{'outline_triangles':used,'outline_width_m':width,'outline_components':[s.name for s in chosen],
  'outline_scope':'Opaque major mechanical surfaces selected within cap; every body component remains intact'}

selected=set(args.assets.split(',')) if args.assets else None
for rec in report['assets']:
 key=rec['key']
 if selected and key not in selected:continue
 scene=bpy.data.scenes[rec['scene']];bpy.context.window.scene=scene;rig=bpy.data.objects[rec['rig']] if rec['rig'] else None
 if rig:rig.data.pose_position='REST'
 bodies=[bpy.data.objects[e['body']] for e in rec['lods']];near=bodies[0]
 if key=='Light':
  # Preserve the source single-sided translucent light shader and original UV;
  # the sprite has exactly two triangles in every LOD.
  for body in bodies:
   body.data.materials[0]=body.data.materials[0].copy();body.data.materials[0].name='M_SSF_Light_SourceTranslucent'
  rec['textures']={'source_display_images':[n.image.name for n in near.data.materials[0].node_tree.nodes if n.type=='TEX_IMAGE' and n.image]}
 else:
  size=4096 if key=='Floor' else 1024 if key in ('Drone','Lamp') else 2048
  source_materials=list(near.data.materials);imgs,near_display,colors,strokes=atlas(near,key,scene,size)
  for lod,body in enumerate(bodies):
   old=list(body.data.materials);display=[is_display(m) for m in old]
   flags=[display[p.material_index] for p in body.data.polygons]
   if lod:transfer_atlas(near,body)
   # Display polygons keep their original source UV via an explicit node input.
   dm=next((m for m in old if is_display(m)),None)
   if dm:
    dm=dm.copy();dm.name=f'M_SSF_{key}_SourceTranslucentDisplay_LOD{lod}'
    u=dm.node_tree.nodes.new('ShaderNodeUVMap');u.uv_map='SourceUV'
    for n in dm.node_tree.nodes:
     if n.type=='TEX_IMAGE':dm.node_tree.links.new(u.outputs[0],n.inputs['Vector'])
   bm=body_material(key,imgs,lod);body.data.materials.clear();body.data.materials.append(bm)
   if dm:body.data.materials.append(dm)
   for p,display_flag in zip(body.data.polygons,flags):p.material_index=1 if display_flag else 0
   rec['lods'][lod]['actual_body_sections']=1+int(dm is not None)
  rec['textures']={k:{'file':str(Path(im.filepath).relative_to(O)),'resolution':list(im.size),'colorspace':im.colorspace_settings.name} for k,im in imgs.items()};rec['structural_atlas_strokes']=strokes
 for lod in range(3):
  col=bpy.data.collections[key+f'_LOD{lod}_BAKED'];ob,stats=outline(key,lod,rec,scene,col,rig)
  rec['lods'][lod].update(stats);rec['lods'][lod]['outline']=ob.name if ob else None
  rec['lods'][lod]['total_triangles']=rec['lods'][lod]['body_triangles']+stats['outline_triangles']
  rec['lods'][lod]['total_cap']=rec['lods'][lod]['body_cap']+rec['lods'][lod]['outline_cap']
  rec['lods'][lod]['total_delta']=max(0,rec['lods'][lod]['total_triangles']-rec['lods'][lod]['total_cap'])
  rec['lods'][lod]['actual_total_sections']=rec['lods'][lod].get('actual_body_sections',1)+int(ob is not None)
 if rig:rig.data.pose_position='POSE'
 print('SSF_REAL_SHADER_READY',key,[(e['body_triangles'],e['outline_triangles'],e['actual_total_sections']) for e in rec['lods']],flush=True)
 # A low-resolution actual-model preview checks shader and silhouette before
 # producing the complete 2048px submission. Freestyle stays disabled.
 view=refs[key]['views']['Hero'];scene.camera.location=view['camera_location_m'];scene.camera.rotation_euler=view['rotation_rad'];scene.camera.data.ortho_scale=view['ortho_scale_m']
 scene.render.resolution_percentage=50;scene.render.filepath=str(O/(key+'_Working_Hero.png'));bpy.ops.render.render(write_still=True);scene.render.resolution_percentage=100
report['stage']='portable_atlas_actual_shaders_and_outlines';report['freestyle_used_for_product']=False;report['tone_factors']=[.40,.72,1.];report['tone_thresholds']=[.38,.68]
file=O/('SSF_B1_AtlasProbe.blend' if args.probe else 'SSF_B1_Atlas.blend');bpy.ops.wm.save_as_mainfile(filepath=str(file))
(O/('atlas_probe_report.json' if args.probe else 'atlas_report.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('SSF_NATIVE_ATLAS_SHADERS_OK',flush=True)
