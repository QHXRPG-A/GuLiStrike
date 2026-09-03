"""Audit Ship Enhanced Input assets through public Unreal Editor Python APIs.

Run with: python Scripts/ue_exec.py Scripts/audit_ship_input_assets.py
"""

from __future__ import annotations

import json
import os

import unreal


PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
REPORT_PATH = os.path.join(
    PROJECT_DIR,
    "TestResults",
    "WingmanPlan",
    "ShipInputAudit",
    "ship_input_audit.json",
)


def _path(value):
    return str(value.get_path_name()) if value is not None else ""


def main():
    context = unreal.load_asset("/Game/GuLiStrike/Input/IMC_Ship")
    blueprint_class = unreal.EditorAssetLibrary.load_blueprint_class(
        "/Game/GuLiStrike/Ship/BP_GuLiStrikeShip"
    )
    if context is None or blueprint_class is None:
        raise RuntimeError("Ship mapping context or Ship Blueprint class is missing")

    mappings = []
    mapping_source = "default_key_mappings"
    try:
        mapping_data = context.get_editor_property("default_key_mappings")
        raw_mappings = mapping_data.get_editor_property("mappings")
    except Exception:
        mapping_source = "mappings_legacy"
        raw_mappings = context.get_editor_property("mappings")
    for mapping in raw_mappings:
        action = mapping.get_editor_property("action")
        key = mapping.get_editor_property("key")
        mappings.append(
            {
                "action": _path(action),
                "key": str(key.get_editor_property("key_name")),
            }
        )

    cdo = unreal.get_default_object(blueprint_class)
    input_properties = {}
    for name in (
        "ship_mapping_context",
        "fire_action",
        "boost_action",
        "cycle_engines_action",
        "cycle_weapons_action",
        "wingman_missile_action",
    ):
        try:
            input_properties[name] = _path(cdo.get_editor_property(name))
        except Exception as error:
            input_properties[name] = {"unavailable": str(error)}

    missile_action = unreal.load_asset(
        "/Game/GuLiStrike/Input/Actions/IA_Ship_WingmanMissile"
    )
    missile_action_record = {"exists": missile_action is not None}
    if missile_action is not None:
        missile_action_record.update(
            {
                "object_path": _path(missile_action),
                "value_type": str(missile_action.get_editor_property("value_type")),
                "triggers": [
                    _path(value)
                    for value in missile_action.get_editor_property("triggers")
                ],
                "modifiers": [
                    _path(value)
                    for value in missile_action.get_editor_property("modifiers")
                ],
            }
        )

    report = {
        "schema": 1,
        "engine_version": unreal.SystemLibrary.get_engine_version(),
        "mapping_context": _path(context),
        "mapping_source": mapping_source,
        "mapping_api": [name for name in dir(context) if "map" in name.lower()],
        "mappings": sorted(mappings, key=lambda value: (value["key"], value["action"])),
        "ship_blueprint_class": _path(blueprint_class),
        "ship_cdo_input_properties": input_properties,
        "missile_action": missile_action_record,
    }
    os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
    with open(REPORT_PATH, "w", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")
    unreal.log("Ship input audit: " + REPORT_PATH)
    return report


RESULT = main()
