import json

import unreal


OUTPUT_PATH = "D:/UE5.7/test1/Progress/CommanderUnitMaterialQACamera.json"

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_game_world()
result = {
    "world": world.get_path_name() if world else None,
    "positioned": False,
    "sample_count": 0,
    "candidate_actors": [],
    "sample_errors": [],
}

if world:
    presentation = None
    camera_pawn = None
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        class_name = actor.get_class().get_name()
        if "Commander" in class_name or "Camera" in class_name:
            result["candidate_actors"].append(
                {
                    "label": actor.get_actor_label(),
                    "class": class_name,
                }
            )
        if class_name == "GuLiCommanderPresentationActor":
            presentation = actor
        elif class_name == "GuLiCommanderCameraPawn":
            camera_pawn = actor

    unit_instances = None
    if presentation:
        for component in presentation.get_components_by_class(
            unreal.InstancedStaticMeshComponent
        ):
            if component.get_name() == "UnitInstances":
                unit_instances = component
                break

    sample_locations = []
    if unit_instances:
        sample_total = min(25, unit_instances.get_instance_count())
        for instance_index in range(sample_total):
            try:
                transform_result = unit_instances.get_instance_transform(
                    instance_index,
                    world_space=True,
                )
                transform = transform_result
                if isinstance(transform_result, tuple):
                    transform = next(
                        (
                            value
                            for value in transform_result
                            if isinstance(value, unreal.Transform)
                        ),
                        None,
                    )
                if transform is None:
                    raise RuntimeError("No Transform in result: " + str(transform_result))
                sample_locations.append(transform.translation)
            except Exception as error:
                if len(result["sample_errors"]) < 3:
                    result["sample_errors"].append(str(error))

    if camera_pawn and sample_locations:
        count = float(len(sample_locations))
        center = unreal.Vector(
            sum(location.x for location in sample_locations) / count,
            sum(location.y for location in sample_locations) / count,
            sum(location.z for location in sample_locations) / count + 150.0,
        )
        camera_pawn.set_actor_tick_enabled(False)
        camera_pawn.set_actor_location(center, False, False)
        for component in camera_pawn.get_components_by_class(
            unreal.SpringArmComponent
        ):
            component.set_editor_property("do_collision_test", False)
            component.set_editor_property("target_arm_length", 8000.0)
        unreal.GameplayStatics.set_game_paused(world, True)
        result["camera"] = camera_pawn.get_path_name()
        result["focus"] = {"x": center.x, "y": center.y, "z": center.z}
        result["sample_count"] = len(sample_locations)
        result["positioned"] = True

with open(OUTPUT_PATH, "w", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
