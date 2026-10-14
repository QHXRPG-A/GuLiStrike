"""Read the production map's FlightNav entities and bake contract; no asset edits."""
from pathlib import Path
import json
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
report={"map":"/Game/Maps/LVL_CommanderMassPrototype","volumes":[],"passed":False}
try:
    if not unreal.EditorLoadingAndSavingUtils.load_map(report["map"]):
        raise RuntimeError("Production map did not load")
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
        if not isinstance(actor,unreal.GuLiFlightNavigationVolume):
            continue
        record={"path":actor.get_path_name(),"enabled":actor.get_editor_property("navigation_enabled"),
                "position":str(actor.get_actor_location()),"bounds":str(actor.get_actor_bounds(False))}
        data=actor.get_editor_property("navigation_data")
        record["data"]=data.get_path_name() if data else None
        if data:
            metadata=data.get_editor_property("metadata")
            for name in ("bounds","source_hash","baked_agent_radius"):
                try:
                    record[name]=str(metadata.get_editor_property(name))
                except Exception as error:
                    record[name]="unavailable: "+str(error)
        report["volumes"].append(record)
    validation=unreal.GuLiNavigationBakeLibrary.validate_world_navigation(world)
    report["validation"]={"success":validation.success,"message":validation.message,
        "entries":[{"path":entry.object_path,"kind":entry.kind,"status":entry.status,"message":entry.message}
                   for entry in validation.entries]}
    report["passed"]=True
except Exception as error:
    report["error"]=str(error)
finally:
    (root/"TestResults/HybridFullAcceptance/Implementation/flight-navigation-map-readback48.json").write_text(
        json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
    unreal.SystemLibrary.quit_editor()
