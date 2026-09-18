"""User-approved visual-only factory door. Save only its project Blueprint, never physics/source meshes."""
import json,os,traceback
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'TestResults/Scale020'
BP='/Game/GuLiStrike/Buildings/ResourceProcessingFactory/Blueprints/BP_ResourceProcessingFactory'
B=unreal.BlueprintService
report={'version':1,'asset':BP,'component':'Door','policy':'presentation_only','success':False,
        'unchanged':['Body','AccessRamp','source skeletal mesh','physics asset','door animation and timing'],
        'user_authorized':'关闭门的碰撞，没有必要有碰撞，本身就是表现'}
def snapshot():
    result={}
    for name in ('Door','Body','AccessRamp'):
        assert B.component_exists(BP,name),name
        result[name]={}
        for field in ('BodyInstance','bGenerateOverlapEvents','bCanEverAffectNavigation',
                      'RelativeLocation','RelativeRotation','RelativeScale3D'):
            value=B.get_component_property(BP,name,field)
            assert value is not None,(name,field)
            result[name][field]=str(value)
    return result
def instance_readback():
    subsystem=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor=subsystem.spawn_actor_from_class(unreal.EditorAssetLibrary.load_blueprint_class(BP),unreal.Vector())
    assert actor
    try:
        components={c.get_name():c for c in actor.get_components_by_class(unreal.PrimitiveComponent)}
        return {name:{'collision':str(components[name].get_collision_enabled()),
                      'no_collision':components[name].get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION}
                for name in ('Door','Body','AccessRamp')}
    finally: assert subsystem.destroy_actor(actor)
try:
    asset=unreal.load_asset(BP);assert asset
    before=snapshot();report['before']=before
    baseline=OUT/'factory-door-collision-baseline.json'
    if not baseline.exists():baseline.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    door=before['Door']
    report['instance_before']=instance_readback()
    target=(report['instance_before']['Door']['no_collision']
            and door['bGenerateOverlapEvents'].lower()=='false' and door['bCanEverAffectNavigation'].lower()=='false')
    report['changed']=not target
    if not target:
        assert B.set_collision_settings(BP,'Door','NoCollision','','NoCollision',{})
        assert B.set_component_property(BP,'Door','bGenerateOverlapEvents','False')
        assert B.set_component_property(BP,'Door','bCanEverAffectNavigation','False')
        result=B.compile_blueprint(BP)
        report['compile']={'success':result.success,'errors':list(result.errors),'warnings':list(result.warnings)}
        assert result.success,report['compile']
    after=snapshot();report['after']=after
    assert after['Body']==before['Body'],'Body changed'
    assert after['AccessRamp']==before['AccessRamp'],'AccessRamp changed'
    for field in ('RelativeLocation','RelativeRotation','RelativeScale3D'):
        assert after['Door'][field]==before['Door'][field],field
    report['instance_after']=instance_readback()
    assert report['instance_after']['Door']['no_collision'],report['instance_after']
    for name in ('Body','AccessRamp'):
        assert report['instance_after'][name]==report['instance_before'][name],name
    assert after['Door']['bGenerateOverlapEvents'].lower()=='false'
    assert after['Door']['bCanEverAffectNavigation'].lower()=='false'
    if report['changed']:
        unreal.EditorAssetLibrary.set_metadata_tag(asset,'GuLi.RPF.DoorCollisionPolicy','PresentationOnly')
        assert unreal.EditorAssetLibrary.save_loaded_asset(asset,False)
    report['success']=True
except Exception: report['error']=traceback.format_exc()
finally:
    name=os.environ.get('GULI_SCALE020_DOOR_REPORT','factory-door-collision-readback.json')
    assert Path(name).name==name
    (OUT/name).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.SystemLibrary.quit_editor()
