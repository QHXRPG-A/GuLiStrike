"""Run through commander_editor_python.py --file; reads the connected editor only."""

import os
import json
import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
settings = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.LevelEditorPlaySettings'))
validation = unreal.GuLiNavigationBakeLibrary.validate_world_navigation(world)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
navigation = []
for actor in actors:
    if isinstance(actor, unreal.RecastNavMesh):
        navigation.append({
            'path': actor.get_path_name(),
            'runtime_generation': str(actor.get_editor_property('RuntimeGeneration')),
            'agent_radius': actor.get_editor_property('AgentRadius'),
            'agent_height': actor.get_editor_property('AgentHeight'),
            'tile_size': actor.get_editor_property('TileSizeUU'),
            'resolution': [{key: param.get_editor_property(key)
                            for key in ('CellSize', 'CellHeight', 'AgentMaxStepHeight')}
                           for param in actor.get_editor_property('NavMeshResolutionParams')],
            'async_gathering': actor.get_editor_property('bDoFullyAsyncNavDataGathering'),
            'force_rebuild_on_load': actor.get_editor_property('bForceRebuildOnLoad'),
        })

unreal.MCPythonHelper.submit_result(json.dumps({
    'success': True,
    'pid': os.getpid(),
    'world': world.get_path_name(),
    'clients': settings.get_editor_property('PlayNumberOfClients'),
    'one_process': settings.get_editor_property('RunUnderOneProcess'),
    'playing': unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),
    'validation': {
        'success': validation.success,
        'message': validation.message,
        'total_seconds': validation.total_seconds,
        'entries': [{
            'path': entry.object_path, 'kind': entry.kind, 'status': entry.status,
            'source_hash': entry.source_hash, 'check_seconds': entry.check_seconds,
            'message': entry.message,
        } for entry in validation.entries],
    },
    'ground': navigation,
}, ensure_ascii=False))
