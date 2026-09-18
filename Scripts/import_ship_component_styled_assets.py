"""Replace precisely the 13 formal Ship mesh assets, with rollback and contracts.

Run only in a standalone normal editor with -ShipComponentStyleImportWorker.
The user explicitly authorized formal replacement. Original object paths, rigs,
socket frames and catalogue Blueprints are retained. Outline is a separate UE
overlay material, so it adds no vertices, bones, collision or gameplay sockets.
"""
import hashlib
import importlib.util
import json
import math
import sys
import traceback
from pathlib import Path
import unreal

PROJECT=Path('D:/UE5.7/test1')
ROOT=PROJECT/'ArtSource/Ships/ShipComponentStyle_20260917'
OUT=ROOT/'UE_Integration'
BASE='/Game/GuLiStrike/Ship/StylizedComponents'
ROLLBACK=BASE+'/Rollback_20260917'
OWNER='GuLi.ShipComponentSurface.20260917'
LIB=unreal.EditorAssetLibrary
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
EDIT=unreal.MaterialEditingLibrary
REPORT={'success':False,'parts':{},'rollback':{},'blueprints':[],
    'release_authorization':'release_authorization_20260917.json',
    'rendering':'Three-tone unlit art light matching the existing Ship pipeline; independent masked backface overlay; body retains real cast shadows.',
    'lighting_limit':'Surface tone uses exposed ArtLightDirection, as on the approved Ship hull, rather than automatic scene light/shadow sampling.'}
sys.path.insert(0,str(PROJECT/'Scripts'))
import author_ship_component_rigs as rigs

def dump(path,data):
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def package(obj):return obj.get_path_name().split('.')[0]
def vec(v):return list(v.to_tuple())
def tf(t):return {'location':vec(t.translation),'rotation':[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w],'scale':vec(t.scale3d)}
def linear(code):
    rgb=[int(code[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb)+(1,)

def records():
    rows={}
    for folder,version in (('','v4'),('Batch02','v2'),('Batch03','v3'),('Batch04','v1')):
        root=ROOT/folder
        snapshot=json.loads((root/'Source/source_snapshot_v1.json').read_text(encoding='utf-8'))
        delivery=root/'Delivery'/version
        validation=json.loads((delivery/'Validation/FBX_Readback_Summary.json').read_text(encoding='utf-8'))
        assert validation['passed'],folder
        readbacks={r['key']:r for r in validation['checks']}
        for key,part in snapshot['parts'].items():
            sk=bool(part.get('bones'))
            fbx=delivery/'FBX'/f'{"SKM" if sk else "SM"}_SC_{key}_Styled.fbx'
            assert digest(fbx)==readbacks[key]['fbx_sha256'],key
            report=json.loads((root/'Production'/version/f'{key}_material_report.json').read_text(encoding='utf-8'))
            rows[key]={'key':key,'snapshot':part,'delivery':delivery,'fbx':fbx,'skeletal':sk,
                'report':report,'readback':readbacks[key],'target':part['visual_mesh'].split('.')[0]}
    assert len(rows)==13
    return rows

def backup(obj):
    source=package(obj)
    target=ROLLBACK+'/'+source.removeprefix('/Game/')
    if not LIB.does_asset_exist(target):
        LIB.make_directory(target.rsplit('/',1)[0])
        copy=LIB.duplicate_asset(source,target);assert copy,source
        LIB.set_metadata_tag(copy,'GuLi.SurfaceRollback.Source',source)
        assert LIB.save_loaded_asset(copy,False)
    else:
        copy=unreal.load_asset(target)
        assert LIB.get_metadata_tag(copy,'GuLi.SurfaceRollback.Source')==source,target
    REPORT['rollback'][source]=target
    return copy

def save(obj):
    LIB.set_metadata_tag(obj,'GuLi.SurfaceOwner',OWNER)
    assert LIB.save_loaded_asset(obj,False),obj.get_path_name()

def restore_static_collision(mesh,original):
    """Copy the preserved authored shapes; FBX reimport may append old hulls."""
    source=original.get_editor_property('body_setup')
    target=mesh.get_editor_property('body_setup')
    assert source and target,package(mesh)
    target.set_editor_property('agg_geom',source.get_editor_property('agg_geom'))
    target.set_editor_property('collision_trace_flag',source.get_editor_property('collision_trace_flag'))
    # This property notification explicitly invalidates and recreates physics
    # in UStaticMesh::PostEditChangeProperty, without changing its value.
    mesh.set_editor_property('complex_collision_mesh',mesh.get_editor_property('complex_collision_mesh'))
    assert target.get_editor_property('agg_geom').export_text()==source.get_editor_property('agg_geom').export_text()

def options(skeletal,skeleton=None):
    ui=unreal.FbxImportUI()
    values=dict(import_materials=False,import_textures=False,import_mesh=True,import_animations=False,
        import_as_skeletal=skeletal,create_physics_asset=False,automated_import_should_detect_type=False,
        mesh_type_to_import=unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else unreal.FBXImportType.FBXIT_STATIC_MESH)
    if skeletal:values['skeleton']=skeleton
    for k,v in values.items():ui.set_editor_property(k,v)
    data=ui.skeletal_mesh_import_data if skeletal else ui.static_mesh_import_data
    props=dict(convert_scene=True,convert_scene_unit=True,force_front_x_axis=False,import_uniform_scale=1.,
        import_mesh_lo_ds=False,normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS,
        vertex_color_import_option=unreal.VertexColorImportOption.REPLACE)
    if skeletal:props.update(update_skeleton_reference_pose=False,use_t0_as_ref_pose=False,preserve_smoothing_groups=True)
    else:props.update(combine_meshes=True,auto_generate_collision=False,generate_lightmap_u_vs=False,remove_degenerates=False)
    for k,v in props.items():data.set_editor_property(k,v)
    return ui

def import_file(file,target,skeletal=None,skeleton=None):
    folder,name=target.rsplit('/',1);LIB.make_directory(folder)
    task=unreal.AssetImportTask()
    for k,v in dict(filename=str(file),destination_path=folder,destination_name=name,automated=True,async_=False,
        replace_existing=LIB.does_asset_exist(target),replace_existing_settings=True,save=False).items():task.set_editor_property(k,v)
    if skeletal is not None:
        task.set_editor_property('factory',unreal.FbxFactory())
        task.set_editor_property('options',options(skeletal,skeleton))
    TOOLS.import_asset_tasks([task])
    obj=unreal.load_asset(target);assert obj,(target,list(task.imported_object_paths))
    LIB.set_metadata_tag(obj,'GuLi.SurfaceSourceSHA256',digest(file))
    return obj

def node(mat,cls,**props):
    n=EDIT.create_material_expression(mat,cls)
    for k,v in props.items():n.set_editor_property(k,v)
    return n
def link(a,out,b,pin):assert EDIT.connect_material_expressions(a,out,b,pin),(a,pin)
def scalar(mat,name,value):return node(mat,unreal.MaterialExpressionScalarParameter,parameter_name=name,default_value=value)
def color(mat,name,value):return node(mat,unreal.MaterialExpressionVectorParameter,parameter_name=name,default_value=unreal.LinearColor(*value))
def custom(mat,code,inputs,output=None):
    result=node(mat,unreal.MaterialExpressionCustom,code=code,
        output_type=output or unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    defs=[]
    for name in inputs:
        item=unreal.CustomInput();item.set_editor_property('input_name',name);defs.append(item)
    result.set_editor_property('inputs',defs)
    for name,(src,pin) in inputs.items():link(src,pin,result,name)
    return result
def material_asset(name):
    path=BASE+'/Materials/'+name
    mat=unreal.load_asset(path)
    if mat:assert LIB.get_metadata_tag(mat,'GuLi.SurfaceOwner')==OWNER,path
    else:
        LIB.make_directory(BASE+'/Materials');mat=TOOLS.create_asset(name,BASE+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    EDIT.delete_all_material_expressions(mat)
    for k,v in dict(shading_model=unreal.MaterialShadingModel.MSM_UNLIT,used_with_skeletal_mesh=True,
        used_with_instanced_static_meshes=True).items():mat.set_editor_property(k,v)
    return mat
def texture(mat,image,index,mask=False):
    uv=node(mat,unreal.MaterialExpressionTextureCoordinate,coordinate_index=index)
    t=node(mat,unreal.MaterialExpressionTextureSample,texture=image,
        sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS if mask else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    link(uv,'',t,'UVs');return t

def materials(key,row,images):
    body=material_asset('M_SC_'+key+'_Toon')
    body.set_editor_property('blend_mode',unreal.BlendMode.BLEND_OPAQUE)
    body.set_editor_property('two_sided',False)
    ink=color(body,'InkColor',linear('213D50'))
    paint=texture(body,images['BaseColor'],0)
    mask=texture(body,images['LineMask'],1,True)
    n=node(body,unreal.MaterialExpressionPixelNormalWS)
    light=color(body,'ArtLightDirection',(.35,-.55,.76,0))
    strength=scalar(body,'InternalLineStrength',.85)
    value=custom(body,
        'float d=dot(normalize(N),normalize(L)); float3 t=d<.18?float3(.43,.50,.60):(d<.46?float3(.72,.79,.84):float3(1,1,1)); return lerp(Base*t,Ink,saturate(Mask*Strength));',
        {'Base':(paint,'RGB'),'Mask':(mask,'R'),'N':(n,''),'L':(light,'RGB'),'Ink':(ink,'RGB'),'Strength':(strength,'')})
    assert EDIT.connect_material_property(value,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.layout_material_expressions(body);EDIT.recompile_material(body);save(body)
    outline=material_asset('M_SC_'+key+'_Outline')
    outline.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
    outline.set_editor_property('two_sided',True)
    ink=color(outline,'InkColor',linear('213D50'))
    width=scalar(outline,'OutlineWidthCm',row['report']['outline_width_m']*100)
    enabled=scalar(outline,'OutlineEnabled',1.)
    normal=node(outline,unreal.MaterialExpressionVertexNormalWS)
    offset=custom(outline,'return N*Width*Enable;',{'N':(normal,''),'Width':(width,''),'Enable':(enabled,'')})
    side=node(outline,unreal.MaterialExpressionTwoSidedSign)
    opacity=custom(outline,'return saturate(-Side)*Enable;',{'Side':(side,''),'Enable':(enabled,'')},unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    shadow=node(outline,unreal.MaterialExpressionShadowReplace)
    zero=node(outline,unreal.MaterialExpressionConstant,r=0.)
    link(opacity,'',shadow,'Default');link(zero,'',shadow,'Shadow')
    for src,pin,prop in ((ink,'RGB',unreal.MaterialProperty.MP_EMISSIVE_COLOR),(offset,'',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET),(shadow,'',unreal.MaterialProperty.MP_OPACITY_MASK)):
        assert EDIT.connect_material_property(src,pin,prop)
    EDIT.layout_material_expressions(outline);EDIT.recompile_material(outline);save(outline)
    return body,outline

def restore_sockets(mesh,row):
    for record in row['snapshot']['sockets']:
        socket=mesh.find_socket(record['name'])
        if not socket and row['skeletal']:
            assert unreal.SkeletonService.add_socket(package(mesh),record['name'],record['bone'],
                unreal.Vector(*record['location']),unreal.Rotator(*record['rotation']),unreal.Vector(*record['scale']),False)
            socket=mesh.find_socket(record['name'])
        elif not socket:
            socket=unreal.new_object(unreal.StaticMeshSocket,outer=mesh)
            socket.set_editor_property('socket_name',record['name'])
            mesh.add_socket(socket)
        socket.set_editor_property('relative_location',unreal.Vector(*record['location']))
        socket.set_editor_property('relative_rotation',unreal.Rotator(*record['rotation']))
        socket.set_editor_property('relative_scale',unreal.Vector(*record['scale']))
        if row['skeletal']:assert str(socket.bone_name)==record['bone']

def normalize_existing_reference(mesh,row):
    # The formal Skeleton already has the correct unit-scale reference frames.
    # Only normalize the imported mesh. The old authoring helper's temporary
    # rename is for NEW skeletons and must not accumulate bones on reimport.
    modifier=unreal.SkeletonModifier();assert modifier.set_skeletal_mesh(mesh)
    for ref in row['snapshot']['bones']:
        data=ref['local']
        transform=unreal.Transform(location=unreal.Vector(*data['location']),
            rotation=unreal.Quat(*data['rotation']).rotator(),scale=unreal.Vector(*data['scale']))
        assert modifier.set_bone_transform(ref['name'],transform,True)
    assert modifier.commit_skeleton_to_skeletal_mesh()
    for path in (mesh.get_path_name(),mesh.skeleton.get_path_name()):
        bones=list(unreal.SkeletonService.list_bones(path))
        assert [str(b.bone_name) for b in bones]==['Root','BarrelPitch'],(path,[str(b.bone_name) for b in bones])
        for bone,ref in zip(bones,row['snapshot']['bones']):
            current=tf(bone.local_transform);expected=ref['local']
            assert max(abs(a-b) for a,b in zip(current['location'],expected['location']))<.01,(path,ref['name'],'location')
            assert max(abs(a-b) for a,b in zip(current['scale'],expected['scale']))<.00001,(path,ref['name'],'scale')
            assert abs(sum(a*b for a,b in zip(current['rotation'],expected['rotation'])))>.999999,(path,ref['name'],'rotation')

def validate(mesh,row,before):
    sk=row['skeletal'];key=row['key']
    bounds=mesh.get_imported_bounds() if sk else mesh.get_bounds()
    errors=[abs(a-b) for a,b in zip(vec(bounds.origin),before['bounds']['origin'])]
    errors.extend(abs(a-b) for a,b in zip(vec(bounds.box_extent),before['bounds']['extent']))
    assert max(errors)<.06,(key,'bounds changed',max(errors))
    report={'mesh':package(mesh),'class':mesh.get_class().get_name(),'bounds_max_error_cm':max(errors),
        'dimensions_cm':vec(bounds.box_extent*2),'socket_count':len(row['snapshot']['sockets']),
        'source_fbx':str(row['fbx'].relative_to(ROOT)),'source_fbx_sha256':digest(row['fbx']),
        'material':(mesh.materials if sk else mesh.static_materials)[0].material_interface.get_path_name(),
        'body_triangles_FBX':row['readback'].get('raw_FBX_triangles',row['readback']['body_triangles']),
        'material_slots':[str(m.material_slot_name) for m in (mesh.materials if sk else mesh.static_materials)],
        'outline':'separate masked overlay; zero extra mesh vertices'}
    if sk:
        actual=list(unreal.SkeletonService.list_bones(package(mesh)))
        assert [str(b.bone_name) for b in actual]==['Root','BarrelPitch']
        for bone,ref in zip(actual,row['snapshot']['bones']):
            value=tf(bone.local_transform)
            assert max(abs(a-b) for a,b in zip(value['location'],ref['local']['location']))<.01
            assert max(abs(a-b) for a,b in zip(value['scale'],ref['local']['scale']))<.00001
            assert abs(sum(a*b for a,b in zip(value['rotation'],ref['local']['rotation'])))>.999999
        assert mesh.skeleton.get_path_name()==before['skeleton']
        assert (mesh.physics_asset.get_path_name() if mesh.physics_asset else None)==before['physics']
        weights=unreal.SkinWeightModifier();assert weights.set_skeletal_mesh(mesh)
        counts={'Root':0,'BarrelPitch':0}
        for i in range(weights.get_num_vertices()):
            active={str(b):w for b,w in weights.get_vertex_weights(i).items() if w>.000001}
            assert len(active)==1 and abs(next(iter(active.values()))-1)<.000001,(key,i)
            counts[next(iter(active))]+=1
        assert all(counts.values());report['rigid_weight_one_vertices']=counts
        report['bones']=[{'name':str(b.bone_name),'parent':str(b.parent_bone_name),'local':tf(b.local_transform)} for b in actual]
        report['skeleton']=mesh.skeleton.get_path_name()
    else:
        assert mesh.get_num_triangles(0)==row['readback']['body_triangles']
        uv=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_num_uv_channels(mesh,0)
        assert uv==len(row['readback']['uv_names']),(key,uv)
        report.update(triangles=mesh.get_num_triangles(0),uv_channels=uv)
    for ref in row['snapshot']['sockets']:
        socket=mesh.find_socket(ref['name']);assert socket
        assert (socket.relative_location-unreal.Vector(*ref['location'])).length()<.0001
        assert (socket.relative_scale-unreal.Vector(*ref['scale'])).length()<.00001
        if sk:assert str(socket.bone_name)==ref['bone']
    report['passed']=True
    return report

def run():
    assert json.loads((OUT/'release_authorization_20260917.json').read_text(encoding='utf-8'))['decision']=='implementation_and_formal_UE_replacement_authorized'
    rows=records()
    before=json.loads((OUT/'formal_targets_before.json').read_text(encoding='utf-8'))['parts']
    # Back up every target before the first mutation; no user assets are deleted.
    for key,row in rows.items():
        mesh=unreal.load_asset(row['target']);backup(mesh)
        if row['skeletal']:backup(mesh.skeleton)
        backup(unreal.load_asset(row['snapshot']['blueprint']))
    backup(unreal.load_asset('/Game/GuLiStrike/Ship/Parts/BP_SC_Thor_MissilePod_Lv2'))
    dump(OUT/'rollback_manifest.json',REPORT['rollback'])
    for key,row in rows.items():
        print('SHIP_SURFACE_IMPORT_BEGIN',key,flush=True)
        old=unreal.load_asset(row['target'])
        sk=row['skeletal'];skeleton=old.skeleton if sk else None
        images={}
        for role in ('BaseColor','ORM','LineMask'):
            name=f'T_SC_{key}_{role}_2K'
            im=import_file(row['delivery']/'Textures'/(name+'.png'),BASE+'/Textures/'+name)
            im.set_editor_property('srgb',role=='BaseColor')
            if role!='BaseColor':im.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS)
            save(im);images[role]=im
        body,outline=materials(key,row,images)
        mesh=import_file(row['fbx'],row['target'],sk,skeleton)
        if sk:
            normalize_existing_reference(mesh,row)
            sub=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
            settings=sub.get_lod_build_settings(mesh,0)
            for k,v in dict(recompute_normals=False,recompute_tangents=False,use_full_precision_u_vs=True).items():settings.set_editor_property(k,v)
            sub.set_lod_build_settings(mesh,0,settings)
            # Legacy reimport can retain the previous slot alongside the FBX
            # slot. Preserve section indices and give every retained slot the
            # same surface; do not leave an old material on a live section.
            slots=list(mesh.materials);assert slots
            for slot in slots:
                slot.set_editor_property('material_interface',body)
                slot.set_editor_property('overlay_material_interface',outline)
            mesh.set_editor_property('materials',slots)
            mesh.set_editor_property('overlay_material',outline)
            save(mesh.skeleton)
        else:
            sub=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
            settings=sub.get_lod_build_settings(mesh,0)
            for k,v in dict(recompute_normals=False,recompute_tangents=False,use_full_precision_u_vs=True,
                build_scale3d=unreal.Vector(1,1,1),generate_lightmap_u_vs=False,remove_degenerates=False).items():settings.set_editor_property(k,v)
            sub.set_lod_build_settings(mesh,0,settings)
            slots=list(mesh.static_materials);assert slots
            for slot in slots:
                slot.set_editor_property('material_interface',body)
                slot.set_editor_property('overlay_material_interface',outline)
            mesh.set_editor_property('static_materials',slots)
            restore_static_collision(mesh,unreal.load_asset(REPORT['rollback'][row['target']]))
        for prop in ('negative_bounds_extension','positive_bounds_extension'):
            mesh.set_editor_property(prop,unreal.Vector(*before[key][prop]))
        restore_sockets(mesh,row)
        report=validate(mesh,row,before[key])
        save(mesh)
        report['overlay_material']=outline.get_path_name()
        report['texture_color_spaces']={'BaseColor':'sRGB','ORM':'Masks/linear','LineMask':'Masks/linear'}
        REPORT['parts'][key]=report
        bps=[row['snapshot']['blueprint']]+(['/Game/GuLiStrike/Ship/Parts/BP_SC_Thor_MissilePod_Lv2'] if key=='Thor_MissilePod' else [])
        for path in bps:
            bp=unreal.load_asset(path);cdo=unreal.get_default_object(bp.generated_class())
            visual=cdo.get_editor_property('skeletal_mesh' if sk else 'static_mesh')
            assert package(visual)==row['target'],(key,path,'formal reference changed')
            overrides=list(cdo.get_editor_property('override_materials'))
            if any(overrides):
                cdo.set_editor_property('override_materials',[])
                unreal.BlueprintService.compile_blueprint(path);save(bp)
            REPORT['blueprints'].append({'path':path,'formal_mesh':package(visual),'override_materials_cleared':bool(any(overrides))})
        dump(OUT/'formal_import_report.json',REPORT)
        print('SHIP_SURFACE_IMPORT_VERIFIED',key,flush=True)
    REPORT['success']=True
    REPORT['unique_meshes']=len(REPORT['parts'])
    REPORT['formal_component_blueprints']=len(REPORT['blueprints'])
    dump(OUT/'formal_import_report.json',REPORT)

if __name__=='__main__':
    assert '-ShipComponentStyleImportWorker' in unreal.SystemLibrary.get_command_line()
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    cvar='Interchange.FeatureFlags.Import.Enable'
    prior=unreal.SystemLibrary.get_console_variable_int_value(cvar)
    unreal.SystemLibrary.execute_console_command(world,cvar+' 0')
    try:run()
    except Exception:
        REPORT['error']=traceback.format_exc();dump(OUT/'formal_import_report.json',REPORT);unreal.log_error(REPORT['error'])
    finally:
        unreal.SystemLibrary.execute_console_command(world,cvar+' '+str(prior))
        unreal.SystemLibrary.quit_editor()
