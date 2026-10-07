"""Build actual single-section LODs, geometry-derived line atlas and inverted outline shells."""
import bpy,bmesh,json,math
import numpy as np
from pathlib import Path
from collections import Counter
from mathutils import Vector,Matrix
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004'); O=R/'Production_B_v1'
T=O/'Textures'; T.mkdir(exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(O/'ControlRigMech_B_v1_RegularCaps.blend'))
scene=bpy.context.scene; rig=bpy.data.objects['Armature']; col=bpy.data.collections['EDITABLE_MECHANICAL_PARTS']
setup=json.loads((R/'References_A_v2/reference_setup.json').read_text(encoding='utf-8'))
names=[b.name for b in rig.data.bones]
# Retain the complete source curvature on leg bearing lids and use a gentler reduction on their cores.
protected=[]
for ob in col.objects:
    bone=ob['source_bone']; helper=bpy.data.objects[ob['source_normal_helper']]
    colors=Counter(helper.data.materials[p.material_index].name for p in helper.data.polygons)
    dominant=colors.most_common(1)[0][0]
    dims=np.ptp(np.array([v.co[:] for v in helper.data.vertices]),axis=0)
    dec=next((m for m in ob.modifiers if m.type=='DECIMATE'),None)
    if dec and bone.startswith('leg_') and 'SandBeige' in dominant and max(dims)>.3:
        dec.ratio=1.; protected.append({'object':ob.name,'reason':'preserve complete circular bearing lid profile','ratio':dec.ratio})
    elif dec and bone.startswith('leg_') and 'DeepTealGray' in dominant and .5<max(dims)<1.1 and min(dims)>.3:
        dec.ratio=max(dec.ratio,.28); protected.append({'object':ob.name,'reason':'preserve circular joint core','ratio':dec.ratio})
    if ob.name.startswith(('Mech_foot_','Mech_toe_','Mech_toecord_')):
        conform=ob.modifiers.new('SourceFootSurfaceConform','SHRINKWRAP')
        conform.target=helper; conform.wrap_method='NEAREST_SURFACEPOINT'; conform.wrap_mode='ON_SURFACE'; conform.offset=0
        before=next(i for i,m in enumerate(ob.modifiers) if m.type in ('DATA_TRANSFER','WEIGHTED_NORMAL','ARMATURE'))
        ob.modifiers.move(len(ob.modifiers)-1,before)
bpy.context.view_layer.update()
export_col=bpy.data.collections.new('PRODUCTION_LODS_SINGLE_BODY_SECTION'); scene.collection.children.link(export_col)
depg=bpy.context.evaluated_depsgraph_get(); evaluated_parts=[]; partstats=[]
for ob in col.objects:
    ev=ob.evaluated_get(depg)
    mesh=bpy.data.meshes.new_from_object(ev,preserve_all_data_layers=True,depsgraph=depg)
    dst=bpy.data.objects.new('PartBake_'+ob.name,mesh); export_col.objects.link(dst)
    for n in names:
        if n not in dst.vertex_groups: dst.vertex_groups.new(name=n)
    # Geometry remains in world meter coordinates; evaluated source parent transform is identity.
    mesh.calc_loop_triangles()
    partstats.append({'object':ob.name,'triangles':len(mesh.loop_triangles),'source_bone':ob['source_bone'],
                      'circular_profile_segments':ob.get('circular_segments',None)})
    evaluated_parts.append(dst)
bpy.ops.object.select_all(action='DESELECT')
for ob in evaluated_parts: ob.select_set(True)
bpy.context.view_layer.objects.active=evaluated_parts[0]; bpy.ops.object.join()
body=bpy.context.object; body.name='ControlRigMech_LOD0_Body'
body.parent=rig; body.matrix_parent_inverse=Matrix.Identity(4)
body.data.calc_loop_triangles()
original_materials=list(body.data.materials)
face_colors=[tuple(original_materials[p.material_index].diffuse_color[:3]) for p in body.data.polygons]
face_emission=[1. if 'FunctionalLensSand' in original_materials[p.material_index].name else 0. for p in body.data.polygons]
palette_attribute=body.data.color_attributes.new(name='GuLi_PaletteLinear',type='FLOAT_COLOR',domain='CORNER')
palette_values=np.array([(*face_colors[p.index],1.) for p in body.data.polygons for _ in p.loop_indices],dtype=np.float32)
palette_attribute.data.foreach_set('color',palette_values.ravel())
# A fresh UV atlas is laid out across the complete body; all distance copies retain it.
while body.data.uv_layers: body.data.uv_layers.remove(body.data.uv_layers[0])
body.data.uv_layers.new(name='ControlRigMech_AtlasUV')
bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.uv.smart_project(angle_limit=math.radians(65),island_margin=.004,area_weight=.25,correct_aspect=True,scale_to_bounds=True)
bpy.ops.object.mode_set(mode='OBJECT')
uv=body.data.uv_layers.active.data; size=2048
base=np.zeros((size,size,4),dtype=np.float32); base[:,:,3]=1
filled=np.zeros((size,size),dtype=bool)
emission=np.zeros((size,size,4),dtype=np.float32); emission[:,:,3]=1
line=np.zeros((size,size,4),dtype=np.float32); line[:,:,3]=1

for tri in body.data.loop_triangles:
    xy=np.array([uv[i].uv[: ] for i in tri.loops],dtype=float)*(size-1)
    lo=np.maximum(np.floor(xy.min(axis=0)).astype(int)-1,0); hi=np.minimum(np.ceil(xy.max(axis=0)).astype(int)+1,size-1)
    if np.any(hi<lo): continue
    X,Y=np.meshgrid(np.arange(lo[0],hi[0]+1)+.5,np.arange(lo[1],hi[1]+1)+.5)
    den=(xy[1,1]-xy[2,1])*(xy[0,0]-xy[2,0])+(xy[2,0]-xy[1,0])*(xy[0,1]-xy[2,1])
    if abs(den)<1e-10: continue
    a=((xy[1,1]-xy[2,1])*(X-xy[2,0])+(xy[2,0]-xy[1,0])*(Y-xy[2,1]))/den
    b=((xy[2,1]-xy[0,1])*(X-xy[2,0])+(xy[0,0]-xy[2,0])*(Y-xy[2,1]))/den
    mask=(a>=-.01)&(b>=-.01)&(a+b<=1.01)
    block=base[lo[1]:hi[1]+1,lo[0]:hi[0]+1]; block[mask,:3]=face_colors[tri.polygon_index]
    filled[lo[1]:hi[1]+1,lo[0]:hi[0]+1][mask]=True
    emission[lo[1]:hi[1]+1,lo[0]:hi[0]+1][mask,:3]=face_emission[tri.polygon_index]

# Extend flat colors three pixels into chart gutters without baking lighting or shadows.
for _ in range(3):
    todo=~filled
    for dy,dx in ((0,1),(0,-1),(1,0),(-1,0)):
        neighbor=np.roll(filled,(dy,dx),(0,1)); take=todo&neighbor
        base[take,:3]=np.roll(base,(dy,dx),(0,1))[take,:3]
        emission[take,:3]=np.roll(emission,(dy,dx),(0,1))[take,:3]
        filled[take]=True; todo[take]=False

edge_faces={}
coordinate_keys=[tuple(round(c,5) for c in v.co) for v in body.data.vertices]
for p in body.data.polygons:
    loops=list(p.loop_indices)
    for k,li in enumerate(loops):
        a=body.data.loops[li].vertex_index; b=body.data.loops[loops[(k+1)%len(loops)]].vertex_index
        key=tuple(sorted((coordinate_keys[a],coordinate_keys[b])))
        edge_faces.setdefault(key,[]).append((p.index,li))
def stroke(a,b,world_length):
    a=np.array(a)* (size-1); b=np.array(b)*(size-1)
    halfwidth=max(.06,min(.65,float(np.linalg.norm(b-a))/max(world_length,.0001)*.004))
    lo=np.maximum(np.floor(np.minimum(a,b)-1.5).astype(int),0); hi=np.minimum(np.ceil(np.maximum(a,b)+1.5).astype(int),size-1)
    X,Y=np.meshgrid(np.arange(lo[0],hi[0]+1)+.5,np.arange(lo[1],hi[1]+1)+.5)
    d=b-a; length=float(d@d)
    f=np.clip(((X-a[0])*d[0]+(Y-a[1])*d[1])/max(length,1e-8),0,1)
    dist=np.sqrt((X-a[0]-f*d[0])**2+(Y-a[1]-f*d[1])**2)
    intensity=np.clip(halfwidth+.5-dist,0,1)
    block=line[lo[1]:hi[1]+1,lo[0]:hi[0]+1]
    block[:,:,:3]=np.maximum(block[:,:,:3],intensity[:,:,None])
selected_edges=0
for edge,adj in edge_faces.items():
    boundary=len(adj)==1
    sharp=boundary
    if len(adj)==2:
        p,q=(body.data.polygons[x[0]] for x in adj)
        sharp=p.normal.angle(q.normal,0)>math.radians(65) or face_colors[p.index]!=face_colors[q.index]
    length=(Vector(edge[0])-Vector(edge[1])).length
    # Bevel strips and sub-pixel fittings retain geometric shading; marking each
    # strip would turn a 2K atlas into noise rather than approved armor lines.
    usable=[(pi,li) for pi,li in adj if body.data.polygons[pi].area>.004]
    if not sharp or length<.045 or not usable: continue
    selected_edges+=1
    for pi,li in usable:
        p=body.data.polygons[pi]; loops=list(p.loop_indices); k=loops.index(li); next_li=loops[(k+1)%len(loops)]
        va=body.data.vertices[body.data.loops[li].vertex_index].co
        vb=body.data.vertices[body.data.loops[next_li].vertex_index].co
        stroke(uv[li].uv[:],uv[next_li].uv[:],(va-vb).length)

scene.view_settings.view_transform='Standard'; scene.view_settings.look='None'
scene.render.image_settings.file_format='PNG'; scene.render.image_settings.color_mode='RGBA'
def save_texture(name,pixels,color=True):
    im=bpy.data.images.new(name,width=size,height=size,alpha=True,float_buffer=True)
    im.colorspace_settings.name='Linear Rec.709' if color else 'Non-Color'; im.pixels.foreach_set(pixels.ravel()); im.update()
    path=T/(name+'.png')
    if color: im.save_render(filepath=str(path),scene=scene)
    else:
        im.filepath_raw=str(path); im.file_format='PNG'; im.save()
    image=bpy.data.images.load(str(path),check_existing=False)
    image.colorspace_settings.name='sRGB' if color else 'Non-Color'; image.pack()
    return image
base_image=save_texture('ControlRigMech_BaseColor_2K',base)
line_image=save_texture('ControlRigMech_InternalLineMask_2K',line,False)
emission_image=save_texture('ControlRigMech_FunctionalMask_2K',emission,False)
orm=np.ones((size,size,4),dtype=np.float32); orm[:,:,1]=.8; orm[:,:,2]=.05
save_texture('ControlRigMech_ORM_2K',orm,False)

mat=bpy.data.materials.new('M_ControlRigMech_ThreeTone_Atlas'); mat.use_nodes=True
nt=mat.node_tree; nt.nodes.clear()
out=nt.nodes.new('ShaderNodeOutputMaterial'); emit=nt.nodes.new('ShaderNodeEmission')
geom=nt.nodes.new('ShaderNodeNewGeometry'); dot=nt.nodes.new('ShaderNodeVectorMath'); dot.operation='DOT_PRODUCT'
dot.inputs[1].default_value=Vector((.35,-.55,.76)).normalized()
ramp=nt.nodes.new('ShaderNodeValToRGB'); ramp.color_ramp.interpolation='CONSTANT'
cr=ramp.color_ramp; cr.elements.remove(cr.elements[1])
for i,(pos,factor) in enumerate(((0,.42),(.12,.74),(.55,1))):
    e=cr.elements[0] if i==0 else cr.elements.new(pos); e.position=pos; e.color=(factor,factor,factor,1)
tex=nt.nodes.new('ShaderNodeTexImage'); tex.image=base_image; tex.interpolation='Linear'
palette_node=nt.nodes.new('ShaderNodeVertexColor'); palette_node.layer_name='GuLi_PaletteLinear'
mask=nt.nodes.new('ShaderNodeTexImage'); mask.image=line_image; mask.interpolation='Linear'
multiply=nt.nodes.new('ShaderNodeMixRGB'); multiply.blend_type='MULTIPLY'; multiply.inputs[0].default_value=1
strength=nt.nodes.new('ShaderNodeMath'); strength.operation='MULTIPLY'; strength.inputs[1].default_value=1; strength.name='InternalLineStrength'
ink=nt.nodes.new('ShaderNodeMixRGB'); ink.blend_type='MIX'
code='#1B2422'; rgb=[int(code[i:i+2],16)/255 for i in (1,3,5)]
linear=tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb)
ink.inputs[2].default_value=(*linear,1)
nt.links.new(geom.outputs['Normal'],dot.inputs[0]); nt.links.new(dot.outputs['Value'],ramp.inputs['Fac'])
nt.links.new(palette_node.outputs['Color'],multiply.inputs[1]); nt.links.new(ramp.outputs['Color'],multiply.inputs[2])
nt.links.new(mask.outputs['Color'],strength.inputs[0]); nt.links.new(strength.outputs[0],ink.inputs[0])
nt.links.new(multiply.outputs[0],ink.inputs[1]); nt.links.new(ink.outputs[0],emit.inputs['Color']); nt.links.new(emit.outputs[0],out.inputs['Surface'])
mat.diffuse_color=(.091,.198,.188,1)
body.data.materials.clear(); body.data.materials.append(mat)
for p in body.data.polygons: p.material_index=0
arm=body.modifiers.new('Original152BoneBinding','ARMATURE'); arm.object=rig
body['LOD']=0; body['screen_size']=1.; body['production_body']=True
col.hide_render=True; col.hide_viewport=True

outline_mat=bpy.data.materials.new('M_ControlRigMech_InvertedOutline'); outline_mat.use_nodes=True
outline_mat.use_backface_culling=True
ont=outline_mat.node_tree; ont.nodes.clear(); output=ont.nodes.new('ShaderNodeOutputMaterial'); ink_node=ont.nodes.new('ShaderNodeEmission')
ink_node.inputs['Color'].default_value=(*linear,1); ont.links.new(ink_node.outputs[0],output.inputs['Surface'])

def shell(source,lod,cap,thickness):
    # Separate source pieces remain intact in authoring; shell is only a silhouette approximation.
    ob=bpy.data.objects.new(f'ControlRigMech_LOD{lod}_Outline',source.data.copy()); export_col.objects.link(ob)
    for n in names:
        if n not in ob.vertex_groups: ob.vertex_groups.new(name=n)
    ob.parent=rig; ob.matrix_parent_inverse=Matrix.Identity(4)
    # The shell covers major pieces; microdetails retain the independent internal mask.
    bm=bmesh.new(); bm.from_mesh(ob.data)
    # Weld coincident shell corners only when the complete skin-weight signature
    # matches. This never joins mechanisms driven by different bones.
    layer=bm.verts.layers.deform.active
    weight_sets={}
    for v in bm.verts:
        key=tuple(sorted((i,round(w,6)) for i,w in v[layer].items()))
        weight_sets.setdefault(key,[]).append(v)
    for vertices in weight_sets.values():
        if len(vertices)>1: bmesh.ops.remove_doubles(bm,verts=vertices,dist=.000002)
    unvisited=set(bm.faces); delete=[]; threshold=(.18,.3,.80)[lod]
    while unvisited:
        first=unvisited.pop(); stack=[first]; group=[first]; vertices=set(first.verts)
        while stack:
            f=stack.pop()
            for e in f.edges:
                for neighbor in e.link_faces:
                    if neighbor in unvisited:
                        unvisited.remove(neighbor); stack.append(neighbor); group.append(neighbor); vertices.update(neighbor.verts)
        dims=[max(v.co[i] for v in vertices)-min(v.co[i] for v in vertices) for i in range(3)]
        if max(dims)<threshold: delete.extend(group)
    if delete: bmesh.ops.delete(bm,geom=delete,context='FACES')
    bm.to_mesh(ob.data); bm.free()
    while ob.data.uv_layers: ob.data.uv_layers.remove(ob.data.uv_layers[0])
    for attr in list(ob.data.color_attributes): ob.data.color_attributes.remove(attr)
    dec=ob.modifiers.new('BudgetedSilhouetteReduction','DECIMATE'); dec.decimate_type='COLLAPSE'
    ob.data.calc_loop_triangles(); count=len(ob.data.loop_triangles)
    dec.ratio=min(1,cap/max(count,1)*.94); dec.use_collapse_triangulate=True
    bpy.context.view_layer.objects.active=ob; bpy.ops.object.select_all(action='DESELECT'); ob.select_set(True)
    bpy.ops.object.modifier_apply(modifier=dec.name)
    wrap=ob.modifiers.new('ConformSilhouetteToBody','SHRINKWRAP'); wrap.target=source
    wrap.wrap_method='NEAREST_SURFACEPOINT'; wrap.wrap_mode='ON_SURFACE'; wrap.offset=0
    bpy.ops.object.modifier_apply(modifier=wrap.name)
    bm=bmesh.new(); bm.from_mesh(ob.data); bm.normal_update()
    for v in bm.verts: v.co+=v.normal*thickness
    bmesh.ops.reverse_faces(bm,faces=list(bm.faces)); bm.to_mesh(ob.data); bm.free()
    ob.data.materials.clear(); ob.data.materials.append(outline_mat)
    for p in ob.data.polygons: p.material_index=0
    skin=ob.modifiers.new('Original152BoneBinding','ARMATURE'); skin.object=rig
    ob['LOD']=lod; ob['production_outline']=True; ob['thickness_m']=thickness
    return ob

bodies=[body]; outlines=[shell(body,0,8000,.014)]
for lod,ratio,screen in ((1,.40,.40),(2,.14,.16),(3,.047,.06)):
    obj=bpy.data.objects.new(f'ControlRigMech_LOD{lod}_Body',body.data.copy()); export_col.objects.link(obj)
    for n in names:
        if n not in obj.vertex_groups: obj.vertex_groups.new(name=n)
    obj.parent=rig; obj.matrix_parent_inverse=Matrix.Identity(4)
    dec=obj.modifiers.new('DistanceLODReduction','DECIMATE'); dec.decimate_type='COLLAPSE'; dec.ratio=ratio; dec.use_collapse_triangulate=True
    bpy.context.view_layer.objects.active=obj; bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True)
    bpy.ops.object.modifier_apply(modifier=dec.name)
    skin=obj.modifiers.new('Original152BoneBinding','ARMATURE'); skin.object=rig
    lodmat=mat.copy(); lodmat.name=f'M_ControlRigMech_ThreeTone_LOD{lod}'
    lodmat.node_tree.nodes['InternalLineStrength'].inputs[1].default_value=0 if lod==3 else .6 if lod==2 else 1
    obj.data.materials.clear(); obj.data.materials.append(lodmat)
    obj['LOD']=lod; obj['screen_size']=screen; obj['production_body']=True
    bodies.append(obj)
    if lod<3: outlines.append(shell(obj,lod,(3000,700)[lod-1],.014 if lod==1 else .019))

lod_reports=[]
for i,ob in enumerate(bodies):
    ob.data.calc_loop_triangles(); outline=next((x for x in outlines if x['LOD']==i),None)
    if outline: outline.data.calc_loop_triangles()
    triangles=len(ob.data.loop_triangles); otris=len(outline.data.loop_triangles) if outline else 0
    caps=((32000,8000),(14000,3000),(5000,1000),(2000,0))[i]
    lod_reports.append({'lod':i,'body_triangles':triangles,'outline_triangles':otris,'total_triangles':triangles+otris,
                         'caps':{'body':caps[0],'outline':caps[1],'total':sum(caps)},
                         'difference':{'body':max(0,triangles-caps[0]),'outline':max(0,otris-caps[1]),'total':max(0,triangles+otris-sum(caps))},
                         'body_sections':len(ob.data.materials),'outline_sections':len(outline.data.materials) if outline else 0,
                         'screen_size':ob['screen_size'],'internal_lines':0 if i==3 else .6 if i==2 else 1})
    ob.hide_render=i!=0; ob.hide_set(i!=0)
    if outline: outline.hide_render=i!=0; outline.hide_set(i!=0)
scene.render.use_freestyle=False; scene.render.resolution_x=scene.render.resolution_y=2048
scene.render.resolution_percentage=100
for view,cam in setup['cameras'].items():
    scene.camera.location=cam['location_m']; scene.camera.rotation_euler=cam['rotation_radians']; scene.camera.data.ortho_scale=cam['ortho_scale_m']
    scene.render.filepath=str(O/f'ControlRigMech_B_v1_{view}.png'); bpy.ops.render.render(write_still=True)
hero=setup['cameras']['Hero']; scene.camera.location=hero['location_m']; scene.camera.rotation_euler=hero['rotation_radians']
scene['approved_reference']='A-v2'; scene['review_B']='pending'; scene['LOD_budget_result']='see production_render_report.json'
report={'version':'B-v1','stage':'actual_Blender_production_pending_B','approved_reference':'A-v2',
        'source_part_stats':partstats,'protected_round_parts':protected,'lods':lod_reports,
        'atlas_resolution':[2048,2048],'atlas_maps':['BaseColor','InternalLineMask','FunctionalMask','ORM'],
        'atlas_layout_shared_across_lods':True,'lighting_baked_in_BaseColor':False,
        'exact_palette_attribute':'GuLi_PaletteLinear; portable vertex color avoids subpixel atlas color bleed',
        'internal_mask_selected_structural_edges':selected_edges,'freestyle_used_in_production_render':False,
        'outline_method':'separate inverted-normal hull with backface culling',
        'source_actions':[(a.name,list(a.frame_range)) for a in bpy.data.actions],
        'source_bones':152,'UE_formal_assets_modified':False,'export_readback_pending_B':True}
(O/'production_render_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(O/'ControlRigMech_B_v1_Production.blend'))
print('PRODUCTION_ATLAS_LODS_OK',json.dumps(lod_reports),flush=True)
