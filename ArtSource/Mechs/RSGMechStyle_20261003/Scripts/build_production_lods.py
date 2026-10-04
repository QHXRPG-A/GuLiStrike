"""Four rigid single-section body LODs and budgeted inverted contour shells."""
import bpy
import bmesh
import json
import math
from collections import defaultdict
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.geometry import barycentric_transform

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT=ROOT/'Production_B_v1'
scene=bpy.context.scene
body0=bpy.data.objects['RSGMech_LOD0_Body']
rig=bpy.data.objects['Armature']
parts=json.loads((ROOT/'Baseline/source_connected_parts.json').read_text(encoding='utf-8'))
part_map={p['component']:p for p in parts}
setup=json.loads((ROOT/'References_A_v3/reference_setup.json').read_text(encoding='utf-8'))
swatches=json.loads((OUT/'atlas_build_report.json').read_text(encoding='utf-8'))['palette_swatch_uv']
colors={a['component']:a['base_color'] for a in setup['component_color_assignments']}
masters=list(bpy.data.collections['01_Editable_Mirrored_Parts'].objects)
pair_map={int(o['SourceComponent']):int(o['MirrorComponent']) for o in masters if o['MirrorComponent']>=0}
mirror_peers=set(pair_map.values())
mesh0=body0.data
ids=mesh0.attributes['RSG_Component'].data
faces_by_part=defaultdict(list)
for face in mesh0.polygons: faces_by_part[ids[face.index].value].append(face.index)
original_groups={g.index:g.name for g in body0.vertex_groups}
templates=[]
temporary=bpy.data.collections.new('99_LOD_Build_Temporary')
scene.collection.children.link(temporary)
for cid,face_ids in faces_by_part.items():
    if cid in mirror_peers: continue
    vertex_ids=sorted({vi for fi in face_ids for vi in mesh0.polygons[fi].vertices})
    remap={vi:i for i,vi in enumerate(vertex_ids)}
    data=bpy.data.meshes.new('LOD_Template_'+str(cid))
    data.from_pydata([mesh0.vertices[vi].co for vi in vertex_ids],[],
                     [[remap[vi] for vi in mesh0.polygons[fi].vertices] for fi in face_ids])
    uv=data.uv_layers.new(name='UV0_Atlas2K')
    for face,fi in zip(data.polygons,face_ids):
        for newli,oldli in zip(face.loop_indices,mesh0.polygons[fi].loop_indices):
            uv.data[newli].uv=mesh0.uv_layers.active.data[oldli].uv
        face.use_smooth=True
    obj=bpy.data.objects.new('LOD_Template_'+str(cid),data)
    temporary.objects.link(obj)
    bone=part_map[cid]['dominant_bone'] or 'Root_M'
    vg=obj.vertex_groups.new(name=bone); vg.add(list(range(len(data.vertices))),1,'REPLACE')
    if cid in pair_map:
        peer_bone=part_map[pair_map[cid]]['dominant_bone']
        if peer_bone!=bone: obj.vertex_groups.new(name=peer_bone)
    dec=obj.modifiers.new('Reduction','DECIMATE')
    dec.decimate_type='COLLAPSE'
    dec.use_collapse_triangulate=True
    if abs(part_map[cid]['center_m'][0])<.01:
        dec.use_symmetry=True
        dec.symmetry_axis='X'
    if cid in pair_map:
        mirror=obj.modifiers.new('Symmetry','MIRROR')
        mirror.use_axis=(True,False,False)
        mirror.use_mirror_merge=False
        mirror.use_mirror_vertex_groups=True
    data.calc_loop_triangles()
    obj['Triangles']=len(data.loop_triangles)
    obj['Component']=cid
    templates.append(obj)

def eligible(obj,lod,outline):
    cid=obj['Component']
    p=part_map[cid]
    size=max(p['dimensions_m'])
    if outline:
        if lod==2:
            return cid in (77,28551) or (size>=.75 and colors[cid] in ('WarmWhite','Coral','SkyBlue','PaleYellow'))
        return size>=.55 or cid in (77,28551)
    if lod==3:
        return size>=.55 or (size>=.30 and colors[cid] in ('WarmWhite','Coral','SkyBlue','PaleYellow')) or colors[cid]=='Amber'
    return True

def apply_factor(factor,lod,outline):
    selected=[]
    for obj in templates:
        if not eligible(obj,lod,outline): continue
        cid=obj['Component']
        p=part_map[cid]
        count=obj['Triangles']
        minimum=8 if lod==3 and not outline else 4 if lod>=2 or outline else 10
        ratio=max(factor,min(1,minimum/max(count,1)))
        if cid in (77,28551):
            curve_floor=80 if outline and lod==2 else 90 if lod==3 else 200 if lod==2 else 480
            ratio=max(ratio,min(1,curve_floor/max(count,1)))
        if p['center_m'][2]>5.5 and p['dimensions_m'][2]>1:
            ratio=max(ratio,min(1,12/max(count,1)))
        obj.modifiers['Reduction'].ratio=min(1,ratio)
        selected.append(obj)
    bpy.context.view_layer.update()
    return selected

def evaluated_count(objects):
    total=0
    dg=bpy.context.evaluated_depsgraph_get()
    for obj in objects:
        ev=obj.evaluated_get(dg); data=ev.to_mesh(); data.calc_loop_triangles()
        total+=len(data.loop_triangles); ev.to_mesh_clear()
    return total

def tuned(lod,outline,target):
    factor=target/(24000 if outline else 29756)
    lower=0; upper=1
    best=None
    for attempt in range(14):
        selected=apply_factor(factor,lod,outline)
        count=evaluated_count(selected)
        if count<=target:
            best=(factor,count)
            lower=factor
            if count>=target*.94: break
        else:
            upper=factor
        factor=(lower+upper)/2
    if best is None:
        selected=apply_factor(.0001,lod,outline)
        count=evaluated_count(selected)
        raise RuntimeError(f'Preserved geometry floor {count} exceeds target {target}; no major structure removed')
    selected=apply_factor(best[0],lod,outline)
    print(json.dumps({'LOD':lod,'outline':outline,'ratio':best[0],'triangles':best[1],'target':target}),flush=True)
    return selected,best

def new_collection(lod):
    if lod==0: return bpy.data.collections['03_LOD0_Review']
    c=bpy.data.collections.new(f'03_LOD{lod}_Review')
    scene.collection.children.link(c)
    return c

def assembled(objects,lod,outline,collection):
    vertices=[]; faces=[]; uvs=[]; weights=[]; components=[]
    dg=bpy.context.evaluated_depsgraph_get()
    for obj in objects:
        cid=obj['Component']
        peer=pair_map.get(cid,-1)
        ev=obj.evaluated_get(dg); data=ev.to_mesh()
        offset=len(vertices)
        names={g.index:g.name for g in obj.vertex_groups}
        for vertex in data.vertices:
            vertices.append(tuple(vertex.co))
            group=max(vertex.groups,key=lambda g:g.weight)
            weights.append(names[group.group])
        for face in data.polygons:
            faces.append([offset+vi for vi in face.vertices])
            uvs.append([tuple(data.uv_layers.active.data[li].uv) for li in face.loop_indices])
            x=sum(data.vertices[vi].co.x for vi in face.vertices)/len(face.vertices)
            components.append(peer if peer>=0 and x<0 else cid)
        ev.to_mesh_clear()
    mesh=bpy.data.meshes.new(f'RSGMech_LOD{lod}_'+('ContourGeometry' if outline else 'BodyGeometry'))
    mesh.from_pydata(vertices,[],faces)
    uv=mesh.uv_layers.new(name='UV0_Atlas2K')
    for face,face_uv in zip(mesh.polygons,uvs):
        face.use_smooth=True
        for li,coordinate in zip(face.loop_indices,face_uv): uv.data[li].uv=coordinate
    attr=mesh.attributes.new('RSG_Component','INT','FACE'); attr.data.foreach_set('value',components)
    obj=bpy.data.objects.new(f'RSGMech_LOD{lod}_'+('Contour' if outline else 'Body'),mesh)
    collection.objects.link(obj)
    by_bone=defaultdict(list)
    for vi,bone in enumerate(weights): by_bone[bone].append(vi)
    for bone,vis in by_bone.items():
        group=obj.vertex_groups.new(name=bone); group.add(vis,1,'REPLACE')
    arm=obj.modifiers.new('Original_44_Bone_Rigid_Binding','ARMATURE'); arm.object=rig
    obj['LOD']=lod; obj['ApprovedReference']='A-v3'
    obj['InternalLinesStrength']=(1,.85,.45,0)[lod]
    if outline:
        mesh.update()
        # Inverted hull, expanded by the average vertex normal.
        width=(.014,.019,.025,0)[lod]
        ns=[v.normal.copy() for v in mesh.vertices]
        for vertex,normal in zip(mesh.vertices,ns): vertex.co+=normal*width
        bm=bmesh.new(); bm.from_mesh(mesh)
        bmesh.ops.reverse_faces(bm,faces=list(bm.faces)); bm.to_mesh(mesh); bm.free()
        mesh.materials.append(ink)
        obj['OutlineWidthMeters']=width
    else:
        material=body0.data.materials[0].copy()
        material.name=f'M_RSGMech_Toon3_LOD{lod}'
        material.node_tree.nodes['InternalLineStrength'].outputs[0].default_value=(1,.85,.45,0)[lod]
        mesh.materials.append(material)
        palette_uv=mesh.uv_layers.new(name='UV_PaletteBase')
        component_attr=mesh.attributes['RSG_Component'].data
        for face in mesh.polygons:
            cid=component_attr[face.index].value
            for li in face.loop_indices:
                palette_uv.data[li].uv=uv.data[li].uv if cid==28551 else swatches[colors[cid]]
                if lod>=2 and cid not in (77,28551):
                    # Newly simplified surfaces have no corresponding fine line
                    # placement; keep their actual contour and primary color.
                    uv.data[li].uv=swatches[colors[cid]]
        uv_node=material.node_tree.nodes.new('ShaderNodeUVMap')
        uv_node.uv_map='UV_PaletteBase'
        material.node_tree.links.new(uv_node.outputs['UV'],material.node_tree.nodes['BaseColor_2K'].inputs['Vector'])
    mesh.calc_loop_triangles()
    return obj,len(mesh.loop_triangles),len(set(components))

def near_contour(collection):
    # Exact armor surfaces avoid hull protrusions through concave assembly.
    vertices=[]; faces=[]; weights=[]; component_ids=[]
    for template in templates:
        cid=template['Component']
        p=part_map[cid]
        if max(p['dimensions_m'])<.55 or colors[cid] not in ('WarmWhite','Coral','SkyBlue','PaleYellow'):
            continue
        if cid in (55006,55172,57164,54324):
            continue  # Circular receiver details use their baked structural mask.
        points=[v.co.copy() for v in template.data.vertices]
        polygons=[list(p.vertices) for p in template.data.polygons]
        copies=[(cid,1)]
        if cid in pair_map: copies.append((pair_map[cid],-1))
        for component,side in copies:
            offset=len(vertices)
            vertices.extend([(v.x*side,v.y,v.z) for v in points])
            bone=part_map[component]['dominant_bone'] or 'Root_M'
            weights.extend([bone]*len(points))
            for poly in polygons:
                face=[offset+vi for vi in poly]
                faces.append(face[::-1] if side<0 else face)
                component_ids.append(component)
    mesh=bpy.data.meshes.new('RSGMech_LOD0_ExactArmorContour')
    mesh.from_pydata(vertices,[],faces)
    mesh.update()
    normals=[v.normal.copy() for v in mesh.vertices]
    for vertex,normal in zip(mesh.vertices,normals): vertex.co+=normal*.014
    bm=bmesh.new(); bm.from_mesh(mesh)
    bmesh.ops.reverse_faces(bm,faces=list(bm.faces))
    bmesh.ops.triangulate(bm,faces=list(bm.faces))
    bm.to_mesh(mesh); bm.free()
    mesh.materials.append(ink)
    obj=bpy.data.objects.new('RSGMech_LOD0_Contour',mesh); collection.objects.link(obj)
    groups=defaultdict(list)
    for vi,bone in enumerate(weights): groups[bone].append(vi)
    for bone,vis in groups.items():
        vg=obj.vertex_groups.new(name=bone); vg.add(vis,1,'REPLACE')
    arm=obj.modifiers.new('Original_44_Bone_Rigid_Binding','ARMATURE'); arm.object=rig
    obj['LOD']=0; obj['ApprovedReference']='A-v3'
    obj['OutlineWidthMeters']=.014
    obj['Construction']='Exact original LOD0 major armor surfaces'
    mesh.calc_loop_triangles()
    print(json.dumps({'LOD':0,'exact_armor_contour_triangles':len(mesh.loop_triangles)}),flush=True)
    return obj,len(mesh.loop_triangles),len(set(component_ids))

def closed_far_proxies(lod):
    """Closed convex mechanical parts prevent thin armor collapsing into shards."""
    for obj in templates:
        cid=obj['Component']
        if not eligible(obj,lod,False) or cid in (77,28551) or obj.get('FarClosedProxy'): continue
        data=obj.data
        if len(data.vertices)<4 or min(part_map[cid]['dimensions_m'])<.0001: continue
        original_points=[v.co.copy() for v in data.vertices]
        original_faces=[list(p.vertices) for p in data.polygons]
        bvh=BVHTree.FromPolygons(original_points,original_faces,all_triangles=True)
        bm=bmesh.new()
        for v in original_points: bm.verts.new(v)
        hull=bmesh.ops.convex_hull(bm,input=list(bm.verts),use_existing_faces=False)
        dead=list(set(hull['geom_interior']+hull['geom_unused']))
        if dead: bmesh.ops.delete(bm,geom=dead,context='VERTS')
        bmesh.ops.triangulate(bm,faces=list(bm.faces))
        proxy=bpy.data.meshes.new('FarClosedPart_'+str(cid))
        bm.to_mesh(proxy); bm.free()
        uv=proxy.uv_layers.new(name='UV0_Atlas2K')
        for face in proxy.polygons:
            for li in face.loop_indices:
                point=proxy.vertices[proxy.loops[li].vertex_index].co
                hit=bvh.find_nearest(point)
                src=data.polygons[hit[2]]
                srcuv=[Vector((*data.uv_layers.active.data[k].uv,0)) for k in src.loop_indices]
                triangle=[original_points[k] for k in src.vertices]
                projected=barycentric_transform(hit[0],*triangle,*srcuv)
                uv.data[li].uv=projected.xy
        obj.data=proxy
        bone=part_map[cid]['dominant_bone'] or 'Root_M'
        obj.vertex_groups.clear()
        vg=obj.vertex_groups.new(name=bone)
        vg.add(list(range(len(proxy.vertices))),1,'REPLACE')
        if cid in pair_map:
            peer_bone=part_map[pair_map[cid]]['dominant_bone']
            if peer_bone!=bone: obj.vertex_groups.new(name=peer_bone)
        proxy.calc_loop_triangles()
        obj['Triangles']=len(proxy.loop_triangles)
        obj['FarClosedProxy']=True

def matching_contour(body,lod,collection,budget):
    """Budgeted paired armor shells copied from this LOD's actual body surface."""
    data=body.data
    attr=data.attributes['RSG_Component'].data
    by_component=defaultdict(list)
    for p in data.polygons: by_component[attr[p.index].value].append(p.index)
    groups=[]
    seen=set()
    for cid in by_component:
        if cid in seen: continue
        peer=pair_map.get(cid,next((k for k,v in pair_map.items() if v==cid),None))
        components={cid}
        if peer in by_component: components.add(peer)
        seen.update(components)
        if colors[cid] not in ('WarmWhite','Coral','SkyBlue','PaleYellow') or max(part_map[cid]['dimensions_m'])<.55:
            continue
        count=sum(len(by_component[c]) for c in components)
        priority=0 if cid in (77,28551) else 1 if colors[cid] in ('WarmWhite','Coral') else 2
        groups.append((priority,-max(part_map[cid]['dimensions_m']),cid,components,count))
    chosen=set(); count=0
    for _,_,_,components,triangles in sorted(groups):
        if count+triangles<=budget:
            chosen.update(components); count+=triangles
    faces=[p for p in data.polygons if attr[p.index].value in chosen]
    vis=sorted({vi for p in faces for vi in p.vertices}); remap={vi:i for i,vi in enumerate(vis)}
    width=(.014,.019,.025,0)[lod]
    mesh=bpy.data.meshes.new(f'RSGMech_LOD{lod}_MatchingArmorContour')
    mesh.from_pydata([data.vertices[vi].co+data.vertices[vi].normal*width for vi in vis],[],
                     [[remap[vi] for vi in p.vertices][::-1] for p in faces])
    mesh.materials.append(ink)
    ob=bpy.data.objects.new(f'RSGMech_LOD{lod}_Contour',mesh); collection.objects.link(ob)
    names={g.index:g.name for g in body.vertex_groups}
    weights=defaultdict(list)
    for i,vi in enumerate(vis):
        name=names[max(data.vertices[vi].groups,key=lambda g:g.weight).group]
        weights[name].append(i)
    for name,indices in weights.items():
        vg=ob.vertex_groups.new(name=name); vg.add(indices,1,'REPLACE')
    arm=ob.modifiers.new('Original_44_Bone_Rigid_Binding','ARMATURE'); arm.object=rig
    ob['LOD']=lod; ob['OutlineWidthMeters']=width
    ob['Construction']='Exact corresponding LOD body surfaces; paired major armor selected within contour budget'
    ob['SourceArmorComponents']=json.dumps(sorted(chosen))
    mesh.calc_loop_triangles()
    return ob,len(mesh.loop_triangles),len(chosen)

ink=bpy.data.materials.new('M_RSGMech_OuterContour')
ink.use_nodes=True
ink.use_backface_culling=True
nt=ink.node_tree; nt.nodes.clear()
out=nt.nodes.new('ShaderNodeOutputMaterial'); emission=nt.nodes.new('ShaderNodeEmission')
hexcode=setup['palette_srgb']['Ink']; srgb=[int(hexcode[i:i+2],16)/255 for i in (1,3,5)]
linear=[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in srgb]
emission.inputs['Color'].default_value=(*linear,1)
geometry=nt.nodes.new('ShaderNodeNewGeometry')
transparent=nt.nodes.new('ShaderNodeBsdfTransparent')
mix=nt.nodes.new('ShaderNodeMixShader')
nt.links.new(geometry.outputs['Backfacing'],mix.inputs[0])
nt.links.new(emission.outputs[0],mix.inputs[1])
nt.links.new(transparent.outputs[0],mix.inputs[2])
nt.links.new(mix.outputs[0],out.inputs['Surface'])
ink.diffuse_color=(*linear,1)
body0.data.calc_loop_triangles()
report=[]
for lod in range(4):
    c=new_collection(lod)
    if lod>=2: closed_far_proxies(lod)
    if lod==0:
        body=body0; body_count=len(body.data.loop_triangles); component_count=392
    else:
        selected,tune=tuned(lod,False,(0,13700,4800,1950)[lod])
        body,body_count,component_count=assembled(selected,lod,False,c)
    outline_count=0
    if lod<3:
        if lod==0:
            contour,outline_count,outline_components=near_contour(c)
        else:
            contour,outline_count,outline_components=matching_contour(body,lod,c,(8000,3000,1000,0)[lod])
    report.append({'LOD':lod,'body_triangles':body_count,'outline_triangles':outline_count,
                   'total_triangles':body_count+outline_count,'body_sections':1,'outline_sections':int(lod<3),
                   'physical_body_components':component_count,'screen_size':(1,.40,.16,.06)[lod],
                   'internal_line_strength':(1,.85,.45,0)[lod]})
    assert body_count<=(32000,14000,5000,2000)[lod]
    assert outline_count<=(8000,3000,1000,0)[lod]
    for obj in c.objects:
        obj.hide_render=lod!=0
        obj.hide_set(lod!=0)
for obj in list(temporary.objects):
    data=obj.data
    bpy.data.objects.remove(obj,do_unlink=True)
    if data.users==0: bpy.data.meshes.remove(data)
bpy.data.collections.remove(temporary)
scene['RSG_LODBudgets']=json.dumps(report)
scene['RSG_CurrentLOD']=0
scene.render.use_freestyle=False
bpy.ops.object.select_all(action='DESELECT')
body0.select_set(True); bpy.context.view_layer.objects.active=body0
(OUT/'lod_build_report.json').write_text(json.dumps({'LODs':report,'LOD0_all_source_components_kept':True,
    'LOD3_policy':'Keep all main armor, weapons, six legs, head, guard, antennae and color blocks; omit only small secondary hardware.',
    'outer_contour':'inverted hull with backface culling; LOD1/2 copied from exact corresponding body surfaces',
    'LOD3_closed_convex_part_proxies':True,'B_approval':'pending'},indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'RSGMech_LODs_Shading.blend'))
print(json.dumps({'LODs':report}),flush=True)
