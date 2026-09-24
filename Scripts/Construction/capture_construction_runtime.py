"""Capture the current PIE construction sample without retaining Python world references."""
import gc
import json
from pathlib import Path
import unreal


def run():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    assert world
    label = globals().get('CONSTRUCTION_SHOT', 'preview')
    center = unreal.Vector(4000, 224000, -6418)
    camera = unreal.GuLiTeleportQALibrary.create_capture(world)
    try:
        capture = camera.capture_component2d
        capture.set_editor_property('capture_every_frame', False)
        capture.set_editor_property('capture_on_movement', False)
        capture.set_editor_property('capture_source', unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        capture.set_editor_property('fov_angle', 55)
        target = unreal.RenderingLibrary.create_render_target2d(world, 1400, 1000,
            unreal.TextureRenderTargetFormat.RTF_RGBA8, unreal.LinearColor(0, 0, 0, 1), False, False)
        capture.set_editor_property('texture_target', target)
        position = center + unreal.Vector(2100, -2200, 2100)
        camera.set_actor_location_and_rotation(position,
            unreal.MathLibrary.find_look_at_rotation(position, center + unreal.Vector(0, 0, 180)), False, True)
        capture.capture_scene()
        directory = 'D:/UE5.7/test1/outputs/construction'
        Path(directory).mkdir(parents=True, exist_ok=True)
        unreal.RenderingLibrary.export_render_target(world, target, directory, label + '.png')
        return {'image': directory + '/' + label + '.png'}
    finally:
        camera.destroy_actor()


def main():
    try:
        result = run()
    except Exception as error:
        result = {'error': str(error)}
    globals().pop('CONSTRUCTION_SHOT', None)
    unreal.MCPythonHelper.submit_result(json.dumps(result))


main()
gc.collect()
