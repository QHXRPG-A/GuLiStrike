"""Save the existing Ground review area and export its real population manifests.
Only structured actor/configuration readback is performed; no PIE or visual testing.
"""
import json
import math
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'Artifacts/BurningRogue_20261029'
MAP='/Game/Maps/LVL_CommanderMassPrototype'

def require(value,message):
    if not value:raise RuntimeError(message)
    return value

def main():
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    require(not level.is_in_play_in_editor(),'Preserve active play session')
    dirty=unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    own_retry=globals().get('GULI_BURNING_RETRY_OWNED_MAP',False)
    require(not dirty or (own_retry and len(dirty)==1 and dirty[0].get_name()==MAP),'Preserve unsaved map work')
    if not dirty:require(level.load_level(MAP),'Load production map')
    subsystem=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    all_actors=subsystem.get_all_level_actors()
    before={a.get_actor_label():a.get_actor_transform().to_tuple() for a in all_actors
            if not a.get_actor_label().startswith(('GroundRogue_','GroundBuff_'))}
    actors={a.get_actor_label():a for a in all_actors}
    related=[]
    def get(label,cls,position):
        a=actors.get(label)
        if a is None:
            a=require(subsystem.spawn_actor_from_class(cls,unreal.Vector(*position)),'Spawn '+label)
            a.set_actor_label(label);actors[label]=a
        require(isinstance(a,cls),'Unexpected actor class '+label)
        a.modify();a.set_actor_location(unreal.Vector(*position),False,False)
        tags=list(a.get_editor_property('tags'))
        if unreal.Name('GuLi.Review.GroundBuff') not in tags:tags.append(unreal.Name('GuLi.Review.GroundBuff'))
        a.set_editor_property('tags',tags);related.append(label)
        return a
    floor=require(actors.get('GroundRogue_Floor'),'Existing Ground rogue floor')
    floor.modify();floor.set_actor_scale3d(unreal.Vector(250,65,.5));related.append('GroundRogue_Floor')
    for label,a in actors.items():
        if label.startswith('GroundRogue_') and isinstance(a,unreal.GuLiCommanderDeploymentPoint):
            a.modify();a.set_editor_property('use_in_population_manifest',True)
            a.set_editor_property('allow_automatic_fire',False);a.set_editor_property('start_idle',True)
            related.append(label)
    # Keep the range boundary target on a valid authored grid cell, beyond the chain.
    get('GroundRogue_OutOfRange',unreal.GuLiCommanderDeploymentPoint,(-15150,54750,905))
    cases=[('Health750',(-5200,51750,905),1,750,0,unreal.GuLiTeam.BLUE),
           ('Shield250',(-7000,51750,905),1,1000,250,unreal.GuLiTeam.BLUE),
           ('LargeBurnHost',(-19650,54000,905),6,3000,0,unreal.GuLiTeam.BLUE),
           ('BehindBurstWall',(-17250,54000,905),1,1000,0,unreal.GuLiTeam.BLUE),
           ('FriendlyInBurst',(-19650,56100,905),1,1000,0,unreal.GuLiTeam.RED)]
    for suffix,position,unit,hp,shield,team in cases:
        a=get('GroundBuff_'+suffix,unreal.GuLiCommanderDeploymentPoint,position)
        for key,value in [('team',team),('unit_type_id',unit),('rows',1),('columns',1),
                          ('spacing_centimeters',600.),('allow_automatic_fire',False),('start_idle',True),
                          ('use_in_population_manifest',True),('initial_max_health_override',float(hp)),('initial_shield',float(shield))]:
            a.set_editor_property(key,value)
    # Odd footprints use cell centers; diagonal rounding from old freehand points
    # can otherwise exceed the canonical 150 cm projection allowance.
    for label,a in actors.items():
        if label.startswith(('GroundRogue_','GroundBuff_')) and isinstance(a,unreal.GuLiCommanderDeploymentPoint) and a.get_editor_property('unit_type_id')==1:
            p=a.get_actor_location()
            a.set_actor_location(unreal.Vector(math.floor(p.x/300)*300+150,math.floor(p.y/300)*300+150,p.z),False,False)
    wall=get('GroundBuff_BurstWall',unreal.StaticMeshActor,(-17850,54000,2205))
    wall.set_actor_scale3d(unreal.Vector(.3,8,26))
    mesh=wall.static_mesh_component;mesh.modify()
    mesh.set_static_mesh(require(unreal.load_asset('/Engine/BasicShapes/Cube'),'Cube'))
    mesh.set_mobility(unreal.ComponentMobility.STATIC);mesh.set_collision_profile_name('BlockAll')
    mesh.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    note=get('GroundBuff_Instructions',unreal.Note,(-2800,54000,1200))
    note.set_editor_property('text','Red Ground：F4 先取灼烧子弹，再取灼烧爆裂子弹；折射可重复获取。\n'
        '东侧原折射群；南侧750生命/250护盾靶；西侧大型宿主、薄墙后的敌人及友军。\n'
        '爆裂从首次启用后独立2秒计时，刷新不重置；按8秒边界观察。\n'
        '助手仅保存与回读场景，未运行PIE；画面、伤害与迁移重试由玩家验收。')
    # Export first so invalid authored positions cannot silently shift to another area.
    manifests=[]
    for population in (200,600):
        path=ROOT/f'Config/Hybrid/RuntimeManifest-{population}.json'
        result=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population,str(path)))
        require(result.get('success'),'Manifest export: '+json.dumps(result))
        validated=json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(path)))
        require(validated.get('success'),'Manifest readback: '+json.dumps(validated))
        manifests.append({'population':population,'export':result,'validation':validated})
    require(unreal.EditorLoadingAndSavingUtils.save_map(world,MAP),'Save Ground scene')
    require(level.load_level(MAP),'Reload saved Ground scene')
    saved={a.get_actor_label():a for a in subsystem.get_all_level_actors()}
    require(before=={k:saved[k].get_actor_transform().to_tuple() for k in before},'Preserve unrelated actors')
    entities=[]
    for label in sorted(set(related)):
        a=require(saved.get(label),'Saved actor '+label)
        row={'label':label,'class':a.get_class().get_path_name(),'position':list(a.get_actor_location().to_tuple()),
             'scale':list(a.get_actor_scale3d().to_tuple()),'map':a.get_outermost().get_name()}
        if isinstance(a,unreal.GuLiCommanderDeploymentPoint):
            row.update({p:str(a.get_editor_property(p)) for p in ['team','unit_type_id','use_in_population_manifest',
                'allow_automatic_fire','start_idle','initial_max_health_override','initial_shield']})
            require(a.get_editor_property('use_in_population_manifest') and not a.get_editor_property('allow_automatic_fire')
                    and a.get_editor_property('start_idle'),'Saved passive deployment '+label)
        if isinstance(a,unreal.StaticMeshActor):
            row.update(mesh=a.static_mesh_component.static_mesh.get_path_name(),collision=str(a.static_mesh_component.get_collision_enabled()))
        entities.append(row)
    # Use the same resource gate as the normal PIE authorizer, not just the
    # population exporter. A saved manifest alone does not certify play readiness.
    bake=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    require(bake.get_editor_property('success'),'PIE resource gate: '+str(bake.get_editor_property('message')))
    navigation=unreal.GuLiNavigationBakeLibrary.validate_world_navigation(
        unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world())
    require(navigation.get_editor_property('success'),'PIE navigation gate: '+str(navigation.get_editor_property('message')))
    return {'success':True,'saved':True,'map':MAP,'entities':entities,'manifests':manifests,
            'resource_preflight':True,'navigation_preflight':True,
            'entry':'GroundRogue_Entry (-2800,54000,1149.04); Red Ground; F4',
            'PIE':False,'assistant_visual_test':False,'runtime_verified':False,'visual_acceptance':'pending_user'}

try:report=main()
except Exception:report={'success':False,'error':traceback.format_exc()}
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'scene_delivery.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'error':report.get('error'),'report':str(OUT/'scene_delivery.json')},ensure_ascii=False))
