"""Read the production assembly catalog through UE; never read binary assets."""
from pathlib import Path
import json
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
destination = root / "TestResults/HybridFullAcceptance/Implementation/ship-capability-catalog-build36.json"
report = {"passed": False, "groups": []}
try:
    ship_class = unreal.load_class(None, "/Game/GuLiStrike/Ship/BP_CombatAvatarFly01.BP_CombatAvatarFly01_C")
    ship = unreal.get_default_object(ship_class)
    assembly = ship.get_ship_assembly()
    catalog = assembly.get_editor_property("catalog")
    report["catalog"] = catalog.get_path_name()
    report["revision"] = catalog.get_editor_property("revision")
    report["validation"] = [str(v) for v in unreal.GuLiSkillAuthoringLibrary.validate_ship_catalog(catalog, ship_class)]
    for group in catalog.get_editor_property("groups"):
        capabilities = []
        for capability in group.get_editor_property("capabilities"):
            component_class = capability.get_editor_property("component_class")
            configuration = capability.get_editor_property("configuration")
            capabilities.append({
                "id": str(capability.get_editor_property("capability_id")),
                "class": component_class.get_path_name() if component_class else None,
                "configuration": configuration.get_path_name() if configuration else None,
            })
        report["groups"].append({
            "id": str(group.get_editor_property("configuration_id")),
            "executable": group.get_editor_property("executable"),
            "notes": str(group.get_editor_property("authoring_notes")),
            "capabilities": capabilities,
        })
    report["passed"] = not report["validation"]
finally:
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False))
