"""Create isolated data-only variants and export their frozen runtime profiles.

Run in the idle source editor after the unified native build. Production tables
are read through UE and never modified; generated assets belong to this work item.
"""
from pathlib import Path
import copy
import hashlib
import json
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
FOLDER = "/Game/Development/HybridAcceptance"
WORK = "WORK-20261011-002/D27"
overrides, assets, sources = [], {}, {}


def settings(name):
    return unreal.get_default_object(unreal.load_class(None, "/Script/GuLiStrike." + name))


def override(cls, prop, value):
    overrides.append({"class": "/Script/GuLiStrike."+cls, "property": prop, "value": value})


def table(cls, prop):
    value = settings(cls).get_editor_property(prop)
    source = value if isinstance(value, unreal.DataTable) else unreal.load_asset(str(value))
    if not source:
        raise RuntimeError("Missing production data " + prop)
    raw = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(source)
    sources[source.get_path_name()] = hashlib.sha256(raw.encode()).hexdigest()
    return source, json.loads(raw)


def duplicate(source, name):
    path = FOLDER + "/" + name
    result = unreal.load_asset(path)
    if result:
        if unreal.EditorAssetLibrary.get_metadata_tag(result, "AcceptanceOwner") != WORK:
            raise RuntimeError("Refusing to overwrite an unrelated asset: " + path)
    else:
        result = unreal.EditorAssetLibrary.duplicate_asset(source.get_path_name(), path)
    if not result:
        raise RuntimeError("Cannot create data variant: " + path)
    unreal.EditorAssetLibrary.set_metadata_tag(result, "AcceptanceOwner", WORK)
    return result


def save_table(cls, prop, source, rows, name):
    result = duplicate(source, name)
    if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(result, json.dumps(rows, ensure_ascii=False)):
        raise RuntimeError("Data row import failed: " + name)
    if not unreal.EditorAssetLibrary.save_loaded_asset(result, False):
        raise RuntimeError("Data table save failed: " + name)
    raw = unreal.DataTableFunctionLibrary.export_data_table_to_json_string(result)
    readback = json.loads(raw)
    if {str(r["Name"]) for r in readback} != {str(r["Name"]) for r in rows}:
        raise RuntimeError("Saved row identities changed: " + name)
    assets[result.get_path_name()] = {"sha256": hashlib.sha256(raw.encode()).hexdigest(), "rows": readback}
    override(cls, prop, result.get_path_name())
    return result


def clone(rows, name, **changes):
    value = copy.deepcopy(next(row for row in rows if row["Name"] == name))
    value.update(changes)
    rows.append(value)
    return value


def main():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world.get_path_name().split(".")[0] != "/Game/Maps/LVL_CommanderMassPrototype":
        raise RuntimeError("Open the production prototype map before exporting variants")
    source, rows = table("GuLiUnitDataSettings", "SoldierDataTable")
    clone(rows, "DefaultSoldier", Name="AcceptanceCombinedMass", Id=101, ModelId=101001)
    clone(rows, "ElectromagneticMiner", Name="AcceptanceCombinedActor", Id=102)
    save_table("GuLiUnitDataSettings", "SoldierDataTable", source, rows, "DT_AcceptanceUnits")

    source, rows = table("GuLiCommanderDataSettings", "SkillDataTable")
    clone(rows, "Strafe", Name="AcceptanceStrafe", Id=max(r["Id"] for r in rows)+1, SkillId="Acceptance_Strafe")
    save_table("GuLiCommanderDataSettings", "SkillDataTable", source, rows, "DT_AcceptanceSkills")
    source, rows = table("GuLiCommanderDataSettings", "UnitSkillDataTable")
    clone(rows, "SoldierA_Strafe", Name="Acceptance_Strafe", Id=max(r["Id"] for r in rows)+1,
          UnitTypeId=101, SkillId="Acceptance_Strafe")
    save_table("GuLiCommanderDataSettings", "UnitSkillDataTable", source, rows, "DT_AcceptanceUnitSkills")
    source, rows = table("GuLiCommanderDataSettings", "WeaponMountDataTable")
    for original in list(rows):
        if original["UnitTypeId"] == 1:
            clone(rows, original["Name"], Name="Acceptance_"+original["Name"],
                  Id=max(r["Id"] for r in rows)+1, UnitTypeId=101)
    save_table("GuLiCommanderDataSettings", "WeaponMountDataTable", source, rows, "DT_AcceptanceMounts")

    source, rows = table("GuLiModelRegistrySettings", "ModelsTable")
    original = next(row for row in rows if row["Id"] == 1001)
    clone(rows, original["Name"], Name="AcceptancePresentation", Id=101001)
    save_table("GuLiModelRegistrySettings", "ModelsTable", source, rows, "DT_AcceptanceModels")
    source, rows = table("GuLiRogueCardSettings", "Cards")
    original = next(row for row in rows if row["Id"] == "01.01")
    clone(rows, original["Name"], Name="AcceptanceFireRate", Id="QA.101", UnitTypeId=101)
    save_table("GuLiRogueCardSettings", "Cards", source, rows, "DT_AcceptanceCards")

    source, rows = table("GuLiBuildingDataSettings", "BuildingsTable")
    clone(rows, "BasicBarracks", Name="AcceptanceActorProduction", Id=101, ProductionUnitId=102)
    # The isolated ordinary producer exercises the new Mass definition; the
    # production table outside this opt-in profile remains unchanged.
    next(row for row in rows if row["Name"]=="BasicBarracks")["ProductionUnitId"] = 101
    clone(rows, "ResourceFactory", Name="AcceptanceCombinedBuilding", Id=102)
    save_table("GuLiBuildingDataSettings", "BuildingsTable", source, rows, "DT_AcceptanceBuildings")
    original = settings("GuLiBuildingDataSettings").get_editor_property("Catalog")
    catalog = duplicate(original, "DA_AcceptanceBuildingCatalog")
    if not unreal.EditorAssetLibrary.save_loaded_asset(catalog, False):
        raise RuntimeError("Cannot save the isolated building catalog")
    override("GuLiBuildingDataSettings", "Catalog", catalog.get_path_name())
    profiles = [f'(Definition="{d}",bCargo=True,bProcessing=True,CargoCapacity=10,ProcessingCapacity=10000000,ExtractionRate=2,ProcessingRate=2)'
                for d in ("unit/101", "unit/102", "building/102")]
    override("GuLiResourceInventorySettings", "Entities", "("+",".join(profiles)+")")

    override("GuLiHybridReviewSubsystem", "bEnableResourceSupply", "True")
    # CustomConfig is an engine-supported opt-in layer; no global defaults are saved.
    config = ROOT / "Config/Custom/HybridAcceptance/DefaultGame.ini"
    config.parent.mkdir(parents=True, exist_ok=True)
    sections = {}
    for value in overrides:
        sections.setdefault(value["class"], []).append(value["property"]+"="+value["value"])
    config.write_text("\n\n".join("["+key+"]\n"+"\n".join(values) for key,values in sections.items())+"\n", encoding="utf-8")
    exports = {}
    for population in (200,600):
        path = ROOT / f"Config/Hybrid/AcceptanceManifest-{population}.json"
        response = json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest_with_overrides(
            population, str(path), json.dumps(overrides)))
        if not response.get("success"):
            raise RuntimeError("Isolated manifest export failed: " + json.dumps(response, ensure_ascii=False))
        manifest = json.loads(path.read_text(encoding="utf-8-sig"))
        for definition in ("unit/101","unit/102","building/101","building/102"):
            if "adapter/definition/"+definition not in manifest["strings"]:
                raise RuntimeError("A variant has no generic registered mechanism: " + definition)
        exports[str(population)] = {"path":str(path),"sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
            "config_hash":manifest["config_hash"],"map_hash":manifest["map_hash"],"objects":len(manifest["objects"])}
    report = {"schema":"guli-acceptance-profile/v1", "custom_config":"HybridAcceptance", "work":WORK,
        "config":str(config), "overrides":overrides, "source_tables":sources, "assets":assets, "manifests":exports,
        "definitions":{"mass":"unit/101","actor":"unit/102","producer":"building/101","building":"building/102",
                       "skill":"Acceptance_Strafe","card":"QA.101","presentation":101001},
        "scope":"Authored data and manifest readback only; gameplay and recovery are unverified"}
    path = ROOT / "Config/Hybrid/AcceptanceProfile.json"
    path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
    for population in (200,600):
        normal=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(
            population,str(ROOT/f"Config/Hybrid/RuntimeManifest-{population}.json")))
        if not normal.get("success"): raise RuntimeError("Production manifest refresh failed: "+json.dumps(normal))
    current=json.loads(unreal.GuLiHybridAuthoringLibrary.read_hybrid_review_entry())
    saved=json.loads(unreal.GuLiHybridAuthoringLibrary.save_hybrid_review_entry(current['manifest'],current['room'],current['identity'],600))
    if not saved.get('success'): raise RuntimeError('Original review entry could not be saved: '+json.dumps(saved))
    return {"success":True,"profile":str(path),"loads":exports}


try:
    response = main()
except Exception as error:
    response = {"success":False,"error":str(error)}
unreal.MCPythonHelper.submit_result(json.dumps(response,ensure_ascii=False))
