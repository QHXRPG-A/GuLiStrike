"""Revalidate explicit review deployments and refresh production manifests without play."""
import hashlib
import json
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'Data/MapAuthoringValidation/PIEStart-20261029'
MAP='/Game/Maps/LVL_CommanderMassPrototype'
DEFINITION='/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap'


def bake_result(value):
    keys=('success','message','source_hash','layout_hash','territory_count','cluster_count','node_count',
          'initial_soldier_count','validated_initial_soldier_count')
    result={key:value.get_editor_property(key) for key in keys}
    result['issues']=list(map(str,value.get_editor_property('issues')))
    return result


def resource_state():
    asset=unreal.load_asset(DEFINITION)
    return {'asset':asset.get_path_name(),'source_hash':asset.get_editor_property('source_hash'),
            'layout_hash':asset.get_editor_property('layout_hash'),
            'territories':len(asset.get_editor_property('territories')),
            'clusters':len(asset.get_editor_property('clusters')),'nodes':len(asset.get_editor_property('nodes'))}


def source_actors():
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    return sorted((a.get_path_name(),a.get_actor_transform().export_text()) for a in actors
                  if isinstance(a,(unreal.GuLiMapMarker,unreal.GuLiMapDensityMap)))


def geometry_digest():
    asset=unreal.load_asset(DEFINITION)
    digest=hashlib.sha256()
    for key in ('map_package','layout_version','baked_object_scale','baked_grid_cell_cm','deterministic_seed',
                'playable_minimum','playable_maximum','has_authored_playable_bounds','spawn_anchors',
                'territories','clusters','nodes'):
        value=asset.get_editor_property(key)
        values=value if key in ('territories','clusters','nodes') else [value]
        for item in values:
            text=item.export_text() if hasattr(item,'export_text') else str(item)
            digest.update((key+'\0'+text+'\n').encode('utf8'))
    return digest.hexdigest()


def added_deployments():
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    points=sorted((a for a in actors if isinstance(a,unreal.GuLiCommanderDeploymentPoint)
                   and a.get_actor_label().startswith('GroundRogue_')),key=lambda a:a.get_actor_label())
    expected={f'GroundRogue_Enemy_{i:02}' for i in range(1,10)}|{
        'GroundRogue_BehindWall','GroundRogue_OutOfRange','GroundRogue_Friendly'}
    assert {a.get_actor_label() for a in points}==expected,'Unexpected added deployment scope'
    for a in points:
        assert a.rows==1 and a.columns==1 and a.unit_type_id==1
        assert not a.allow_automatic_fire and a.start_idle,'Review target configuration changed'
    return points


def repair():
    OUT.mkdir(parents=True,exist_ok=True)
    report={'success':False,'date':'2026-10-29','scope':'Added deployment revalidation and production manifests only',
            'PIE_started':False,'native_build':False}
    try:
        level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        assert not level.is_in_play_in_editor(),'Do not interrupt play'
        world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        assert world.get_path_name().split('.')[0]==MAP,'Unexpected editor map'
        assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Save authored map changes first'
        report['map']=world.get_path_name()
        report['before']=bake_result(unreal.GuLiResourceAuthoringLibrary.validate_current_bake())
        report['resource_before']=resource_state()
        report['geometry_before']=geometry_digest()
        before_actors=source_actors()
        report['source_actor_count']=len(before_actors)
        report['manifests_before']={str(population):hashlib.sha256((ROOT/f'Config/Hybrid/RuntimeManifest-{population}.json').read_bytes()).hexdigest()
                                    for population in (200,600)}
        (OUT/'incremental_before.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
        if not report['before']['success']:
            assert report['before']['message']=='GuLiMapAuthoring source changed after the last resource bake; rebake before PIE.',report['before']
            assert hasattr(unreal.GuLiResourceAuthoringLibrary,'revalidate_added_deployments'),'Load the new Editor module first'
            points=added_deployments()
            report['added_deployments']=[{'actor':a.get_path_name(),'label':a.get_actor_label(),
                'transform':a.get_actor_transform().export_text()} for a in points]
            report['dry_run']=bake_result(unreal.GuLiResourceAuthoringLibrary.revalidate_added_deployments(points,False))
            assert report['dry_run']['success'],report['dry_run']
            assert resource_state()==report['resource_before'] and geometry_digest()==report['geometry_before']
            report['revalidation']=bake_result(unreal.GuLiResourceAuthoringLibrary.revalidate_added_deployments(points,True))
            assert report['revalidation']['success'],report['revalidation']
        report['after']=bake_result(unreal.GuLiResourceAuthoringLibrary.validate_current_bake())
        assert report['after']['success'],report['after']
        report['resource_after']=resource_state()
        report['geometry_after']=geometry_digest()
        assert report['geometry_before']==report['geometry_after'],'Resource geometry changed'
        report['source_actors_unchanged']=before_actors==source_actors()
        assert report['source_actors_unchanged'],'Source authoring actors changed during bake'
        report['manifests_after']=[]
        for population in (200,600):
            path=ROOT/f'Config/Hybrid/RuntimeManifest-{population}.json'
            exported=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population,str(path)))
            assert exported.get('success'),exported
            validated=json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(path)))
            assert validated.get('success'),validated
            data=json.loads(path.read_text(encoding='utf-8-sig'))
            assert data['map_package']==MAP and float(data['numbers'].get('acceptance/resource_supply',0))==0
            report['manifests_after'].append({'population':population,'path':str(path),'export':exported,'validation':validated,
                'config_hash':data['config_hash'],'map_hash':data['map_hash'],'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
        report['success']=True
    except Exception:
        report['error']=traceback.format_exc()
    (OUT/'incremental_repair.json').write_text(json.dumps(report,ensure_ascii=False,indent=2,default=str),encoding='utf8')
    return report


if __name__=='__main__':
    result=repair()
    unreal.MCPythonHelper.submit_result(json.dumps(result,ensure_ascii=False,default=str))
