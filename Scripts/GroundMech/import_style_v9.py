"""Import authorized v9 art into new owned paths; leave the live Ground BP alone.

Run in a source-engine editor worker with -MechStyleImportWorker. After this
finishes, the live editor installs the new visuals in the existing Ground BP.
"""
import unreal,json,hashlib,traceback,math
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/UE_StyleSync_v9')
GROUND='/Game/GuLiStrike/GroundMech/Style_v9'
SPIDER='/Game/GuLiStrike/Mechs/SpiderMech'
SOURCE='/Game/Assets/MechaController/Mech_Constructor_Lt_Med'
SP_SOURCE='/Game/Assets/Mech_Project/Characters/SpiderMech'
OWNER='GuLi.MechStyleSync.v9'
LIB=unreal.EditorAssetLibrary;TOOLS=unreal.AssetToolsHelpers.get_asset_tools();EDIT=unreal.MaterialEditingLibrary
REPORT={'success':False,'parts':{},'stage':'begin'}

def checkpoint(): (OUT/'ue_import.json').write_text(json.dumps(REPORT,indent=2,ensure_ascii=False),encoding='utf-8')
def save(obj):
    assert obj.get_path_name().startswith((GROUND+'/',SPIDER+'/'))
    LIB.set_metadata_tag(obj,'GuLi.StyleOwner',OWNER)
    assert LIB.save_loaded_asset(obj,False),obj.get_path_name()
    return obj
def create(path,cls,factory):
    if LIB.does_asset_exist(path):
        obj=unreal.load_asset(path);assert LIB.get_metadata_tag(obj,'GuLi.StyleOwner')==OWNER,path
        return obj
    folder,name=path.rsplit('/',1);LIB.make_directory(folder)
    return save(TOOLS.create_asset(name,folder,cls,factory))
def imported(file,path,options=None):
    if LIB.does_asset_exist(path):assert LIB.get_metadata_tag(unreal.load_asset(path),'GuLi.StyleOwner')==OWNER,path
    folder,name=path.rsplit('/',1);LIB.make_directory(folder)
    task=unreal.AssetImportTask()
    for k,v in dict(filename=str(file),destination_path=folder,destination_name=name,automated=True,async_=False,
            replace_existing=True,replace_existing_settings=True,save=False).items():task.set_editor_property(k,v)
    task.factory=unreal.FbxFactory() if options else unreal.TextureFactory()
    if options:task.options=options
    TOOLS.import_asset_tasks([task]);asset=unreal.load_asset(path);assert asset,(path,list(task.imported_object_paths))
    LIB.set_metadata_tag(asset,'GuLi.StyleSourceSHA256',hashlib.sha256(Path(file).read_bytes()).hexdigest())
    return asset
def node(mat,cls,**props):
    n=EDIT.create_material_expression(mat,cls)
    for k,v in props.items():n.set_editor_property(k,v)
    return n
def link(a,p,b,q):assert EDIT.connect_material_expressions(a,p,b,q)
def linear(hex):
    c=[int(hex[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in c)+(1,)
def custom(mat,code,inputs,output=unreal.CustomMaterialOutputType.CMOT_FLOAT3):
    n=node(mat,unreal.MaterialExpressionCustom,code=code,output_type=output)
    slots=[]
    for key in inputs:
        pin=unreal.CustomInput();pin.set_editor_property('input_name',key);slots.append(pin)
    n.set_editor_property('inputs',slots)
    for key,(src,pin) in inputs.items():link(src,pin,n,key)
    return n
def material(path):
    m=create(path,unreal.Material,unreal.MaterialFactoryNew());EDIT.delete_all_material_expressions(m)
    for k,v in dict(shading_model=unreal.MaterialShadingModel.MSM_UNLIT,used_with_skeletal_mesh=True).items():m.set_editor_property(k,v)
    return m
def outline(base):
    mat=material(base+'/Materials/M_Ink_Backfaces')
    mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED);mat.set_editor_property('two_sided',True)
    ink=node(mat,unreal.MaterialExpressionVectorParameter,parameter_name='InkColor',default_value=unreal.LinearColor(*linear('152632')))
    side=node(mat,unreal.MaterialExpressionTwoSidedSign)
    mask=custom(mat,'return saturate(-Side);',{'Side':(side,'')},unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    shadow=node(mat,unreal.MaterialExpressionShadowReplace);zero=node(mat,unreal.MaterialExpressionConstant,r=0.)
    link(mask,'',shadow,'Default');link(zero,'',shadow,'Shadow')
    assert EDIT.connect_material_property(ink,'RGB',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert EDIT.connect_material_property(shadow,'',unreal.MaterialProperty.MP_OPACITY_MASK)
    EDIT.layout_material_expressions(mat);EDIT.recompile_material(mat);return save(mat)
def surface(base,name,images):
    mat=material(base+'/Materials/M_'+name+'_InkCel')
    textures={}
    for role,image in images.items():
        textures[role]=node(mat,unreal.MaterialExpressionTextureSample,texture=image,
            sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS if role=='LineMask' else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    normal=node(mat,unreal.MaterialExpressionPixelNormalWS)
    light=node(mat,unreal.MaterialExpressionVectorParameter,parameter_name='ArtLightDirection',default_value=unreal.LinearColor(.35,.55,.76,0))
    ink=node(mat,unreal.MaterialExpressionVectorParameter,parameter_name='InkColor',default_value=unreal.LinearColor(*linear('152632')))
    emission=(textures['Emissive'],'RGB') if 'Emissive' in textures else (node(mat,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(0,0,0)), '')
    value=custom(mat,'float d=dot(normalize(N),normalize(L)); float3 t=d<0.13?float3(.28,.34,.45):(d<0.56?float3(.64,.70,.80):float3(1.15,1.10,1.02)); return lerp(Base*t+Glow,Ink,saturate(Mask));',
        {'Base':(textures['BaseColor'],'RGB'),'Mask':(textures['LineMask'],'R'),'Glow':emission,'N':(normal,''),'L':(light,'RGB'),'Ink':(ink,'RGB')})
    assert EDIT.connect_material_property(value,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.layout_material_expressions(mat);EDIT.recompile_material(mat);return save(mat)
def fbx_options(skeletal,original):
    ui=unreal.FbxImportUI();kind=unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else unreal.FBXImportType.FBXIT_STATIC_MESH
    for k,v in dict(import_materials=False,import_textures=False,import_mesh=True,import_as_skeletal=skeletal,
            import_animations=False,create_physics_asset=False,automated_import_should_detect_type=False,
            mesh_type_to_import=kind,original_import_type=kind).items():ui.set_editor_property(k,v)
    if skeletal:ui.skeleton=original.skeleton
    data=ui.skeletal_mesh_import_data if skeletal else ui.static_mesh_import_data
    props=dict(convert_scene=True,convert_scene_unit=True,force_front_x_axis=False,import_uniform_scale=1.,
        normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS,import_mesh_lo_ds=False)
    if skeletal:props.update(update_skeleton_reference_pose=False,use_t0_as_ref_pose=False,preserve_smoothing_groups=True)
    else:props.update(combine_meshes=True,auto_generate_collision=False,remove_degenerates=False,generate_lightmap_u_vs=False)
    for k,v in props.items():data.set_editor_property(k,v)
    return ui
def bones(mesh):return list(unreal.SkeletonService.list_bones(mesh.get_path_name()))

def run():
    export=json.loads((OUT/'export_report.json').read_text());assert export['success']
    readback=json.loads((OUT/'fbx_readback.json').read_text());assert readback['success']
    ink={base:outline(base) for base in (GROUND,SPIDER)}
    original_paths={'Legs':SOURCE+'/Meshes_Skeletal/Mech_Legs_Lt','Armor':SOURCE+'/Meshes/Cockpit_Jet',
        'Shoulder':SOURCE+'/Meshes/HalfShoulder_Box','Machinegun':SOURCE+'/Meshes_Skeletal/Weapons/Weapons_Machinegun_lvl1',
        'SpiderMech':SP_SOURCE+'/Mesh/SpiderMech'}
    for name,row in export['parts'].items():
        REPORT['stage']=name;checkpoint();print('MECH_IMPORT_BEGIN',name,flush=True)
        assert row['fbx_sha256']==readback['parts'][name]['fbx_sha256']
        base=SPIDER if name=='SpiderMech' else GROUND;sk=bool(row['bones']);original=unreal.load_asset(original_paths[name])
        source_bones=bones(original) if sk else []
        surfaces=[]
        for i in range(row['body_material_slots']):
            label=f'{name}_{i:02d}' if name=='SpiderMech' else name
            images={}
            for role,files in row['textures'].items():
                tex=imported(files[i],base+'/Textures/T_'+label+'_'+role)
                tex.set_editor_property('srgb',role!='LineMask')
                tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS if role=='LineMask' else unreal.TextureCompressionSettings.TC_DEFAULT)
                images[role]=save(tex)
            surfaces.append(surface(base,label,images))
        path=base+'/Meshes/'+('SK_' if sk else 'SM_')+name
        mesh=imported(row['fbx'],path,fbx_options(sk,original))
        expected=surfaces+[ink[base]]
        if sk:
            slots=list(mesh.get_editor_property('materials'));assert len(slots)==len(expected),(name,len(slots),len(expected))
            for slot,mat in zip(slots,expected):slot.set_editor_property('material_interface',mat)
            mesh.set_editor_property('materials',slots);mesh.set_editor_property('physics_asset',original.get_editor_property('physics_asset'))
            sub=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
            settings=sub.get_lod_build_settings(mesh,0)
            for k,v in dict(recompute_normals=False,recompute_tangents=False,use_full_precision_u_vs=True).items():settings.set_editor_property(k,v)
            sub.set_lod_build_settings(mesh,0,settings)
            actual=bones(mesh);assert len(actual)==len(source_bones)==row['bones'],name
            max_translation=max_scale=max_angle=0.
            for a,b in zip(source_bones,actual):
                assert a.bone_name==b.bone_name and a.parent_bone_name==b.parent_bone_name,(name,str(a.bone_name),str(b.bone_name))
                x,y=a.local_transform,b.local_transform
                max_translation=max(max_translation,(x.translation-y.translation).length())
                max_scale=max(max_scale,(x.scale3d-y.scale3d).length())
                q,r=x.rotation,y.rotation
                max_angle=max(max_angle,math.degrees(2*math.acos(min(1,abs(q.x*r.x+q.y*r.y+q.z*r.z+q.w*r.w)))))
            assert max_translation<.02 and max_scale<.001 and max_angle<.1,(name,max_translation,max_scale,max_angle)
            assert mesh.skeleton==original.skeleton
            dimensions=mesh.get_imported_bounds().box_extent*2
            contract={'bones':len(actual),'max_reference_translation_cm':max_translation,'max_reference_scale_error':max_scale,'max_reference_angle_deg':max_angle,'skeleton':mesh.skeleton.get_path_name()}
        else:
            for i,mat in enumerate(expected):mesh.set_material(i,mat)
            sub=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem);settings=sub.get_lod_build_settings(mesh,0)
            for k,v in dict(recompute_normals=False,recompute_tangents=False,use_full_precision_u_vs=True).items():settings.set_editor_property(k,v)
            sub.set_lod_build_settings(mesh,0,settings)
            assert mesh.get_num_triangles(0)==row['body_triangles']+row['outline_triangles']
            if name=='Armor':
                for socket_name in ('Mount_Weapon_L','Mount_Weapon_R'):
                    previous=mesh.find_socket(socket_name)
                    if previous:mesh.remove_socket(previous)
                    socket=unreal.StaticMeshSocket(outer=mesh);src=original.find_socket(socket_name)
                    for prop in ('socket_name','relative_location','relative_rotation','relative_scale'):socket.set_editor_property(prop,src.get_editor_property(prop))
                    mesh.add_socket(socket)
            dimensions=mesh.get_bounds().box_extent*2;contract={}
        expected_dimensions=[row['bounds_cm']['max'][i]-row['bounds_cm']['min'][i] for i in range(3)]
        assert max(abs(a-b) for a,b in zip(dimensions.to_tuple(),expected_dimensions))<.05,(name,list(dimensions.to_tuple()),expected_dimensions)
        save(mesh)
        REPORT['parts'][name]={'path':path,'dimensions_cm':list(dimensions.to_tuple()),'body_triangles':row['body_triangles'],'outline_triangles':row['outline_triangles'],'material_slots':len(expected),**contract}
        checkpoint();print('MECH_IMPORT_DONE',name,flush=True)
    factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',LIB.load_blueprint_class(SP_SOURCE+'/SpiderMech_BP'))
    bp=create(SPIDER+'/BP_SpiderMech_Styled',unreal.Blueprint,factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp);cdo=unreal.get_default_object(bp.generated_class())
    bp.modify();cdo.modify();component=cdo.get_component_by_class(unreal.SkeletalMeshComponent);component.modify()
    component.set_skeletal_mesh_asset(unreal.load_asset(REPORT['parts']['SpiderMech']['path']))
    component.set_editor_property('override_materials',[])
    cdo.set_editor_property('auto_possess_player',unreal.AutoReceiveInput.DISABLED)
    save(bp);assert unreal.BlueprintService.compile_blueprint(bp.get_path_name());save(bp)
    REPORT.update(success=True,stage='complete',spider_blueprint=bp.get_path_name(),ground_install='pending live editor reference update')
    checkpoint();print('MECH_STYLE_UE_IMPORT_READY',flush=True)

if __name__=='__main__':
    assert '-MechStyleImportWorker' in unreal.SystemLibrary.get_command_line()
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    flag='Interchange.FeatureFlags.Import.Enable';before=unreal.SystemLibrary.get_console_variable_int_value(flag)
    unreal.SystemLibrary.execute_console_command(world,flag+' 0')
    try:run()
    except Exception:
        REPORT['error']=traceback.format_exc();checkpoint();unreal.log_error(REPORT['error'])
    finally:
        unreal.SystemLibrary.execute_console_command(world,flag+' '+str(before))
        unreal.SystemLibrary.quit_editor()
