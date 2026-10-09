"""Capture user-authorized PIE cameras; shot showui preserves the actual Slate layer."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008/Images')
OUT.mkdir(parents=True, exist_ok=True)


def run():
    out = []
    for world in unreal.ObjectIterator(unreal.World):
        if 'UEDPIE' not in world.get_path_name():
            continue
        subs = [s for s in unreal.ObjectIterator(unreal.GuLiLocalTeamColorSubsystem) if s.get_world() == world]
        if not subs:
            continue
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        team = 'red' if pc.player_state.get_team() == unreal.GuLiTeam.RED else 'blue'
        cameras = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.SceneCapture2D) if a.actor_has_tag('GuLi.ModelRuntimeCamera')]
        camera = cameras[0] if cameras else unreal.GuLiTeleportQALibrary.create_capture(world)
        camera.tags = ['GuLi.ModelRuntimeCamera']
        center = unreal.Vector(-11000, 65000, 1100)
        position = center + unreal.Vector(-1000, -7500, 6500)
        camera.set_actor_location_and_rotation(position, unreal.MathLibrary.find_look_at_rotation(position, center), False, True)
        capture = camera.capture_component2d
        capture.capture_every_frame = False
        capture.capture_on_movement = False
        capture.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
        capture.fov_angle = 55
        target = unreal.RenderingLibrary.create_render_target2d(world, 1400, 1000, unreal.TextureRenderTargetFormat.RTF_RGBA8, unreal.LinearColor(0, 0, 0, 1), False, False)
        capture.texture_target = target
        capture.capture_scene()
        filename = 'client_' + team + '_models.png'
        unreal.RenderingLibrary.export_render_target(world, target, str(OUT), filename)
        pc.set_view_target_with_blend(camera, 0)
        out.append({'world': world.get_path_name(), 'team': team, 'model_capture': str(OUT / filename), 'camera': camera.get_path_name()})
    return {'success': len(out) == 2, 'cameras': out, 'viewport_shot_next_tick': True}


unreal.MCPythonHelper.submit_result(json.dumps(run()))
