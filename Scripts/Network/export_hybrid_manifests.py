"""Run through the source UE Python editor to export and read back both loads."""
from pathlib import Path
import hashlib
import json
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
OUTPUT=ROOT / "TestResults/HybridFullAcceptance/Implementation/manifest-readback-capability-contracts.json"
REPORT={"passed":False,"map":"/Game/Maps/LVL_CommanderMassPrototype","loads":[],
        "evidence_scope":"Editor export and file readback only; no gameplay validation"}
try:
    # Do not unload an already-open production world just to export its data.
    current_map = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name().split(".")[0]
    if current_map != REPORT["map"] and not unreal.EditorLoadingAndSavingUtils.load_map(REPORT["map"]):
        raise RuntimeError("Production map could not be loaded")
    for population in (200,600):
        file=ROOT / f"Config/Hybrid/RuntimeManifest-{population}.json"
        exported=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population,str(file)))
        if not exported.get("success"):
            raise RuntimeError(f"Manifest {population}: {exported}")
        validated=json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(file)))
        if not validated.get("success"):
            raise RuntimeError(f"Manifest readback {population}: {validated}")
        manifest=json.loads(file.read_text(encoding="utf-8-sig"))
        prefix="/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_UnitSkills.DT_GuLiStrikeCommander_UnitSkills/"
        speeds={row:float(manifest["numbers"][prefix+row+"/ProjectileSpeedCentimetersPerSecond"])
                for row in ("SoldierA_Strafe","WM01_Strafe","SweeperSummon_Strafe")}
        if (set(speeds.values())!={4800.0}
                or manifest["strings"].get("startup/resource_ledger")!="1"
                or manifest["strings"].get("startup/mining_reservations")!="1"
                or manifest["strings"].get("startup/mining_slots_adapter")!="2"
                or len([o for o in manifest["objects"] if o["definition"]=="unit/3"])!=16
                or len([o for o in manifest["objects"] if o["definition"]=="building/6"])!=2
                or manifest["strings"].get("startup/acceptance_flight_adapter")!="2"
                or int(float(manifest["numbers"].get("startup/engineer_count",0)))!=16
                or len([o for o in manifest["objects"] if o["definition"]=="unit/4"])!=16
                or float(manifest["numbers"].get("startup/ground_health",0))<=0
                or not {"admit/work/place/6","reserve/building/6","place/building/6","refund/building/6","complete/building/6"}.issubset({r['id'] for r in manifest['rules']})):
            raise RuntimeError(f"Manifest mismatch: schema={manifest['schema']}, speeds={speeds}")
        families={k.removeprefix("adapter/definition/"):v for k,v in manifest["strings"].items() if k.startswith("adapter/definition/")}
        if (families.get("building/6")!="ResourceFactory" or families.get("building/1")!="PlacedBuilding" or families.get("work/place/6")!="PlacementWork"):
            raise RuntimeError(f"Missing data-driven adapter contracts: {families}")
        # Version ownership stays in native registrations. This script records
        # their exported values and keeps only the acceptance scenario's facts.
        versions={k.removeprefix("adapter/family/").removesuffix("/state_version"):int(float(v))
                  for k,v in manifest["numbers"].items() if k.startswith("adapter/family/") and k.endswith("/state_version")}
        components={k.removeprefix("component/").removesuffix("/state_version"):int(float(v))
                    for k,v in manifest["numbers"].items() if k.startswith("component/") and k.endswith("/state_version")}
        if (not set(families.values()).issubset(versions) or not components
                or any(v<=0 for v in (*versions.values(),*components.values()))):
            raise RuntimeError("Missing owner-state family/component versions")
        capability_versions={k:int(float(v)) for k,v in manifest["numbers"].items() if k.startswith("equipment/") and k.endswith("/private_version")}
        configuration_hashes={k:v for k,v in manifest["strings"].items() if k.startswith("equipment/") and k.endswith("/configuration_hash")}
        if any(not value.isdecimal() or not 0<int(value)<2**64 for value in configuration_hashes.values()):
            raise RuntimeError("Invalid immutable capability checksum")
        cadence={k:int(float(manifest["numbers"].get(k,0))) for k in
                 ("simulation/logic_hz","simulation/mass_hz","simulation/wingman_hz","network/wingman/motion_hz")}
        if tuple(cadence.values())!=(60,10,30,10):
            raise RuntimeError(f"Unexpected simulation/publish cadence: {cadence}")
        if int(float(manifest["numbers"]["resource/profile/building/6/processor/kind"]))!=7 or int(float(manifest["numbers"]["resource/profile/unit/3/cargo/kind"]))!=6:
            raise RuntimeError("Portable resource capability profiles are missing")
        upgrade_chunks=int(float(manifest["numbers"].get("upgrade_catalog/ship/chunks",0)))
        if manifest["strings"].get("startup/ship_upgrade_catalog")!="ship" or upgrade_chunks<=0:
            raise RuntimeError("Portable Ship upgrade catalog is missing")
        upgrade_bytes=b"".join(bytes.fromhex(manifest["strings"][f"upgrade_catalog/ship/chunk/{i}"]) for i in range(upgrade_chunks))
        if int.from_bytes(upgrade_bytes[:4],"little")!=1 or int.from_bytes(upgrade_bytes[12:16],"little")<=0:
            raise RuntimeError("Portable upgrade catalog has no compatible schema or choices")
        REPORT["loads"].append({"population":population,"file":str(file),"schema":manifest["schema"],
            "protocol_version":manifest["protocol_version"],"handoff_version":manifest["handoff_version"],
            "resource_nodes":int(float(manifest["numbers"]["resource_map/node_count"])),
            "mining_reservations":manifest["strings"]["startup/mining_reservations"],
            "mining_slots_adapter":manifest["strings"]["startup/mining_slots_adapter"],
            "resource_initial_raw":sum(int(float(v)) for k,v in manifest["numbers"].items() if k.startswith("ore/") and k.endswith("/amount")),
            "config_hash":manifest["config_hash"],"map_hash":manifest["map_hash"],"speeds_cm_s":speeds,
            "file_sha256":hashlib.sha256(file.read_bytes()).hexdigest(),"objects":len(manifest["objects"]),
            "rules":len(manifest["rules"]),"coverage":manifest["strings"]["startup/coverage"],"adapter_families":families,
            "family_versions":versions,"component_versions":components,"capability_versions":capability_versions,
            "capability_configuration_hashes":configuration_hashes,"cadence":cadence,
            "object_components_contract":manifest["strings"]["adapter/actor_components"],
            "ship_upgrade_catalog":{"chunks":upgrade_chunks,"bytes":len(upgrade_bytes),"sha256":hashlib.sha256(upgrade_bytes).hexdigest(),
                "revision":int(float(manifest["numbers"]["upgrade_catalog/ship/revision"])),"nodes":int.from_bytes(upgrade_bytes[12:16],"little")},
            "engineer_count":int(float(manifest["numbers"]["startup/engineer_count"])),
            "ground_configured_health":float(manifest["numbers"]["startup/ground_health"]),
            "ground_object_health":[float(o["health"]) for o in manifest["objects"] if o["definition"]=="player/ground"]})
    REPORT["passed"]=True
except Exception as error:
    REPORT["error"]=str(error)
    raise
finally:
    OUTPUT.parent.mkdir(parents=True,exist_ok=True)
    OUTPUT.write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding="utf-8")
