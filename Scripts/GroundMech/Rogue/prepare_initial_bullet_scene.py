"""Save only Ground ammo review entities; export manifests only after the new import."""
import json
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'Artifacts/GroundAmmo_20261108'
MAP='/Game/Maps/LVL_CommanderMassPrototype'
PREFIX='GroundAmmo_'

def require(v,message):
    if not v:raise RuntimeError(message)
    return v

def prepare_scene_dependencies(world, actors):
    # Deployment identities participate in the resource source hash. Prove that
    # only these additions changed before refreshing the existing ore layout.
    resource=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    if not resource.success:
        additions=[a for label,a in actors.items() if label.startswith(PREFIX)
                   and isinstance(a,unreal.GuLiCommanderDeploymentPoint)]
        resource=unreal.GuLiResourceAuthoringLibrary.revalidate_added_deployments(additions,True)
    require(resource.success,'Resource deployment certification: '+str(resource.message))
    navigation=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(world,True)
    require(navigation.success,'Save navigation for authored collision: '+str(navigation.message))
    # Read the saved dependent assets as well as the map before reporting ready.
    assets=[require(unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap'),'Resource definition')]
    for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if isinstance(a,unreal.GuLiFlightNavigationVolume):
            assets.append(require(a.get_editor_property('navigation_data'),'Flight navigation data'))
    reloaded,error=unreal.EditorLoadingAndSavingUtils.reload_packages(
        [a.get_outer() for a in assets],unreal.ReloadPackagesInteractionMode.ASSUME_NEGATIVE)
    require(reloaded and not str(error),'Reload saved scene dependencies: '+str(error))
    return {'resource':str(resource.message),'ground_rebuilds':navigation.ground_rebuilds,
            'flight_rebuilds':navigation.flight_rebuilds,'saved_assets_reloaded':True}

def main():
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    require(not level.is_in_play_in_editor(),'Preserve active play')
    require(not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() or globals().get('GULI_AMMO_OWNED_MAP_RETRY',False),'Preserve unsaved map edits')
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world.get_path_name().split('.')[0]!=MAP:require(level.load_level(MAP),'Load existing prototype map')
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors={a.get_actor_label():a for a in sub.get_all_level_actors()}
    require('GroundRogue_Entry' in actors and 'GroundRogue_Floor' in actors,'Existing Ground review area')
    before={k:a.get_actor_transform().to_tuple() for k,a in actors.items() if not k.startswith(PREFIX)}
    related=[]
    def actor(name,cls,position):
        label=PREFIX+name;a=actors.get(label)
        if a is None:a=require(sub.spawn_actor_from_class(cls,unreal.Vector(*position)),'Spawn '+label);a.set_actor_label(label);actors[label]=a
        require(isinstance(a,cls),'Preserve unexpected actor class '+label)
        a.modify();a.set_actor_location(unreal.Vector(*position),False,False)
        tags=list(a.get_editor_property('tags'));tag=unreal.Name('GuLi.Review.GroundAmmo')
        if tag not in tags:tags.append(tag)
        a.set_editor_property('tags',tags);related.append(label);return a
    cases=[('PrimaryDamage',(150,52050,905),1,unreal.GuLiTeam.BLUE,100000,0),
           ('BlastCenter',(-750,56550,905),1,unreal.GuLiTeam.BLUE,100000,0),
           ('Inside12m',(-750,55350,905),1,unreal.GuLiTeam.BLUE,100000,0),
           ('Boundary15m',(750,56550,905),1,unreal.GuLiTeam.BLUE,100000,0),
           ('Outside24m',(1650,56550,905),1,unreal.GuLiTeam.BLUE,100000,0),
           ('Friendly',(450,55950,905),1,unreal.GuLiTeam.RED,100000,0),
           ('Shield',(-1950,56550,905),1,unreal.GuLiTeam.BLUE,100000,1000),
           ('BehindWall',(-750,55950,905),1,unreal.GuLiTeam.BLUE,100000,0),
           ('Pioneer',(-12150,51750,905),1,unreal.GuLiTeam.RED,100000,0),
           ('HeavyDefense',(-16350,55350,905),2,unreal.GuLiTeam.RED,100000,0),
           ('Sweeper',(-16200,51600,905),5,unreal.GuLiTeam.RED,100000,0)]
    for name,position,unit,team,hp,shield in cases:
        a=actor(name,unreal.GuLiCommanderDeploymentPoint,position)
        for key,value in [('team',team),('unit_type_id',unit),('rows',1),('columns',1),('spacing_centimeters',600.),
                          ('start_idle',True),('allow_automatic_fire',False),('use_in_population_manifest',True),
                          ('initial_max_health_override',float(hp)),('initial_shield',float(shield))]:a.set_editor_property(key,value)
    wall=actor('BlockingWall',unreal.StaticMeshActor,(-750,54750,1505))
    wall.set_actor_scale3d(unreal.Vector(8,.6,12));mesh=wall.static_mesh_component;mesh.modify()
    mesh.set_static_mesh(require(unreal.load_asset('/Engine/BasicShapes/Cube'),'Wall cube'))
    mesh.set_mobility(unreal.ComponentMobility.STATIC);mesh.set_collision_profile_name('BlockAll');mesh.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    instructions={
        'Entry': '从 GroundRogue_Entry 进入红队游骑兵。初始普攻伤害6/9/12；10.01强化后10/15/20。南侧100000生命靶读单发差值；先驱者/扫荡者半宽半长直线，重防号原样。三台红方对照单位默认待机，在Commander席手动命令开火。',
        'AttackRate': 'F4取09.01一级、二级，HUD攻速为R*1.08和R*1.08^2。R=4/8/12时最多26/17/11级。再升一级不出候选；超过30的武器换档被拒绝。保留开火冷却剩余比例。',
        'Prerequisites': '新局Ground候选可有09.01/10.01/11.01，不含06.01/07.01/08.01。先取10.01再观察06.01/07.01；07.01后才有08.01。三张新牌空白但保留卡框、文字、HUD空槽及等级。',
        'Explosion': '11.01三级分别每5/4/2发普攻附一枚。先打空n-1发，下一发命中BlastCenter。普攻与20/30/40范围伤害分别结算，未强化也为2D。附近12米、15米边界、24米外、友军、护盾和墙后靶；边界使用实际受击体，不只按演员中心。超时消失不爆炸；爆炸不产生灼烧或折射。',
        'Recovery': '先打空积累进度，停火、死亡复活、换席再回来，下一阈值保持；首次取得/升级11.01归零。发射后马上升级10.01或11.01，旧弹保持旧快照。用现有Hybrid恢复入口恢复同局；ReadState的builds.shot_progress、attacks.revision/companion、explosions.hits事件ID与伤害应保持，重复回执不得新增扣血。新局清空。',
    }
    for index,(name,text) in enumerate(instructions.items()):
        note=actor('Instructions_'+name,unreal.Note,(-2800+index*150,54000,1200))
        note.set_editor_property('text',text)
    actor('ObserveExplosion',unreal.TargetPoint,(-2850,54150,1149.04))
    actor('ObserveDamage',unreal.TargetPoint,(-1950,52050,1149.04))
    # Existing notes must reflect the new prerequisite, without replacing unrelated content.
    notes=[]
    for name in ('GroundBuff_Instructions','GroundRogue_Instructions'):
        if name in actors and isinstance(actors[name],unreal.Note):
            a=actors[name];old=str(a.get_editor_property('text'));prefix='当前前置：先取得强化子弹10.01，再取折射06.01/灼烧07.01；灼烧爆裂08.01仍依赖07.01。\n'
            if not old.startswith(prefix):a.modify();a.set_editor_property('text',prefix+old)
            notes.append(name)
    require(unreal.EditorLoadingAndSavingUtils.save_map(world,MAP),'Save authored scene')
    dependencies=prepare_scene_dependencies(world,actors)
    require(level.load_level(MAP),'Reload saved scene')
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    saved={a.get_actor_label():a for a in sub.get_all_level_actors()}
    require(before=={k:saved[k].get_actor_transform().to_tuple() for k in before},'Preserve unrelated transforms')
    entities=[]
    for label in related+notes:
        a=require(saved.get(label),'Saved entity '+label)
        row={'label':label,'class':a.get_class().get_path_name(),'position':a.get_actor_location().to_tuple(),'scale':a.get_actor_scale3d().to_tuple()}
        if isinstance(a,unreal.GuLiCommanderDeploymentPoint):
            row.update({key:str(a.get_editor_property(key)) for key in ['unit_type_id','team','rows','columns','start_idle','allow_automatic_fire','use_in_population_manifest','initial_max_health_override','initial_shield']})
            require(a.get_editor_property('use_in_population_manifest') and a.get_editor_property('start_idle') and not a.get_editor_property('allow_automatic_fire'),'Saved passive target')
        if isinstance(a,unreal.Note):row['text']=str(a.get_editor_property('text'))
        if isinstance(a,unreal.StaticMeshActor):row['collision']=str(a.static_mesh_component.get_collision_enabled())
        entities.append(row)
    imported=OUT/'table_import.json';ready=imported.exists() and json.loads(imported.read_text(encoding='utf8')).get('success',False)
    manifests=[]
    if ready:
        for population in (200,600):
            path=ROOT/f'Config/Hybrid/RuntimeManifest-{population}.json'
            result=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population,str(path)))
            require(result.get('success'),'Export manifest '+json.dumps(result))
            checked=json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(path)))
            require(checked.get('success'),'Read manifest '+json.dumps(checked))
            manifests.append({'population':population,'export':result,'readback':checked})
    resource=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    navigation=unreal.GuLiNavigationBakeLibrary.validate_world_navigation(world)
    require(resource.success and navigation.success,'Saved resource/navigation certification: '+str(resource.message)+'; '+str(navigation.message))
    dependencies.update({'resource_readback':str(resource.message),'navigation_readback':str(navigation.message),
                         'resource_ready':bool(resource.success),'navigation_ready':bool(navigation.success)})
    return {'success':True,'scene_saved':True,'entities':entities,'map':MAP,'native_data_ready':bool(ready),'manifests':manifests,'dependencies':dependencies,
            'play_ready':bool(ready and len(manifests)==2),'PIE':False,'assistant_visual_test':False,'player_acceptance':'pending'}

try:report=main()
except Exception:report={'success':False,'error':traceback.format_exc()}
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'scene_delivery.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({k:report.get(k) for k in ['success','scene_saved','play_ready','error']},ensure_ascii=False))
