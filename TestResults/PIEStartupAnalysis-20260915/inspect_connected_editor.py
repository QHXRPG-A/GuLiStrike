"""Read-only editor snapshot through the existing Scripts/ bridge.

The response includes the real editor PID; never assume the bridge reaches the
foreground editor when multiple processes share the configured port.
"""

import json
import sys
from pathlib import Path


OUT = Path(__file__).resolve().parent
sys.path.insert(0, str(OUT.parents[1] / "Scripts"))
from commander_editor_python import call_editor


CODE = """
import os, unreal, json
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
es = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = es.get_editor_world()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
rows = []
for actor in actors:
    if actor.get_class().get_name() not in ['NavMeshBoundsVolume', 'RecastNavMesh']:
        continue
    center, extent = actor.get_actor_bounds(False)
    row = {'name': actor.get_name(), 'class': actor.get_class().get_name(),
           'center_cm': [center.x, center.y, center.z],
           'extent_cm': [extent.x, extent.y, extent.z], 'properties': {}}
    for key in ['runtime_generation', 'tile_size_uu', 'agent_radius', 'agent_height',
                'max_simultaneous_tile_generation_jobs_count', 'force_rebuild_on_load',
                'do_fully_async_nav_data_gathering', 'can_be_main_nav_data']:
        try:
            row['properties'][key] = str(actor.get_editor_property(key))
        except Exception:
            pass
    if actor.get_class().get_name() == 'RecastNavMesh':
        row['resolution_params_low_default_high'] = [
            {key: param.get_editor_property(key)
             for key in ['cell_size', 'cell_height', 'agent_max_step_height']}
            for param in actor.get_editor_property('nav_mesh_resolution_params')]
    rows.append(row)
play = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.LevelEditorPlaySettings'))
play_settings = {key: str(play.get_editor_property(key))
                 for key in ['PlayNumberOfClients', 'RunUnderOneProcess', 'AutoRecompileBlueprints']}
performance = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
unreal.MCPythonHelper.submit_result(json.dumps({
    'success': True, 'pid': os.getpid(), 'in_pie': les.is_in_play_in_editor(),
    'world': world.get_path_name(), 'actor_count': len(actors),
    'nav': rows, 'pie_settings': play_settings,
    'throttle_cpu_when_not_foreground': str(performance.get_editor_property('bThrottleCPUWhenNotForeground')),
}))
"""


if __name__ == "__main__":
    response = call_editor(CODE, timeout=15)
    (OUT / "connected-editor.json").write_text(json.dumps(response, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(response, ensure_ascii=True, indent=2))
    if not response.get("success") or not response.get("result", {}).get("success"):
        raise SystemExit(1)
