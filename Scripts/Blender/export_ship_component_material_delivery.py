"""Export the explicitly B-approved original-mesh material study and read FBX back.

Uses the existing IndustrialDefenseSet FBX reader and rigid Ship conventions.
No engine assets are imported. Approved review files are never overwritten.
"""
import ast
import collections
import bpy
import importlib.util
import json
import math
import shutil
import time
from pathlib import Path
from mathutils import Vector, Matrix
from mathutils.kdtree import KDTree
import numpy as np

PROJECT=Path('D:/UE5.7/test1')
spec=importlib.util.spec_from_file_location('material_source',PROJECT/'Scripts/Blender/style_ship_component_original_materials.py')
s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
DELIVERY=s.ROOT/'Delivery/v4'
for folder in ('Blender','FBX','Textures','Previews','Parameters','Validation'):(DELIVERY/folder).mkdir(parents=True,exist_ok=True)
APPROVAL=json.loads((s.ROOT/'approval_B_20260917.json').read_text(encoding='utf-8'))
assert APPROVAL['decision']=='approved' and APPROVAL['gate']=='B'
bpy.context.preferences.filepaths.save_version=0

# Reuse exactly the existing import_file/world_points functions without running
# that script's hard-coded building-asset validation at module import time.
reader_path=PROJECT/'ArtSource/Buildings/IndustrialDefenseSet/scripts/validate_exports.py'
tree=ast.parse(reader_path.read_text(encoding='utf-8'))
reader_nodes=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ('import_file','world_points')]
reader_globals={'bpy':bpy}
exec(compile(ast.Module(body=reader_nodes,type_ignores=[]),str(reader_path),'exec'),reader_globals)
import_file=reader_globals['import_file'];world_points=reader_globals['world_points']

def uv_order_for_export(mesh):
    channels={}
    for uv in mesh.uv_layers:
        data=np.empty(len(uv.data)*2,dtype=np.float32);uv.data.foreach_get('uv',data);channels[uv.name]=data
    order=[s.UV_NAME,s.LINE_UV]+[name for name in channels if name not in (s.UV_NAME,s.LINE_UV)]
    while mesh.uv_layers:mesh.uv_layers.remove(mesh.uv_layers[0])
    for name in order:
        layer=mesh.uv_layers.new(name=name);layer.data.foreach_set('uv',channels[name])
    mesh.uv_layers.active_index=0;mesh.uv_layers[0].active_render=True
    return order

def export_material(key):
    mat=bpy.data.materials.new('M_SC_'+key+'_Portable');mat.use_nodes=True
    nodes=mat.node_tree.nodes;links=mat.node_tree.links
    bsdf=next(n for n in nodes if n.type=='BSDF_PRINCIPLED')
    bsdf.inputs['Metallic'].default_value=.2;bsdf.inputs['Roughness'].default_value=.64
    image=bpy.data.images.load(str(DELIVERY/'Textures'/('T_SC_'+key+'_BaseColor_2K.png')),check_existing=False)
    image.colorspace_settings.name='sRGB'
    uv=nodes.new('ShaderNodeUVMap');uv.uv_map=s.UV_NAME
    tex=nodes.new('ShaderNodeTexImage');tex.image=image
    links.new(uv.outputs[0],tex.inputs[0]);links.new(tex.outputs['Color'],bsdf.inputs['Base Color'])
    mat['GuLiStrike_Toon_Parameters']='../Parameters/MaterialParameters_v4.json'
    mat['GuLiStrike_LineMask']='../Textures/T_SC_'+key+'_LineMask_2K.png'
    return mat

def vertex_owners(body):
    names={g.index:g.name for g in body.vertex_groups}
    owners=[]
    for v in body.data.vertices:
        groups=[g for g in v.groups if g.weight>1e-7]
        assert len(groups)==1 and abs(groups[0].weight-1)<1e-6
        owners.append(names[groups[0].group])
    return owners

def portable_image_paths():
    textures=list((DELIVERY/'Textures').glob('*.png'))
    for image in bpy.data.images:
        if image.packed_file and image.name.startswith('T_SC_'):
            matches=[p for p in textures if image.name.startswith(p.stem)]
            assert len(matches)==1,(image.name,[p.name for p in matches])
            image.filepath='//../Textures/'+matches[0].name

def save_delivery_blend(path):
    bpy.context.preferences.filepaths.save_version=0
    for attempt in range(3):
        try:
            bpy.ops.wm.save_as_mainfile(filepath=str(path),compress=True,relative_remap=False)
            return
        except RuntimeError:
            if attempt==2:raise
            time.sleep(.5)

def export_one(key):
    approved=APPROVAL['parts'][key]
    source=s.ROOT/approved['approved_blend']
    assert s.filehash(source)==approved['approved_blend_sha256'],'Approved source changed after B'
    bpy.ops.wm.open_mainfile(filepath=str(source));scene=bpy.context.scene;scene.frame_set(0)
    body=next(o for o in scene.objects if o.type=='MESH' and o.get('source_file'))
    arm=next((o for o in scene.objects if o.type=='ARMATURE'),None)
    assert s.ink_bake.geometry_hash(body.data)==approved['original_geometry_sha256']
    # Save a portable, fully editable final Blender copy with its approved toon
    # material, separate outline and demonstration action intact.
    scene['review_B']='approved: 三件均通过 B，导出 FBX'
    scene['approval_record']='//../Parameters/approval_B_20260917.json'
    # ORM is a handoff texture rather than an input of the approved toon graph;
    # keep it packed explicitly even though it has no material-node users.
    for role in ('BaseColor','ORM','LineMask'):
        filename='T_SC_'+key+'_'+role+'_2K.png'
        image=next((i for i in bpy.data.images if i.name.startswith(Path(filename).stem)),None)
        if image is None:
            image=bpy.data.images.load(str(DELIVERY/'Textures'/filename),check_existing=False)
            image.colorspace_settings.name='sRGB' if role=='BaseColor' else 'Non-Color'
            image.pack()
        image.use_fake_user=True
    portable_image_paths()
    blend_path=DELIVERY/'Blender'/('SC_'+key+'_Styled.blend')
    save_delivery_blend(blend_path)
    if arm:
        arm.animation_data_clear()
        for bone in arm.pose.bones:bone.matrix_basis.identity()
    bpy.context.view_layer.update()
    # Export a mesh-data copy so even the original UV order in the final Blender
    # source remains preserved. FBX UV0/UV1 are explicit portable paint/mask UVs.
    body.data=body.data.copy();order=uv_order_for_export(body.data)
    body.data.materials.clear();body.data.materials.append(export_material(key))
    for polygon in body.data.polygons:polygon.material_index=0
    sockets=s.SNAP['parts'][key]['sockets']
    body['GuLiStrike_Sockets_JSON']=json.dumps(sockets,separators=(',',':'))
    body['GuLiStrike_PaintUV_Index']=0;body['GuLiStrike_LineUV_Index']=1
    body['GuLiStrike_OriginalGeometry_SHA256']=approved['original_geometry_sha256']
    body['GuLiStrike_SourceApproval']='B approved v4; original geometry retained'
    # As in the project's existing rig export, sockets are a separate interface
    # contract, not extra skeleton bones. Embed that contract in FBX custom props
    # and a readable sidecar, and retain bone-parented markers in the .blend.
    s.dump(DELIVERY/'Parameters'/(key+'_Sockets.json'),{'key':key,'sockets':sockets,'FBX_custom_property':'GuLiStrike_Sockets_JSON',
             'Blender_markers':'Original Socket_1...N; bone-parented for the two guns','UE_socket_creation':'Use original names/transforms when formal UE integration is separately scheduled'})
    expected_points=[body.matrix_world@v.co for v in body.data.vertices]
    body.data.calc_loop_triangles()
    expected={'points':[list(p) for p in expected_points],'triangles':len(body.data.loop_triangles),
       'triangle_loops':[list(t.loops) for t in body.data.loop_triangles],
       'triangle_vertices':[list(t.vertices) for t in body.data.loop_triangles],
       'triangle_world_areas':[(expected_points[t.vertices[1]]-expected_points[t.vertices[0]]).cross(expected_points[t.vertices[2]]-expected_points[t.vertices[0]]).length*.5 for t in body.data.loop_triangles],
       'uv_names':order,'uv_values':{u.name:[list(d.uv) for d in u.data] for u in body.data.uv_layers},
       'origin':list(body.matrix_world.translation),'sockets':sockets,'body_geometry_sha256':s.ink_bake.geometry_hash(body.data),
       'bones':{b.name:{'parent':b.parent.name if b.parent else None,'head':list(arm.matrix_world@b.head_local)} for b in arm.data.bones} if arm else {},
       'rigid_owners':vertex_owners(body) if arm else [],'approved_blend_sha256':approved['approved_blend_sha256'],
       'final_blend':str(blend_path.relative_to(DELIVERY)),'final_blend_sha256':s.filehash(blend_path)}
    assert expected['body_geometry_sha256']==approved['original_geometry_sha256']
    s.select(body)
    if arm:arm.select_set(True);bpy.context.view_layer.objects.active=arm
    prefix='SKM' if arm else 'SM'
    fbx=DELIVERY/'FBX'/(prefix+'_SC_'+key+'_Styled.fbx')
    bpy.ops.export_scene.fbx(filepath=str(fbx),use_selection=True,object_types={'MESH','ARMATURE'} if arm else {'MESH'},
        use_mesh_modifiers=True,add_leaf_bones=False,bake_anim=False,axis_forward='-Y',axis_up='Z',
        primary_bone_axis='X',secondary_bone_axis='Z',use_armature_deform_only=True,
        apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',mesh_smooth_type='OFF',
        use_tspace=True,path_mode='RELATIVE',use_custom_props=True,embed_textures=False)
    expected['fbx_file']=str(fbx.relative_to(DELIVERY));expected['fbx_sha256']=s.filehash(fbx)
    s.dump(DELIVERY/'Validation'/(key+'_export_expected.json'),expected)
    print('APPROVED_FBX_EXPORTED',key,flush=True)
    return expected

def max_closest(a,b):
    kd=KDTree(len(b))
    for i,p in enumerate(b):kd.insert(p,i)
    kd.balance();return max(kd.find(p)[2] for p in a)

def validate(key,expected):
    fbx=DELIVERY/expected['fbx_file']
    objects=import_file(fbx);assert len(objects)==1
    body=objects[0];body.data.calc_loop_triangles()
    arm=next((o for o in bpy.context.scene.objects if o.type=='ARMATURE'),None)
    points=world_points(body);source=[Vector(p) for p in expected['points']]
    lo,hi=s.bounds(points);elo,ehi=s.bounds(source)
    geo_error=max(max_closest(points,source),max_closest(source,points))
    dim_error=max(abs((hi-lo)[i]-(ehi-elo)[i]) for i in range(3))
    origin_error=(body.matrix_world.translation-Vector(expected['origin'])).length
    uv_names=[u.name for u in body.data.uv_layers]
    # FBX preserves the exported loop order for these original triangle meshes.
    uv_errors={}
    dropped_degenerate=0
    dropped_duplicate=0
    raw_fbx_check=None
    retained_loops=None
    if len(body.data.loops)!=len(expected['uv_values'][uv_names[0]]) and 'triangle_world_areas' in expected:
        # Identify the actual omitted faces. Some collinear zero-area faces can
        # survive FBX, whereas a collapsed face with repeated corners cannot.
        original_all=np.concatenate([np.array(expected['uv_values'][name]) for name in uv_names],axis=1)
        actual_all=np.concatenate([np.array([d.uv[:] for d in body.data.uv_layers[name].data]) for name in uv_names],axis=1)
        from io_scene_fbx import parse_fbx
        rawtree,_=parse_fbx.parse(str(fbx))
        objects=next(n for n in rawtree.elems if n.id==b'Objects')
        geometries=[n for n in objects.elems if n.id==b'Geometry' and n.props[-1]==b'Mesh']
        assert len(geometries)==1
        raw=geometries[0]
        indices=next(n for n in raw.elems if n.id==b'PolygonVertexIndex').props[0]
        raw_vertices=[int(i) if i>=0 else -int(i)-1 for i in indices]
        assert sum(i<0 for i in indices)==expected['triangles']
        assert raw_vertices==[v for triangle in expected['triangle_vertices'] for v in triangle]
        raw_uv_errors={}
        for layer in [n for n in raw.elems if n.id==b'LayerElementUV']:
            children={n.id:n.props[0] for n in layer.elems}
            name=children[b'Name'].decode()
            assert children[b'MappingInformationType']==b'ByPolygonVertex'
            values=np.array(children[b'UV']).reshape(-1,2)[np.array(children[b'UVIndex'])]
            original=np.array(expected['uv_values'][name])
            assert values.shape==original.shape
            raw_uv_errors[name]=float(np.max(np.abs(values-original)))
            assert raw_uv_errors[name]<.000001
        assert set(raw_uv_errors)==set(expected['uv_names'])
        raw_fbx_check={'triangles':expected['triangles'],'all_original_polygon_vertex_indices_preserved':True,
            'uv_max_errors':raw_uv_errors,'passed':True,'reader':'Blender bundled io_scene_fbx.parse_fbx'}
        # Mesh validation may move the last polygon into the deleted polygon's
        # slot. Compare full multi-channel triangle UV signatures, preserving
        # winding by allowing cyclic corner rotations only.
        def signature(values):
            corners=[tuple(round(float(x),7) for x in row) for row in values]
            variants=[tuple(corners[i:]+corners[:i]) for i in range(3)]
            best=min(range(3),key=lambda i:variants[i])
            return variants[best],best
        lookup=collections.defaultdict(list)
        for index,loops in enumerate(expected['triangle_loops']):
            sig,rotation=signature(original_all[loops]);lookup[sig].append((index,loops,rotation))
        retained_loops=[None]*len(actual_all)
        for tri in body.data.loop_triangles:
            loops=list(tri.loops);sig,rotation=signature(actual_all[loops])
            assert lookup.get(sig),('Triangle UV signature changed',key,tri.index)
            _,oldloops,oldrotation=lookup[sig].pop()
            oldcanonical=oldloops[oldrotation:]+oldloops[:oldrotation]
            newcanonical=loops[rotation:]+loops[:rotation]
            for new,old in zip(newcanonical,oldcanonical):retained_loops[new]=old
        for items in lookup.values():
            for index,loops,_ in items:
                if expected['triangle_world_areas'][index]<=1e-12:dropped_degenerate+=1
                else:
                    vertices=sorted(expected['triangle_vertices'][index])
                    duplicates=[i for i,t in enumerate(expected['triangle_vertices']) if i!=index and sorted(t)==vertices]
                    assert duplicates,('Unique source face lost',key,index)
                    dropped_duplicate+=1
        assert None not in retained_loops and dropped_degenerate+dropped_duplicate==expected['triangles']-len(body.data.loop_triangles)
    for uv in body.data.uv_layers:
        values=np.array([d.uv[:] for d in uv.data]);original=np.array(expected['uv_values'][uv.name])
        if retained_loops is not None:original=original[retained_loops]
        assert values.shape==original.shape,(uv.name,values.shape,original.shape)
        uv_errors[uv.name]=float(np.max(np.abs(values-original)))
    socket_contract=json.loads(body['GuLiStrike_Sockets_JSON'])
    bones={b.name:{'parent':b.parent.name if b.parent else None,'head':list(arm.matrix_world@b.head_local)} for b in arm.data.bones} if arm else {}
    assert set(bones)==set(expected['bones'])
    bone_error=max(((Vector(bones[n]['head'])-Vector(b['head'])).length for n,b in expected['bones'].items()),default=0)
    if arm:
        assert {n:b['parent'] for n,b in bones.items()}=={'Root':None,'BarrelPitch':'Root'}
        owners=vertex_owners(body)
        # Compare rigid memberships with world positions; FBX can reorder vertices.
        for bone in ('Root','BarrelPitch'):
            original=[p for p,o in zip(source,expected['rigid_owners']) if o==bone]
            imported=[p for p,o in zip(points,owners) if o==bone]
            assert max_closest(imported,original)<.0001
    record={'key':key,'fbx_file':expected['fbx_file'],'fbx_sha256':s.filehash(fbx),'mesh_count':1,
       'body_triangles':len(body.data.loop_triangles),'dimensions_m':list(hi-lo),'max_dimension_error_m':dim_error,
       'max_geometry_error_m':geo_error,'origin_error_m':origin_error,'uv_names':uv_names,'uv_max_errors':uv_errors,
       'materials':[m.name for m in body.data.materials],'bones':bones,'max_bone_pivot_error_m':bone_error,
       'rigid_weights':'single bone, weight 1' if arm else 'not_applicable_static',
       'socket_names':[x['name'] for x in socket_contract],'socket_metadata_matches_source':socket_contract==expected['sockets'],
       'socket_storage':'FBX mesh custom property GuLiStrike_Sockets_JSON; editable markers retained in Blender',
       'reader_reused':str(reader_path.relative_to(PROJECT)),'reader_sha256':s.filehash(reader_path),
       'demo_animation_exported':False,'UE_formal_validation':'not_run'}
    record['Blender_readback_cleaned_degenerate_triangles']=dropped_degenerate
    record['Blender_readback_cleaned_duplicate_triangles']=dropped_duplicate
    record['raw_FBX_preservation']=raw_fbx_check
    record['raw_FBX_triangles']=expected['triangles']
    record['source_body_geometry_edited']=False
    record['passed']=bool(geo_error<.0001 and dim_error<.0001 and origin_error<.0001 and bone_error<.0001
       and len(body.data.loop_triangles)==expected['triangles']-dropped_degenerate-dropped_duplicate and len(body.data.materials)==1 and uv_names==expected['uv_names']
       and max(uv_errors.values())<.00001 and socket_contract==expected['sockets'] and not bpy.data.actions)
    s.dump(DELIVERY/'Validation'/(key+'_FBX_Readback.json'),record)
    print('FBX_READBACK',key,json.dumps(record),flush=True)
    assert record['passed'],'FBX readback failed: '+key
    return record

def overview():
    bpy.ops.wm.read_factory_settings(use_empty=True);empty=bpy.context.scene;scenes=[]
    for key in s.SOURCE_FILES:
        with bpy.data.libraries.load(str(DELIVERY/'Blender'/('SC_'+key+'_Styled.blend')),link=False) as (src,dst):dst.scenes=src.scenes
        scenes.extend(dst.scenes)
    bpy.context.window.scene=scenes[0];bpy.data.scenes.remove(empty)
    portable_image_paths()
    save_delivery_blend(DELIVERY/'Blender/ShipComponentStyle_Overview.blend')

def verify_portable_blends():
    records=[]
    for key in s.SOURCE_FILES:
        path=DELIVERY/'Blender'/('SC_'+key+'_Styled.blend')
        bpy.ops.wm.open_mainfile(filepath=str(path));scene=bpy.context.scene
        body=next(o for o in scene.objects if o.type=='MESH' and o.get('source_file'))
        assert s.ink_bake.geometry_hash(body.data)==APPROVAL['parts'][key]['original_geometry_sha256']
        images=[]
        for image in bpy.data.images:
            if not image.name.startswith('T_SC_'):continue
            external=Path(bpy.path.abspath(image.filepath))
            assert external.is_file() and image.packed_file and tuple(image.size)==(2048,2048),(image.name,image.filepath,str(external),tuple(image.size))
            images.append({'name':image.name,'relative_path':image.filepath,'packed':True,'external_exists':True})
        assert len(images)==3,images
        assert Path(bpy.path.abspath(scene['approval_record'])).is_file()
        assert scene.render.engine=='BLENDER_EEVEE'
        assert any(n.type=='SHADERTORGB' for n in body.data.materials[0].node_tree.nodes)
        records.append({'key':key,'blend':str(path.relative_to(DELIVERY)),'sha256':s.filehash(path),
            'geometry_matches_approved_source':True,'images':images,'approval_record_resolves':True,
            'renderer':'BLENDER_EEVEE','toon_shader_retained':True,'passed':True})
    overview_path=DELIVERY/'Blender/ShipComponentStyle_Overview.blend'
    bpy.ops.wm.open_mainfile(filepath=str(overview_path))
    assert len(bpy.data.scenes)==3
    assert all(Path(bpy.path.abspath(i.filepath)).is_file() for i in bpy.data.images if i.name.startswith('T_SC_'))
    s.dump(DELIVERY/'Validation/Portable_Blender_Readback.json',{'passed':True,'checks':records,
        'overview':{'scenes':len(bpy.data.scenes),'sha256':s.filehash(overview_path),'external_textures_resolve':True,'passed':True}})

if __name__=='__main__':
    shutil.copy2(s.ROOT/'approval_B_20260917.json',DELIVERY/'Parameters/approval_B_20260917.json')
    for image in (s.OUT/'Textures').glob('*.png'):shutil.copy2(image,DELIVERY/'Textures'/image.name)
    shutil.copy2(s.OUT/'MaterialParameters_v4.json',DELIVERY/'Parameters/MaterialParameters_v4.json')
    for image in s.PRE.iterdir():
        if image.suffix in ('.png','.mp4') and not image.name.endswith('_material_preview.png'):shutil.copy2(image,DELIVERY/'Previews'/image.name)
    expected={key:export_one(key) for key in s.SOURCE_FILES}
    reports=[validate(key,row) for key,row in expected.items()]
    s.dump(DELIVERY/'Validation/FBX_Readback_Summary.json',{'passed':all(r['passed'] for r in reports),'checks':reports})
    overview()
    verify_portable_blends()
    print('SHIP_APPROVED_MATERIAL_DELIVERY_EXPORTED',flush=True)
