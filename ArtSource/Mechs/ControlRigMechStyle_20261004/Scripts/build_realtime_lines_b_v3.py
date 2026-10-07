"""Actual three-tone structural-line shader and skinned outline; no Freestyle dependency."""
import bpy,bmesh,json,math,hashlib
import numpy as np
from pathlib import Path
from collections import defaultdict
from mathutils import Vector,Matrix
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004')
O=R/'Production_B_v3'; T=O/'Textures'; T.mkdir(parents=True,exist_ok=True)
source=R/'Production_B_v2/ControlRigMech_B_v2_Production.blend'
assert hashlib.sha256(source.read_bytes()).hexdigest()=='8e94a4547875ba54b731770b77297a229fbe36e53112a634624bb6502dee88ef'
bpy.ops.wm.open_mainfile(filepath=str(source))
s=bpy.data.scenes['PORTABLE_SHADER_LODS']; s.name='STYLE_REALTIME_B_v3'; s.use_fake_user=True
bpy.context.window.scene=s
rig=bpy.data.objects['Armature']; rig.animation_data.action=None; rig.data.pose_position='REST'
for p in rig.pose.bones: p.matrix_basis=Matrix.Identity(4)
s.render.engine='BLENDER_EEVEE'; s.render.use_freestyle=False
s.render.resolution_x=s.render.resolution_y=2048; s.render.resolution_percentage=100
setup=json.loads((R/'References_A_v2/reference_setup.json').read_text(encoding='utf-8'))
col=bpy.data.collections['PRODUCTION_LODS_SINGLE_BODY_SECTION']
report={'version':'B-v3','source':'frozen B-v2','freestyle_enabled':False,'shader_line_method':'selected structural edges, distance fields in two additional UV channels plus independent native-baked 2K mask','body_geometry_changed':False,'source_rig_animation_changed':False,'lods':[]}

def structural_distance_mesh(ob,lod):
    old=ob.data; old.calc_loop_triangles()
    coords=np.array([v.co[:] for v in old.vertices],dtype=np.float32)
    normals=np.array([n.vector[:] for n in old.corner_normals],dtype=np.float32)
    skins=[[(g.group,float(g.weight)) for g in v.groups] for v in old.vertices]
    group_names=[g.name for g in ob.vertex_groups]
    palette=old.color_attributes['GuLi_PaletteLinear']
    colors=np.array([d.color[:] for d in palette.data],dtype=np.float32)
    srcuv=np.array([d.uv[:] for d in old.uv_layers[0].data],dtype=np.float32)
    keys=[(tuple(round(float(c),5) for c in v.co),tuple((g.group,round(g.weight,6)) for g in v.groups)) for v in old.vertices]
    edges=defaultdict(list)
    for p in old.polygons:
        ids=list(p.vertices)
        for a,b in zip(ids,ids[1:]+ids[:1]): edges[tuple(sorted((keys[a],keys[b])))].append(p.index)
    selected=set(); kinds=defaultdict(int)
    for key,adj in edges.items():
        length=(Vector(key[0][0])-Vector(key[1][0])).length
        if length<.035 or max(old.polygons[i].area for i in adj)<.008: continue
        kind=None
        if len(adj)==1: kind='open_structural_boundary'
        elif len(adj)==2:
            a,b=(old.polygons[i] for i in adj)
            if np.max(np.abs(colors[a.loop_start,:3]-colors[b.loop_start,:3]))>.00001: kind='armor_color_boundary'
            elif a.normal.dot(b.normal)<math.cos(math.radians(35)): kind='mechanical_crease'
        if kind: selected.add(key); kinds[kind]+=1
    triangles=list(old.loop_triangles)
    loopids=np.array([t.loops[:] for t in triangles],dtype=np.int32).ravel()
    faces=[t.vertices[:] for t in triangles]
    distances=np.full((len(faces),3,3),8.,dtype=np.float32)
    selected_triangle_edges=0
    for ti,vs in enumerate(faces):
        a,b,c=(coords[v].astype(float) for v in vs)
        double_area=float(np.linalg.norm(np.cross(b-a,c-a)))
        for corner in range(3):
            x,y=vs[(corner+1)%3],vs[(corner+2)%3]
            if tuple(sorted((keys[x],keys[y]))) not in selected: continue
            height=double_area/max(float(np.linalg.norm(coords[x]-coords[y])),1e-12)
            distances[ti,:,corner]=0.; distances[ti,corner,corner]=height
            selected_triangle_edges+=1
    mesh=bpy.data.meshes.new(f'B_v3_LOD{lod}_SameSurface_StructuralUV')
    mesh.from_pydata(coords.tolist(),[],faces); mesh.update()
    for p in mesh.polygons: p.use_smooth=True
    # Isolate triangle smoothing fans before copying per-corner source normals.
    # Custom normals still preserve curved shading; this prevents non-unit fan encoding.
    sharp=mesh.attributes.new(name='sharp_edge',type='BOOLEAN',domain='EDGE')
    sharp.data.foreach_set('value',np.ones(len(mesh.edges),dtype=bool))
    mesh.normals_split_custom_set(normals[loopids].tolist())
    mainuv=mesh.uv_layers.new(name='ControlRigMech_AtlasUV')
    mainuv.data.foreach_set('uv',srcuv[loopids].ravel())
    lineuv=mesh.uv_layers.new(name='GuLi_StructuralDistance_RG')
    lineuv.data.foreach_set('uv',distances.reshape(-1,3)[:,:2].ravel())
    lineuv2=mesh.uv_layers.new(name='GuLi_StructuralDistance_B')
    lineuv2.data.foreach_set('uv',np.column_stack((distances.reshape(-1,3)[:,2],np.zeros(len(loopids)))).ravel())
    mesh.uv_layers.active_index=0
    for i,uv in enumerate(mesh.uv_layers): uv.active_render=i==0
    c=mesh.color_attributes.new(name='GuLi_PaletteLinear',type='FLOAT_COLOR',domain='CORNER')
    c.data.foreach_set('color',colors[loopids].ravel())
    ob.data=mesh
    while ob.vertex_groups: ob.vertex_groups.remove(ob.vertex_groups[-1])
    groups=[ob.vertex_groups.new(name=n) for n in group_names]
    ones=defaultdict(list)
    for vi,weights in enumerate(skins):
        for gi,w in weights:
            if w==1.: ones[gi].append(vi)
            else: groups[gi].add([vi],w,'REPLACE')
    for gi,ids in ones.items(): groups[gi].add(ids,1.,'REPLACE')
    mesh.calc_loop_triangles()
    assert len(mesh.loop_triangles)==len(triangles)
    assert np.array_equal(coords,np.array([v.co[:] for v in mesh.vertices],dtype=np.float32))
    assert [t.vertices[:] for t in mesh.loop_triangles]==faces
    assert np.array_equal(colors[loopids],np.array([d.color[:] for d in c.data],dtype=np.float32))
    return {'selected_structural_edges':len(selected),'selected_triangle_edges':selected_triangle_edges,'edge_kinds':dict(kinds),'body_triangles':len(faces),'body_vertices':len(coords),'vertex_positions_exactly_preserved':True,'triangular_surface_exactly_preserved':True,'three_uv_channels':True}

def mask_nodes(nt,width):
    uv=nt.nodes.new('ShaderNodeUVMap'); uv.uv_map='GuLi_StructuralDistance_RG'; uv.label='Structure distance: R/G in meters'
    uv2=nt.nodes.new('ShaderNodeUVMap'); uv2.uv_map='GuLi_StructuralDistance_B'; uv2.label='Structure distance: B in meters'
    sep=nt.nodes.new('ShaderNodeSeparateXYZ'); sep2=nt.nodes.new('ShaderNodeSeparateXYZ')
    nt.links.new(uv.outputs['UV'],sep.inputs[0]); nt.links.new(uv2.outputs['UV'],sep2.inputs[0])
    a=nt.nodes.new('ShaderNodeMath'); a.operation='MINIMUM'; b=nt.nodes.new('ShaderNodeMath'); b.operation='MINIMUM'
    nt.links.new(sep.outputs['X'],a.inputs[0]); nt.links.new(sep.outputs['Y'],a.inputs[1])
    nt.links.new(a.outputs[0],b.inputs[0]); nt.links.new(sep2.outputs['X'],b.inputs[1])
    mask=nt.nodes.new('ShaderNodeMapRange'); mask.name='StructuralLineWidthMeters'
    mask.clamp=True; mask.inputs['From Min'].default_value=width-.002
    mask.inputs['From Max'].default_value=width+.002
    mask.inputs['To Min'].default_value=1.; mask.inputs['To Max'].default_value=0.
    nt.links.new(b.outputs[0],mask.inputs['Value'])
    return mask.outputs[0]

body0=bpy.data.objects['ControlRigMech_LOD0_Body']
for lod in range(4):
    ob=bpy.data.objects[f'ControlRigMech_LOD{lod}_Body']
    entry=structural_distance_mesh(ob,lod); entry['lod']=lod; report['lods'].append(entry)
    print('STRUCTURE_FIELDS_READY',lod,entry,flush=True)

# Native unlit EMIT baking creates the independent 2K line mask; color atlas remains unlit.
maskimage=bpy.data.images.new('ControlRigMech_B_v3_InternalLineMask_2K',width=2048,height=2048,alpha=True,float_buffer=True)
maskimage.colorspace_settings.name='Non-Color'
bakemat=bpy.data.materials.new('TEMP_B_v3_NativeLineBake'); bakemat.use_nodes=True
nt=bakemat.node_tree; nt.nodes.clear(); out=nt.nodes.new('ShaderNodeOutputMaterial'); em=nt.nodes.new('ShaderNodeEmission')
mask=mask_nodes(nt,.010); nt.links.new(mask,em.inputs['Color']); nt.links.new(em.outputs[0],out.inputs['Surface'])
target=nt.nodes.new('ShaderNodeTexImage'); target.image=maskimage; nt.nodes.active=target
body0.data.materials.clear(); body0.data.materials.append(bakemat)
bpy.ops.object.select_all(action='DESELECT'); body0.hide_set(False); body0.select_set(True); bpy.context.view_layer.objects.active=body0
s.render.engine='CYCLES'; s.cycles.samples=1
bpy.ops.object.bake(type='EMIT',margin=2)
maskimage.filepath_raw=str(T/'ControlRigMech_InternalLineMask_2K.png'); maskimage.file_format='PNG'; maskimage.save(); maskimage.pack()
s.render.engine='BLENDER_EEVEE'
inkcode='#1B2422'; rgb=[int(inkcode[i:i+2],16)/255 for i in (1,3,5)]
ink=tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb)
for lod in range(4):
    ob=bpy.data.objects[f'ControlRigMech_LOD{lod}_Body']
    src=bpy.data.materials['M_ControlRigMech_ThreeTone_Atlas']
    mat=src.copy(); mat.name=f'M_ControlRigMech_B_v3_ThreeTone_StructuralLines_LOD{lod}'
    nt=mat.node_tree
    uv=nt.nodes.new('ShaderNodeUVMap'); uv.uv_map='ControlRigMech_AtlasUV'
    tex=next(n for n in nt.nodes if n.type=='TEX_IMAGE' and n.image and 'InternalLineMask' in n.image.name)
    tex.image=maskimage; tex.extension='EXTEND'; tex.interpolation='Linear'; nt.links.new(uv.outputs[0],tex.inputs['Vector'])
    strength=nt.nodes['InternalLineStrength']; strength.inputs[1].default_value=(1.,.85,.6,0.)[lod]
    analytic=mask_nodes(nt,(.010,.011,.012,.010)[lod])
    textureweight=nt.nodes.new('ShaderNodeMath'); textureweight.operation='MULTIPLY'; textureweight.inputs[1].default_value=.35
    nt.links.new(tex.outputs['Color'],textureweight.inputs[0])
    combine=nt.nodes.new('ShaderNodeMath'); combine.operation='MAXIMUM'; combine.label='Native atlas + precise structural field'
    nt.links.new(analytic,combine.inputs[0]); nt.links.new(textureweight.outputs[0],combine.inputs[1])
    nt.links.new(combine.outputs[0],strength.inputs[0])
    ob.data.materials.clear(); ob.data.materials.append(mat)
    for p in ob.data.polygons: p.material_index=0
    ob['line_method']='real-time structural fields + independent 2K atlas; no Freestyle'
    ob['B_version']='B-v3'

# Retain full silhouettes of substantial parts. Removing small outline-only fittings never deletes body geometry.
old_outlines=[o for o in list(col.objects) if o.get('production_outline')]
for ob in old_outlines:
    for c in list(ob.users_collection): c.objects.unlink(ob)
    ob.use_fake_user=True; ob.hide_render=True; ob.hide_viewport=True
outline_mat=bpy.data.materials['M_ControlRigMech_InvertedOutline'].copy(); outline_mat.name='M_ControlRigMech_B_v3_RealOutline'
outline_mat.use_backface_culling=True
group=bpy.data.node_groups.new('GuLi_B_v3_EditableSkinnedOutline','GeometryNodeTree')
group.interface.new_socket(name='Geometry',in_out='INPUT',socket_type='NodeSocketGeometry')
widthsocket=group.interface.new_socket(name='WidthMeters',in_out='INPUT',socket_type='NodeSocketFloat'); widthsocket.default_value=.018
group.interface.new_socket(name='Geometry',in_out='OUTPUT',socket_type='NodeSocketGeometry')
inp=group.nodes.new('NodeGroupInput'); out=group.nodes.new('NodeGroupOutput')
normal=group.nodes.new('GeometryNodeInputNormal'); scale=group.nodes.new('ShaderNodeVectorMath'); scale.operation='SCALE'
pos=group.nodes.new('GeometryNodeSetPosition'); flip=group.nodes.new('GeometryNodeFlipFaces')
group.links.new(normal.outputs[0],scale.inputs[0]); group.links.new(inp.outputs['WidthMeters'],scale.inputs[3])
group.links.new(inp.outputs['Geometry'],pos.inputs['Geometry']); group.links.new(scale.outputs[0],pos.inputs['Offset'])
group.links.new(pos.outputs['Geometry'],flip.inputs['Mesh']); group.links.new(flip.outputs['Mesh'],out.inputs['Geometry'])
for lod in range(3):
    body=bpy.data.objects[f'ControlRigMech_LOD{lod}_Body']
    ob=bpy.data.objects.new(f'ControlRigMech_B_v3_LOD{lod}_Outline',body.data.copy()); col.objects.link(ob)
    for g in body.vertex_groups: ob.vertex_groups.new(name=g.name)
    ob.parent=rig; ob.matrix_parent_inverse=Matrix.Identity(4)
    bm=bmesh.new(); bm.from_mesh(ob.data); deform=bm.verts.layers.deform.active
    bins=defaultdict(list)
    for v in bm.verts: bins[tuple(sorted((i,round(w,6)) for i,w in v[deform].items()))].append(v)
    for vs in bins.values():
        if len(vs)>1: bmesh.ops.remove_doubles(bm,verts=vs,dist=.000002)
    remaining=set(bm.faces); drop=[]; threshold=(.23,.36,.75)[lod]
    colorlayer=bm.loops.layers.float_color.get('GuLi_PaletteLinear')
    primary_srgb=((.3333333333,.4823529412,.4705882353),(.5568627451,.2274509804,.1647058824))
    primary_linear=[tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb) for rgb in primary_srgb]
    kept_components=0; skipped_components=0
    while remaining:
        f=remaining.pop(); todo=[f]; fs=[f]; vs=set(f.verts)
        while todo:
            x=todo.pop()
            for e in x.edges:
                for y in e.link_faces:
                    if y in remaining: remaining.remove(y); todo.append(y); fs.append(y); vs.update(y.verts)
        dims=[max(v.co[i] for v in vs)-min(v.co[i] for v in vs) for i in range(3)]
        areas=defaultdict(float)
        for face in fs:
            color=tuple(round(float(x),5) for x in face.loops[0][colorlayer][:3])
            areas[color]+=face.calc_area()
        dominant=max(areas,key=areas.get)
        primary=min(sum((dominant[i]-rgb[i])**2 for i in range(3)) for rgb in primary_linear)<.0001
        if max(dims)<threshold or not primary: drop.extend(fs); skipped_components+=1
        else: kept_components+=1
    if drop: bmesh.ops.delete(bm,geom=drop,context='FACES')
    bmesh.ops.dissolve_limit(bm,angle_limit=.17,verts=list(bm.verts),edges=list(bm.edges),use_dissolve_boundaries=False,delimit={'SHARP'})
    bm.to_mesh(ob.data); bm.free(); ob.data.update()
    while ob.data.uv_layers: ob.data.uv_layers.remove(ob.data.uv_layers[0])
    for a in list(ob.data.color_attributes): ob.data.color_attributes.remove(a)
    ob.data.materials.clear(); ob.data.materials.append(outline_mat)
    for p in ob.data.polygons: p.material_index=0
    localgroup=group.copy(); localgroup.name=f'GuLi_B_v3_EditableOutline_LOD{lod}'
    next(x for x in localgroup.interface.items_tree if x.item_type=='SOCKET' and x.name=='WidthMeters').default_value=(.018,.020,.025)[lod]
    gn=ob.modifiers.new('Editable_Outline_Width','NODES'); gn.node_group=localgroup
    arm=ob.modifiers.new('Original152BoneBinding','ARMATURE'); arm.object=rig
    ob['LOD']=lod; ob['production_outline']=True; ob['B_version']='B-v3'
    ob['width_m']=(.018,.020,.025)[lod]
    ev=ob.evaluated_get(bpy.context.evaluated_depsgraph_get()); m=bpy.data.meshes.new_from_object(ev)
    m.calc_loop_triangles(); report['lods'][lod].update(outline_triangles=len(m.loop_triangles),outline_kept_components=kept_components,outline_skipped_small_components=skipped_components)
    bpy.data.meshes.remove(m)

for lod in range(4):
    e=report['lods'][lod]; e.setdefault('outline_triangles',0); e['total_triangles']=e['body_triangles']+e['outline_triangles']
    cap=((32000,8000),(14000,3000),(5000,1000),(2000,0))[lod]
    e['body_cap'],e['outline_cap']=cap; e['body_delta']=max(0,e['body_triangles']-cap[0]); e['outline_delta']=max(0,e['outline_triangles']-cap[1]); e['total_cap']=sum(cap); e['total_delta']=max(0,e['total_triangles']-sum(cap))
for ob in col.objects:
    show=ob.get('LOD')==0
    ob.hide_render=not show; ob.hide_viewport=False; ob.hide_set(not show)
for view,cam in setup['cameras'].items():
    s.camera.location=cam['location_m']; s.camera.rotation_euler=cam['rotation_radians']; s.camera.data.ortho_scale=cam['ortho_scale_m']
    s.render.filepath=str(O/f'ControlRigMech_B_v3_{view}.png'); bpy.ops.render.render(write_still=True)
cam=setup['cameras']['Hero']; s.camera.location=cam['location_m']; s.camera.rotation_euler=cam['rotation_radians']; s.camera.data.ortho_scale=cam['ortho_scale_m']
report['three_tone_coefficients']=[.42,.74,1.]; report['ink_srgb']='#1B2422'; report['shader_render_is_actual_model_not_freestyle']=True
report['outline_scope']='Substantial teal/rust outer armor; small fittings, bearing caps and interior skeleton retain the actual surface structural-line shader. Body geometry is never deleted by outline filtering.'
rig.data.pose_position='POSE'
(O/'realtime_line_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(O/'ControlRigMech_B_v3_Production.blend'))
print('B_V3_REALTIME_LINES_BUILT',json.dumps(report['lods']),flush=True)
