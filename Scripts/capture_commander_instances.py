import json
import time

import unreal


editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_game_world()
result = {"captured_at": time.time(), "instances": []}
if world:
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
        if "GuLiCommanderPresentationActor" not in actor.get_class().get_path_name():
            continue
        for component in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
            if component.get_name() != "UnitInstances":
                continue
            for index in range(min(30, component.get_instance_count())):
                transform = component.get_instance_transform(index, True)
                result["instances"].append(list(transform.translation.to_tuple()))
with open("D:/UE5.7/test1/Progress/CommanderInstanceCapture.json", "w", encoding="utf-8") as stream:
    json.dump(result, stream, indent=2)
