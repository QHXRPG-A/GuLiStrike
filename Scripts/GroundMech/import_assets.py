"""Maintain controls and the demo GameMode for the production Ground pawn."""
import unreal, json, traceback
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir())
OUT=ROOT/'TestResults/GroundMech'
BASE='/Game/GuLiStrike/GroundMech'
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
    rocket=action('Ground_RocketJump',consume=True)
    ground_context=create(BASE+'/Input/IMC_GroundMech',unreal.InputMappingContext,unreal.InputMappingContext_Factory())
    def negate(): return unreal.InputModifierNegate(outer=ground_context)
    def swizzle():
        modifier=unreal.InputModifierSwizzleAxis(outer=ground_context); modifier.set_editor_property('order',unreal.InputAxisSwizzle.YXZ); return modifier
    ctx=context('GroundMech',[mapping(move,'W',[swizzle()]),mapping(move,'S',[negate(),swizzle()]),mapping(move,'A',[negate()]),mapping(move,'D'),mapping(sprint,'LeftShift'),mapping(sprint,'RightShift'),mapping(zoom,'MouseWheelAxis'),mapping(rocket,'SpaceBar')])
    keys={'Primary':'LeftMouseButton','Secondary':'RightMouseButton','Cancel':'Escape','Build':'B','Slot1':'One','Slot2':'Two','Slot3':'Three','Slot4':'Four','Slot5':'Five','Slot6':'Six','Select':'Seven','Radius':'Add','ZoomIn':'MouseScrollUp','ZoomOut':'MouseScrollDown','UnitSkill':'Q','Teleport':'T'}
    actions={name:action('Battle_'+name,consume=name=='UnitSkill') for name in keys}
    shift=action('Battle_Shift')
    rows=[mapping(actions[n],key) for n,key in keys.items()]
    battle_context=create(BASE+'/Input/IMC_BattleCommands',unreal.InputMappingContext,unreal.InputMappingContext_Factory())
    chord=unreal.InputTriggerChordAction();chord.set_editor_property('chord_action',shift)
    assert chord.rename(outer=battle_context)
    rows += [mapping(shift,'LeftShift'),mapping(shift,'RightShift'),mapping(actions['Radius'],'Equals',triggers=[chord])]
    context('BattleCommands',rows)
    return {'mapping_context':ctx,'move_action':move,'sprint_action':sprint,'zoom_action':zoom,'rocket_jump_action':rocket}
def run():
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    bp=unreal.load_asset(BASE+'/BP_GroundMech_Light')
    abp=unreal.load_asset(BASE+'/Animations/A4B2/ABP_GroundMech_A4B2')
    assert bp and abp
    cdo=unreal.get_default_object(bp.generated_class())
    assert cdo.get_editor_property('mesh').get_editor_property('anim_class')==abp.generated_class()
    controls=('mapping_context','move_action','sprint_action','zoom_action','rocket_jump_action')
    if any(not cdo.get_editor_property(field) for field in controls):
        bp.modify(); cdo.modify()
        for field,asset in inputs().items(): cdo.set_editor_property(field,asset)
        assert LIB.save_loaded_asset(bp,False)
    factory=unreal.BlueprintFactory();factory.set_editor_property('parent_class',unreal.GuLiCommanderGameMode)
    gm=create(BASE+'/BP_GroundMech_DemoMode',unreal.Blueprint,factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(gm)
    gcdo=unreal.get_default_object(gm.generated_class())
    gcdo.set_editor_property('initial_role_priority',[unreal.GuLiCommanderRole.GROUND,unreal.GuLiCommanderRole.COMMANDER,unreal.GuLiCommanderRole.AIR])
    roles=gcdo.get_editor_property('role_pawn_classes');roles[unreal.GuLiCommanderRole.GROUND]=bp.generated_class();gcdo.set_editor_property('role_pawn_classes',roles)
    save(gm)
    REPORT.update(success=True,stage='complete',blueprint=bp.get_path_name(),animation=abp.get_path_name(),game_mode=gm.get_path_name())

OUT.mkdir(parents=True,exist_ok=True)
try:
    run()
except Exception:
    REPORT['error']=traceback.format_exc()
    raise
finally:
    checkpoint()
