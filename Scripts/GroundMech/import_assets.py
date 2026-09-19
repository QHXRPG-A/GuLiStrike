"""Owned GroundMech asset authoring. Run in a dedicated source-engine editor process."""
import unreal, json, hashlib, traceback
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir())
SOURCE=ROOT/'ArtSource/Mechs/StyleUnification_20260919/GroundMech_UE'
OUT=ROOT/'TestResults/GroundMech'
BASE='/Game/GuLiStrike/GroundMech'
ORIGINAL='/Game/Assets/MechaController/Mech_Constructor_Lt_Med'
LIB=unreal.EditorAssetLibrary
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
OWNER='GuLi.GroundMech.v1'
REPORT={'success':False,'assets':{},'stage':'begin'}

def checkpoint(): (OUT/'asset-import.json').write_text(json.dumps(REPORT,indent=2,ensure_ascii=False),encoding='utf-8')
def save(asset):
    assert asset.get_path_name().startswith(BASE+'/')
    LIB.set_metadata_tag(asset,'GuLi.Owner',OWNER)
    assert LIB.save_loaded_asset(asset,False),asset.get_path_name()
    return asset
def get(path):
    if not LIB.does_asset_exist(path): return None
    asset=unreal.load_asset(path)
    assert LIB.get_metadata_tag(asset,'GuLi.Owner')==OWNER,path
    return asset
def create(path,cls,factory):
    asset=get(path)
    if asset: return asset
    folder,name=path.rsplit('/',1); LIB.make_directory(folder)
    return save(TOOLS.create_asset(name,folder,cls,factory))
def imported(file,path,options=None,factory=None):
    sha=hashlib.sha256(Path(file).read_bytes()).hexdigest(); old=get(path)
    if old and LIB.get_metadata_tag(old,'GuLi.SourceSHA256')==sha: return old
    folder,name=path.rsplit('/',1); LIB.make_directory(folder)
    task=unreal.AssetImportTask()
    for k,v in dict(filename=str(file),destination_path=folder,destination_name=name,automated=True,async_=False,replace_existing=bool(old),save=False).items(): task.set_editor_property(k,v)
    if options: task.options=options
    if factory: task.factory=factory
    TOOLS.import_asset_tasks([task]); asset=unreal.load_asset(path); assert asset,path
    LIB.set_metadata_tag(asset,'GuLi.SourceSHA256',sha)
    return save(asset)
def action(name,kind=unreal.InputActionValueType.BOOLEAN,consume=False):
    a=create(BASE+'/Input/IA_'+name,unreal.InputAction,unreal.InputAction_Factory())
    a.set_editor_property('value_type',kind); a.set_editor_property('consume_input',consume)
    return save(a)
def mapping(action,key,modifiers=(),triggers=()):
    k=unreal.Key();k.set_editor_property('key_name',key)
    m=unreal.EnhancedActionKeyMapping()
    for name,value in {'action':action,'key':k,'modifiers':list(modifiers),'triggers':list(triggers)}.items():m.set_editor_property(name,value)
    return m
def context(name,rows):
    c=create(BASE+'/Input/IMC_'+name,unreal.InputMappingContext,unreal.InputMappingContext_Factory())
    data=c.get_editor_property('default_key_mappings'); data.set_editor_property('mappings',rows)
    c.set_editor_property('default_key_mappings',data)
    return save(c)
def inputs():
    move=action('Ground_Move',unreal.InputActionValueType.AXIS2D,True)
    move.set_editor_property('accumulation_behavior',unreal.InputActionAccumulationBehavior.CUMULATIVE);save(move)
    sprint=action('Ground_Sprint',consume=True)
    zoom=action('Ground_Zoom',unreal.InputActionValueType.AXIS1D,True)
    ground_context=create(BASE+'/Input/IMC_GroundMech',unreal.InputMappingContext,unreal.InputMappingContext_Factory())
    def negate(): return unreal.InputModifierNegate(outer=ground_context)
    def swizzle():
        modifier=unreal.InputModifierSwizzleAxis(outer=ground_context); modifier.set_editor_property('order',unreal.InputAxisSwizzle.YXZ); return modifier
    ctx=context('GroundMech',[mapping(move,'W',[swizzle()]),mapping(move,'S',[negate(),swizzle()]),mapping(move,'A',[negate()]),mapping(move,'D'),mapping(sprint,'LeftShift'),mapping(sprint,'RightShift'),mapping(zoom,'MouseWheelAxis')])
    keys={'Primary':'LeftMouseButton','Secondary':'RightMouseButton','Cancel':'Escape','Build':'B','Slot1':'One','Slot2':'Two','Slot3':'Three','Slot4':'Four','Slot5':'Five','Slot6':'Six','Select':'Seven','Radius':'Add','ZoomIn':'MouseScrollUp','ZoomOut':'MouseScrollDown','UnitSkill':'Q','Teleport':'T'}
    actions={name:action('Battle_'+name,consume=name=='UnitSkill') for name in keys}
    shift=action('Battle_Shift')
    rows=[mapping(actions[n],key) for n,key in keys.items()]
    battle_context=create(BASE+'/Input/IMC_BattleCommands',unreal.InputMappingContext,unreal.InputMappingContext_Factory())
    chord=unreal.InputTriggerChordAction();chord.set_editor_property('chord_action',shift)
    assert chord.rename(outer=battle_context)
    rows += [mapping(shift,'LeftShift'),mapping(shift,'RightShift'),mapping(actions['Radius'],'Equals',triggers=[chord])]
    context('BattleCommands',rows)
    return {'mapping_context':ctx,'move_action':move,'sprint_action':sprint,'zoom_action':zoom}
def material(name):
    existing=get(BASE+'/Materials/M_'+name)
    if existing:return existing # v1 shader graphs are authored once; bone reimports leave them intact.
    mat=create(BASE+'/Materials/M_'+name,unreal.Material,unreal.MaterialFactoryNew())
    edit=unreal.MaterialEditingLibrary; edit.delete_all_material_expressions(mat)
    for role in ['BaseColor','ORM','Emissive']:
        tex=imported(SOURCE/(name+'_'+role+'.png'),BASE+'/Textures/T_'+name+'_'+role,factory=unreal.TextureFactory())
        tex.set_editor_property('srgb',role!='ORM')
        tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS if role=='ORM' else unreal.TextureCompressionSettings.TC_DEFAULT); save(tex)
        n=edit.create_material_expression(mat,unreal.MaterialExpressionTextureSample)
        n.texture=tex; n.sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS if role=='ORM' else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR
        links={'BaseColor':[('RGB',unreal.MaterialProperty.MP_BASE_COLOR)],'ORM':[('R',unreal.MaterialProperty.MP_AMBIENT_OCCLUSION),('G',unreal.MaterialProperty.MP_ROUGHNESS),('B',unreal.MaterialProperty.MP_METALLIC)],'Emissive':[('RGB',unreal.MaterialProperty.MP_EMISSIVE_COLOR)]}[role]
        for output,prop in links: assert edit.connect_material_property(n,output,prop)
    mat.set_editor_property('used_with_skeletal_mesh',True); edit.recompile_material(mat)
    return save(mat)
def mesh(name,source):
    skeletal=name in ('Legs','Machinegun')
    original=unreal.load_asset(source)
    ui=unreal.FbxImportUI()
    kind=unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else unreal.FBXImportType.FBXIT_STATIC_MESH
    for k,v in dict(import_materials=False,import_textures=False,import_mesh=True,import_as_skeletal=skeletal,import_animations=False,create_physics_asset=False,automated_import_should_detect_type=False,mesh_type_to_import=kind,original_import_type=kind).items():ui.set_editor_property(k,v)
    if skeletal: ui.skeleton=original.skeleton
    data=ui.skeletal_mesh_import_data if skeletal else ui.static_mesh_import_data
    for k,v in dict(convert_scene=True,convert_scene_unit=True,force_front_x_axis=False,import_uniform_scale=1.0,normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS).items(): data.set_editor_property(k,v)
    if skeletal:
        data.set_editor_property('update_skeleton_reference_pose',False)
        data.set_editor_property('use_t0_as_ref_pose',False)
        data.set_editor_property('preserve_smoothing_groups',True)
    else:
        data.set_editor_property('combine_meshes',True);data.set_editor_property('auto_generate_collision',False)
    prefix='SK_' if skeletal else 'SM_'
    m=imported(SOURCE/(name+'.fbx'),BASE+'/Meshes/'+prefix+name,ui,unreal.FbxFactory())
    mat=material(name)
    if skeletal:
        slots=m.get_editor_property('materials');slots[0].material_interface=mat;m.set_editor_property('materials',slots)
        bounds=m.get_imported_bounds()
        bones=list(unreal.SkeletonService.list_bones(m.get_path_name()))
        REPORT['assets'][name]={'bones':[{'name':str(b.bone_name),'parent':str(b.parent_bone_name),'local':str(b.local_transform)} for b in bones],'skeleton':m.skeleton.get_path_name()}
    else:
        m.set_material(0,mat);bounds=m.get_bounds();REPORT['assets'][name]={}
    if name=='Armor':
        for socket_name in ('Mount_Weapon_L','Mount_Weapon_R'):
            existing=m.find_socket(socket_name)
            if existing:m.remove_socket(existing)
            src=original.find_socket(socket_name); s=unreal.StaticMeshSocket(outer=m)
            for prop in ('socket_name','relative_location','relative_rotation','relative_scale'):s.set_editor_property(prop,src.get_editor_property(prop))
            m.add_socket(s)
    REPORT['assets'][name].update(path=m.get_path_name(),size_cm=list((bounds.box_extent*2).to_tuple()))
    save(m);checkpoint();return m
def run():
    REPORT['stage']='inputs';checkpoint();input_assets=inputs()
    meshes={}
    paths={'Legs':'Meshes_Skeletal/Mech_Legs_Lt','Armor':'Meshes/Cockpit_Jet','Shoulder':'Meshes/HalfShoulder_Box','Machinegun':'Meshes_Skeletal/Weapons/Weapons_Machinegun_lvl1'}
    for name,path in paths.items():
        REPORT['stage']=name;checkpoint();meshes[name]=mesh(name,ORIGINAL+'/'+path)
    bs=unreal.load_asset('/Game/Assets/MechaController/Blueprints/SalvaMeshIntegration/Examples/Mech_Legs_Lt_IdleToRunWithTurnRate')
    REPORT['blendspace_axes']=[{p:str(axis.get_editor_property(p)) for p in ('display_name','min','max')} for axis in bs.get_editor_property('blend_parameters')]
    REPORT['blendspace_samples']=[{'animation':sample.get_editor_property('animation').get_path_name(),'position':list(sample.get_editor_property('sample_value').to_tuple())} for sample in bs.get_editor_property('sample_data')];checkpoint()
    REPORT['stage']='animation';checkpoint()
    factory=unreal.AnimBlueprintFactory();factory.set_editor_property('target_skeleton',meshes['Legs'].skeleton)
    factory.set_editor_property('parent_class',unreal.GuLiGroundMechAnimInstance)
    abp=create(BASE+'/Animations/ABP_GroundMech',unreal.AnimBlueprint,factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    acdo=unreal.get_default_object(abp.generated_class());acdo.set_editor_property('locomotion_blend_space',bs)
    # Source locomotion carries root translation. Extract it from the pose without
    # applying it to the Character, whose movement is driven solely by CMC input.
    assert unreal.BlueprintService.set_property(abp.get_path_name(),'RootMotionMode','IgnoreRootMotion')
    # Refresh the generated class's property initialization list after changing
    # defaults; otherwise new instances can still use MontagesOnly in this session.
    unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    save(abp)
    factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.GuLiGroundMechCharacter)
    bp=create(BASE+'/BP_GroundMech_Light',unreal.Blueprint,factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo=unreal.get_default_object(bp.generated_class())
    for name,value in input_assets.items():cdo.set_editor_property(name,value)
    # Native default subobjects belong to this Blueprint CDO, not its SCS graph.
    bp.modify();cdo.modify()
    legs=cdo.get_editor_property('mesh');legs.modify()
    legs.set_skeletal_mesh_asset(meshes['Legs']);legs.set_anim_instance_class(abp.generated_class())
    for part,prop in [('Armor','armor'),('Shoulder','shoulder')]:
        component=cdo.get_editor_property(prop);component.modify();component.set_static_mesh(meshes[part])
    gun=cdo.get_editor_property('machinegun');gun.modify();gun.set_skeletal_mesh_asset(meshes['Machinegun'])
    save(bp)
    factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.GuLiCommanderGameMode)
    gm=create(BASE+'/BP_GroundMech_DemoMode',unreal.Blueprint,factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(gm)
    gcdo=unreal.get_default_object(gm.generated_class())
    gcdo.set_editor_property('initial_role_priority',[unreal.GuLiCommanderRole.GROUND,unreal.GuLiCommanderRole.COMMANDER,unreal.GuLiCommanderRole.AIR])
    roles=gcdo.get_editor_property('role_pawn_classes');roles[unreal.GuLiCommanderRole.GROUND]=bp.generated_class();gcdo.set_editor_property('role_pawn_classes',roles)
    save(gm)
    REPORT.update(success=True,stage='complete',blueprint=bp.get_path_name(),animation=abp.get_path_name(),game_mode=gm.get_path_name())

original_flag=unreal.SystemLibrary.get_console_variable_bool_value('Interchange.FeatureFlags.Import.Enable')
try:
    unreal.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.Enable 0')
    run()
except Exception:REPORT['error']=traceback.format_exc()
finally:
    unreal.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.Enable '+str(int(original_flag)))
    checkpoint()
