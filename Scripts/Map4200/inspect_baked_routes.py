"""Inspect static source NavData connectivity for the newly authored map, without PIE."""
import json
from pathlib import Path
import unreal


def main():
    out=Path(unreal.Paths.project_dir()).resolve()/'Artifacts/Map4200/20260922'
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    definition=unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap')
    assert len(definition.territories)==49
    navs=[a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(a,unreal.RecastNavMesh)]
    assert len(navs)==2
    points={(t.board_row,t.board_column):t for t in definition.territories}
    failures=[]
    records=[]
    for nav in navs:
        # Territory.Center is the canonical XY board coordinate (Z=0), not an
        # actor's ground height. Use the resource baker's explicit query extent.
        # Default's unchanged 250 cm Z extent cannot reach the crater floor.
        def project(point):
            return unreal.NavigationSystemV1.project_point_to_navigation(
                world,point,nav,None,unreal.Vector(2500,2500,5000))
        def route(start,end):
            projected_start,projected_end=project(start),project(end)
            if projected_start is None or projected_end is None:
                return None
            return unreal.NavigationSystemV1.find_path_to_location_synchronously(
                world,projected_start,projected_end,nav)
        for (r,c),start in points.items():
            for neighbor in ((r+1,c),(r,c+1)):
                if neighbor not in points: continue
                end=points[neighbor]
                path=route(start.center,end.center)
                complete=bool(path and path.is_valid() and not path.is_partial())
                record={'agent':nav.get_name(),'from':str(start.territory_id),'to':str(end.territory_id),
                        'complete':complete,'length_cm':path.get_path_length() if complete else None}
                records.append(record)
                if not complete: failures.append(record)
        for cluster in definition.clusters:
            start=definition.territories[cluster.territory_index].center
            path=route(start,cluster.center)
            complete=bool(path and path.is_valid() and not path.is_partial())
            record={'agent':nav.get_name(),'territory_to_cluster':cluster.get_editor_property('cluster_id'),'complete':complete}
            records.append(record)
            if not complete: failures.append(record)
    result={'success':not failures,'query_count':len(records),'failures':failures,'static_source_routes':records,
            'projection_extent_cm':[2500,2500,5000],'agent_settings_changed':False,'pie_run':False}
    (out/'baked-route-inspection.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    return {'success':result['success'],'query_count':len(records),'failure_count':len(failures),'first_failures':failures[:8]}


unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))
