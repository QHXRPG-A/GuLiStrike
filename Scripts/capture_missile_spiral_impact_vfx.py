# Capture deterministic start/peak/dissipation frames without saving or changing
# the current level. Run through Scripts/ue_exec.py in the live editor.

import json
import os
import traceback
import unreal


SYSTEM_PATH = "/Game/GuLiStrike/FX/MissileSpiralImpact/NS_MissileSpiralImpact"
OUTPUT_DIR = "D:/UE5.7/test1/Saved/VibeUE/Screenshots"
SAMPLES = (
    ("start_025s", 0.25),
    ("peak_090s", 0.90),
    ("late_peak_120s", 1.20),
    ("dissipation_160s", 1.60),
)

report = {"system": SYSTEM_PATH, "samples": [], "transient_actors_destroyed": False}
niagara_actor = None
capture_actor = None

try:
    world = unreal.EditorLevelLibrary.get_editor_world()
    asset_subsystem = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    system = asset_subsystem.load_asset(SYSTEM_PATH)
    if not world or not system:
        raise RuntimeError("Editor world or Niagara System is unavailable")

    niagara_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.NiagaraActor,
        unreal.Vector(0.0, 0.0, 0.0),
        unreal.Rotator(0.0, 0.0, 0.0),
        transient=True,
    )
    capture_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
        unreal.SceneCapture2D,
        unreal.Vector(1450.0, -1450.0, 760.0),
        unreal.Rotator(0.0, 0.0, 0.0),
        transient=True,
    )
    if not niagara_actor or not capture_actor:
        raise RuntimeError("Could not spawn transient capture actors")

    component = niagara_actor.niagara_component
    component.set_asset(system, reset_existing_override_parameters=True)
    component.set_auto_activate(False)
    component.set_can_render_while_seeking(True)
    component.set_force_solo(True)
    component.set_bounds_scale(20.0)
    component.set_age_update_mode(unreal.NiagaraAgeUpdateMode.TICK_DELTA_TIME)
    component.activate(True)

    capture = capture_actor.capture_component2d
    target = unreal.Vector(0.0, 0.0, 330.0)
    camera_location = unreal.Vector(1450.0, -1450.0, 760.0)
    camera_rotation = unreal.MathLibrary.find_look_at_rotation(camera_location, target)
    capture_actor.set_actor_location_and_rotation(
        camera_location, camera_rotation, sweep=False, teleport=True
    )
    capture.set_editor_property("capture_every_frame", False)
    capture.set_editor_property("capture_on_movement", False)
    capture.set_editor_property("fov_angle", 34.0)
    capture.set_editor_property(
        "capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
    )
    capture.set_editor_property(
        "primitive_render_mode",
        unreal.SceneCapturePrimitiveRenderMode.PRM_RENDER_SCENE_PRIMITIVES,
    )

    render_target = unreal.RenderingLibrary.create_render_target2d(
        world,
        1024,
        1024,
        unreal.TextureRenderTargetFormat.RTF_RGBA8,
        unreal.LinearColor(0.008, 0.008, 0.012, 1.0),
        False,
        False,
    )
    capture.set_editor_property("texture_target", render_target)
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    for label, age in SAMPLES:
        component.reinitialize_system()
        component.activate(True)
        component.advance_simulation_by_time(age, 1.0 / 60.0)
        capture.capture_scene()
        file_name = f"missile_spiral_{label}.png"
        unreal.RenderingLibrary.export_render_target(
            world, render_target, OUTPUT_DIR, file_name
        )
        output_path = os.path.join(OUTPUT_DIR, file_name).replace("\\", "/")
        report["samples"].append(
            {
                "label": label,
                "age_seconds": age,
                "path": output_path,
                "exists": os.path.isfile(output_path),
                "size_bytes": os.path.getsize(output_path)
                if os.path.isfile(output_path)
                else 0,
            }
        )
finally:
    if capture_actor:
        unreal.EditorLevelLibrary.destroy_actor(capture_actor)
    if niagara_actor:
        unreal.EditorLevelLibrary.destroy_actor(niagara_actor)
    report["transient_actors_destroyed"] = True

print(json.dumps(report, ensure_ascii=False, sort_keys=True))
