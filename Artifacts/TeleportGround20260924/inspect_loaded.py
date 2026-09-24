import json
import os
import traceback
from pathlib import Path
import unreal

try:
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    building = unreal.get_default_object(unreal.GuLiPlacedBuilding)
    collision = building.get_editor_property('collision_root')
    modifier = building.get_editor_property('navigation_modifier')
    rows = []
    for a in api.get_all_level_actors():
        if 'TeleportGroundReview20260924' in [str(t) for t in a.tags]:
            rows.append({'label': a.get_actor_label(), 'class': a.get_class().get_name(),
                         'location': a.get_actor_location().to_tuple(),
                         'editor_only': a.get_editor_property('is_editor_only_actor')})
    report = {
        'editor_pid': os.getpid(),
        'map': editor.get_editor_world().get_path_name(),
        'is_pie': level.is_in_play_in_editor(),
        'building_defaults': {
            'collision_profile': str(collision.get_collision_profile_name()),
            'step_up': str(collision.get_editor_property('can_character_step_up_on')),
            'slope': str(collision.get_walkable_slope_override()),
            'dynamic_obstacle': collision.get_editor_property('dynamic_obstacle'),
            'obstacle_area': collision.get_editor_property('area_class_override').get_name(),
            'modifier_area': modifier.get_editor_property('area_class').get_name(),
        },
        'saved_scene_markers': rows,
        'boundary': 'Loaded class defaults and saved editor entities only; no PIE or runtime landing test.'
    }
    Path('D:/UE5.7/test1/Artifacts/TeleportGround20260924/loaded-config.json').write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(report))
except Exception:
    unreal.MCPythonHelper.submit_result(json.dumps({'error': traceback.format_exc()}))
