"""Source-editor worker for the four explicitly approved gameplay maps; no SaveAll."""
import json,os,time,traceback
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'TestResults/Scale020'
helpers={'__name__':'scale020_reflection'}
exec((ROOT/'Scripts/Scale020/capture_editor_baseline.py').read_text(encoding='utf-8').split('def main():')[0],helpers)
value=helpers['value']
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
report={'pid':os.getpid(),'started':time.time(),'maps':[],'success':False}
maps=['/Game/Maps/LVL_ShipTest','/Game/Maps/LVL_ShipWingmanAirCombatPrototype',
      '/Game/Maps/LVL_Main','/Game/Maps/LVL_CommanderMassPrototype']
requested=os.environ.get('GULI_SCALE020_NAV_MAP','')
if requested:
    assert requested in maps,requested
    maps=[requested]
report_name=os.environ.get('GULI_SCALE020_NAV_REPORT','navigation-worker.json')
assert Path(report_name).name==report_name
validate_only=os.environ.get('GULI_SCALE020_NAV_READ_ONLY','0')=='1'
def write(): (OUT/report_name).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
try:
    assert not editor.get_game_world()
    for path in maps:
        assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Unexpected unsaved map'
        report['current']=path;write()
        assert levels.load_level(path),path
        world=editor.get_editor_world()
        assert world.get_path_name().split('.')[0]==path
        row={'map':path,'started':time.time()}
        report['maps'].append(row);write()
        if validate_only:
            row['validation']=value(unreal.GuLiNavigationBakeLibrary.validate_world_navigation(world));write()
            assert row['validation']['success'],row['validation']
            continue
        row['ground_parameters_before']=[]
        for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
            if a.get_class().get_name()!='RecastNavMesh': continue
            data={'path':a.get_path_name()}
            for key in ('tile_size_uu','cell_size','cell_height','agent_radius','agent_height',
                        'agent_max_step_height','nav_mesh_resolution_params','tile_pool_size'):
                try: data[key]=value(a.get_editor_property(key))
                except Exception as exc: data[key]={'unavailable':str(exc)}
            row['ground_parameters_before'].append(data)
        write()
        row['configuration']=value(unreal.GuLiNavigationBakeLibrary.migrate_object_scale020(world));write()
        assert row['configuration']['success'],row['configuration']
        result=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(world,True)
        row['prepare']=value(result);row['finished']=time.time();write()
        assert result.success,result.message
        # Only this loaded, explicitly migrated map. Never save unrelated packages.
        dirty=unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
        assert all(p.get_name()==path for p in dirty),[p.get_name() for p in dirty]
        if dirty: assert levels.save_current_level()
        row['validation']=value(unreal.GuLiNavigationBakeLibrary.validate_world_navigation(world));write()
        assert row['validation']['success'],row['validation']
    report['success']=True
except Exception: report['error']=traceback.format_exc()
finally:
    report['finished']=time.time();write()
    unreal.SystemLibrary.quit_editor()
