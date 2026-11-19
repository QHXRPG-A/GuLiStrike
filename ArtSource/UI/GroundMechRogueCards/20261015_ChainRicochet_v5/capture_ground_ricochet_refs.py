"""Capture current Ground/Pioneer meshes through isolated scene capture components."""
import json
import traceback
from pathlib import Path

import unreal

ROOT = Path("D:/UE5.7/test1")
OUT = ROOT / "ArtSource/UI/GroundMechRogueCards/20261015_ChainRicochet_v3/References"
OUT.mkdir(parents=True, exist_ok=True)
report = {"success": False, "scope": "Actual project model art references. Transient actors; no map/asset saves, no gameplay.", "captures": []}
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_editor_world()
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
created = []
base = unreal.Vector(200000, 200000, 50000)


def spawn(cls, label, offset=(0, 0, 0)):
    actor = actors.spawn_actor_from_class(cls, base + unreal.Vector(*offset), transient=True)
    actor.set_actor_label("GroundRicochetRef_" + label)
    created.append(actor)
    return actor


def bounds(parts):
    boxes = []
    for component in parts:
        origin, extent, _ = unreal.SystemLibrary.get_component_bounds(component)
        boxes.append((origin - extent, origin + extent))
    low = unreal.Vector(*(min(v[0].to_tuple()[i] for v in boxes) for i in range(3)))
    high = unreal.Vector(*(max(v[1].to_tuple()[i] for v in boxes) for i in range(3)))
    return (low + high) * .5, (high - low) * .5


def linear_hex(value):
    values = [int(value[i:i + 2], 16) / 255 for i in (1, 3, 5)]
    return [v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4 for v in values]


try:
    report["world"] = world.get_path_name()
    bp_path = "/Game/GuLiStrike/GroundMech/BP_GroundMech_Light"
    ground = spawn(unreal.EditorAssetLibrary.load_blueprint_class(bp_path), "Ground", (0, 0, 400))
    ground.get_editor_property("upper_body_pivot").set_world_rotation(unreal.Rotator(yaw=-90), False, False)
    ground_parts = [ground.get_editor_property(name) for name in ("mesh", "armor", "shoulder", "machinegun")]
    report["ground"] = {"blueprint": bp_path, "model_ids": [4001, 4002, 4003, 4004], "parts": []}
    for component in ground_parts:
        mesh = component.get_skinned_asset() if isinstance(component, unreal.SkeletalMeshComponent) else component.static_mesh
        assert mesh and "/GroundMech/Style_v9/" in mesh.get_path_name()
        report["ground"]["parts"].append({"component": component.get_name(), "mesh": mesh.get_path_name(),
                                         "materials": [m.get_path_name() if m else None for m in component.get_materials()]})

    models = json.loads((ROOT / "Data/Json/DT_GuLiStrikeModels_Models.json").read_text(encoding="utf-8-sig"))
    pioneer_row = next(row for row in models if row["Id"] == 1001)
    pioneer = spawn(unreal.StaticMeshActor, "Pioneer", (4000, 0, 0))
    pioneer_component = pioneer.static_mesh_component
    pioneer_component.set_static_mesh(unreal.load_asset(pioneer_row["ResourcePath"]))
    pioneer_component.set_editor_property("evaluate_world_position_offset", False)
    pioneer_component.set_forced_lod_model(1)
    parameters = json.loads((ROOT / "Data/Json/DT_GuLiStrikeModels_MaterialParameters.json").read_text(encoding="utf-8-sig"))
    team_bindings = {row["ParameterKey"]: row for row in parameters if row["ModelId"] == 1001 and row["Driver"] == "CPD"}
    for key, color in [("TeamPrimary", pioneer_row["EnemyPrimaryHex"]), ("TeamSecondary", pioneer_row["EnemySecondaryHex"])]:
        pioneer_component.set_custom_primitive_data_vector4(team_bindings[key]["CustomDataIndex"], unreal.Vector4(*linear_hex(color), 1))
    for key, value in [("TeamEnabled", 1.), ("TeamLightStrength", .35)]:
        pioneer_component.set_custom_primitive_data_float(team_bindings[key]["CustomDataIndex"], value)
    report["pioneer"] = {"model_id": 1001, "mesh": pioneer_component.static_mesh.get_path_name(), "display_name": pioneer_row["DisplayName"],
                          "enemy_primary": pioneer_row["EnemyPrimaryHex"], "enemy_secondary": pioneer_row["EnemySecondaryHex"],
                          "materials": [m.get_path_name() if m else None for m in pioneer_component.get_materials()]}

    for index, (offset, intensity) in enumerate([((1300, 1300, 1700), 160000.), ((-900, 600, 1200), 90000.), ((4600, 900, 1100), 90000.)]):
        light = spawn(unreal.PointLight, "LocalLight" + str(index), offset)
        comp = light.get_component_by_class(unreal.PointLightComponent)
        comp.set_mobility(unreal.ComponentMobility.MOVABLE)
        comp.set_attenuation_radius(4500.)
        comp.set_intensity(intensity)

    camera = spawn(unreal.SceneCapture2D, "Capture")
    capture = camera.capture_component2d
    capture.capture_every_frame = False
    capture.capture_on_movement = False
    capture.always_persist_rendering_state = True
    capture.capture_source = unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
    capture.primitive_render_mode = unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST
    pp = capture.get_editor_property("post_process_settings")
    for key, value in dict(auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL, auto_exposure_bias=.5,
                           auto_exposure_apply_physical_camera_exposure=False, motion_blur_amount=0., bloom_intensity=0.).items():
        pp.set_editor_property("override_" + key, True)
        pp.set_editor_property(key, value)
    capture.set_editor_property("post_process_settings", pp)
    capture.post_process_blend_weight = 1.
    shots = [("GroundMech_Project_Rear3Q.png", ground_parts, (-1., 1., .3), 832, 1248, 1.4),
             ("GroundMech_Project_Weapon.png", ground_parts[1:], (1., 1., .2), 768, 768, 1.35),
             ("Pioneer_Project_Enemy.png", [pioneer_component], (1., 1., .35), 768, 1024, 1.1)]
    for filename, components, direction, width, height, distance_scale in shots:
        center, extent = bounds(components)
        span = max(extent.to_tuple()) * 2
        capture.clear_show_only_components()
        for component in components:
            capture.show_only_component(component)
        target = unreal.RenderingLibrary.create_render_target2d(world, width, height, unreal.TextureRenderTargetFormat.RTF_RGBA8,
                                                               unreal.LinearColor(.08, .10, .12, 1), False, False)
        target.target_gamma = 2.2
        capture.texture_target = target
        capture.fov_angle = 40.
        position = center + unreal.Vector(*direction) * span * distance_scale
        camera.set_actor_location_and_rotation(position, unreal.MathLibrary.find_look_at_rotation(position, center), False, True)
        capture.set_editor_property("camera_cut_this_frame", True)
        for _ in range(5):
            capture.capture_scene()
        unreal.RenderingLibrary.export_render_target(world, target, str(OUT), filename)
        assert (OUT / filename).is_file(), filename
        report["captures"].append({"file": str(OUT / filename), "size": [width, height], "center": center.to_tuple(), "extent": extent.to_tuple()})
    report["success"] = True
except Exception:
    report["error"] = traceback.format_exc()
finally:
    for actor in reversed(created):
        actors.destroy_actor(actor)
    report["temporary_actors_removed"] = True
    report["viewport_and_selection_unchanged"] = True
    (OUT / "project_models_capture.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

unreal.MCPythonHelper.submit_result(json.dumps({"success": report["success"], "captures": len(report["captures"]),
                                              "error": report.get("error"), "report": str(OUT / "project_models_capture.json")}, ensure_ascii=False))
