"""Capture Commander cull QA evidence at the camera's current spring-arm length."""

import json
import math
import os
import time

import unreal


PROGRESS_DIR = "D:/UE5.7/test1/Progress"
UNIT_CULL_CM = 100000.0

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_game_world()
result = {
    "captured_at": time.time(),
    "world": world.get_path_name() if world else None,
    "errors": [],
}

if world:
    camera_pawn = None
    unit_component = None
    ring_component = None
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        class_name = actor.get_class().get_name()
        if class_name == "GuLiCommanderCameraPawn":
            camera_pawn = actor
        elif class_name == "GuLiCommanderPresentationActor":
            for component in actor.get_components_by_class(
                unreal.InstancedStaticMeshComponent
            ):
                if component.get_name() == "UnitInstances":
                    unit_component = component
                elif component.get_name() == "RingInstances":
                    ring_component = component

    if camera_pawn and unit_component and ring_component:
        spring_arm = camera_pawn.get_commander_spring_arm()
        camera = camera_pawn.get_commander_camera()
        arm_length = float(spring_arm.get_editor_property("target_arm_length"))
        camera_location = camera.get_world_location()
        distances = []
        for index in range(unit_component.get_instance_count()):
            transform_result = unit_component.get_instance_transform(index, True)
            transform = transform_result
            if isinstance(transform_result, tuple):
                transform = next(
                    (
                        item
                        for item in transform_result
                        if isinstance(item, unreal.Transform)
                    ),
                    None,
                )
            if transform is None:
                continue
            delta = transform.translation - camera_location
            distances.append(math.sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z))

        arm_label = int(round(arm_length))
        screenshot = f"{PROGRESS_DIR}/CommanderCullQA-{arm_label}cm.png"
        report = f"{PROGRESS_DIR}/CommanderCullQA-{arm_label}cm.json"
        result.update(
            {
                "camera_pawn": camera_pawn.get_path_name(),
                "pivot_location": list(camera_pawn.get_actor_location().to_tuple()),
                "camera_location": list(camera_location.to_tuple()),
                "arm_length_cm": arm_length,
                "unit_instances": unit_component.get_instance_count(),
                "ring_instances": ring_component.get_instance_count(),
                "unit_start_end_cull_cm": list(unit_component.get_cull_distances()),
                "ring_start_end_cull_cm": list(ring_component.get_cull_distances()),
                "distance_samples": len(distances),
                "minimum_view_to_unit_cm": min(distances) if distances else None,
                "maximum_view_to_unit_cm": max(distances) if distances else None,
                "units_inside_cull_distance": sum(
                    distance <= UNIT_CULL_CM for distance in distances
                ),
                "units_outside_cull_distance": sum(
                    distance > UNIT_CULL_CM for distance in distances
                ),
                "screenshot": screenshot,
            }
        )
        unreal.SystemLibrary.execute_console_command(
            world,
            f'HighResShot 1920x1080 filename="{screenshot}"',
        )
        result["screenshot_requested"] = True
        with open(report, "w", encoding="utf-8") as stream:
            json.dump(result, stream, ensure_ascii=False, indent=2)
        print(report)
    else:
        result["errors"].append("CameraPawn or Unit/Ring ISM was not found")
else:
    result["errors"].append("PIE World is not ready")

if result["errors"]:
    fallback = os.path.join(PROGRESS_DIR, "CommanderCullQA-Error.json")
    with open(fallback, "w", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2)
    print(fallback)
