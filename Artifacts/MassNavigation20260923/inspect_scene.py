import json
from pathlib import Path
import unreal
out=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))/'Artifacts/MassNavigation20260923'
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world=editor.get_editor_world()
api=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors=api.get_all_level_actors()
def rec(a):
    return dict(name=a.get_name(),label=a.get_actor_label(),class_path=a.get_class().get_path_name(),location=list(a.get_actor_location().to_tuple()))
definition=unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap')
economy=unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy')
anchors=definition.get_editor_property('spawn_anchors')
data={'map':world.get_path_name(),'game_world':str(editor.get_game_world()),
      'game_mode':str(world.get_world_settings().get_editor_property('default_game_mode')),
      'actors':[rec(a) for a in actors if a.get_class().get_name() in ['Note','StaticMeshActor','PlayerStart','RecastNavMesh','NavMeshBoundsVolume','GuLiCommanderDeploymentPoint'] or 'R1C5' in a.get_actor_label()],
      'red_assembly':list(anchors.get_editor_property('red_assembly').to_tuple()),
      'red_factory':list(anchors.get_editor_property('red_factory').to_tuple()),
      'layout_hash':definition.get_editor_property('layout_hash'),
      'cluster_count':len(definition.get_editor_property('clusters')),
      'miners_per_team':economy.get_editor_property('initial_mining_vehicles_per_team'),
      'builders_per_team':economy.get_editor_property('initial_construction_vehicles_per_team'),
      'trace_doc':unreal.SystemLibrary.line_trace_single.__doc__,
      'nearest_red_cluster':None}
clusters=definition.get_editor_property('clusters')
closest=sorted(clusters,key=lambda c:(c.get_editor_property('center')-anchors.get_editor_property('red_assembly')).length())[:3]
data['nearest_red_clusters']=[dict(center=list(c.get_editor_property('center').to_tuple()),data=str(c)) for c in closest]
(out/'scene-before.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'map':data['map'],'game_world':data['game_world'],'actors':len(actors),'saved_report':str(out/'scene-before.json')}))
