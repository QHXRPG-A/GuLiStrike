"""Validate saved FlightNav data assets in a fresh Unreal Editor process.

This is deliberately separate from deployment: validation in the process that
performed a bake can accidentally inspect the still-loaded UObject rather than
the bytes that were committed to the package.  Run with the Python commandlet:

    UnrealEditor-Cmd.exe GuLiStrike.uproject -run=pythonscript \
        -script=Scripts/validate_wingman_flightnav_roundtrip.py
"""

from __future__ import annotations

import json
import os
import tempfile
from typing import Any

import unreal


SCHEMA = "guli.flightnav.saved-roundtrip.v1"
DATA_ASSETS = (
    "/Game/GuLiStrike/Navigation/Baked/LVL_CommanderMassPrototype/"
    "DA_FlightNav_LVL_CommanderMassPrototype",
    "/Game/GuLiStrike/Navigation/Baked/LVL_Main/DA_FlightNav_LVL_Main",
    "/Game/GuLiStrike/Navigation/Baked/LVL_ShipTest/DA_FlightNav_LVL_ShipTest",
)
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
REPORT_PATH = os.path.join(
    PROJECT_DIR,
    "TestResults",
    "WingmanPlan",
    "AssetDeployment",
    "saved_roundtrip_validation.json",
)


def _atomic_write_json(path: str, value: dict[str, Any]) -> None:
    directory = os.path.dirname(path)
    os.makedirs(directory, exist_ok=True)
    handle, temporary_path = tempfile.mkstemp(
        prefix="flightnav_roundtrip_", suffix=".json.tmp", dir=directory
    )
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2, sort_keys=True)
            stream.write("\n")
        os.replace(temporary_path, path)
    except Exception:
        try:
            os.unlink(temporary_path)
        except OSError:
            pass
        raise


def _validation_result(value: Any) -> tuple[bool, str]:
    if isinstance(value, tuple):
        if not value:
            return False, "ValidateNavigationData returned an empty tuple"
        return bool(value[0]), str(value[1]) if len(value) > 1 else ""
    return bool(value), ""


def main() -> dict[str, Any]:
    library = getattr(unreal, "GuLiFlightNavigationEditorLibrary", None)
    records: list[dict[str, Any]] = []
    if library is None:
        result = {
            "schema": SCHEMA,
            "success": False,
            "engine_version": unreal.SystemLibrary.get_engine_version(),
            "error": "GuLiFlightNavigationEditorLibrary is unavailable",
            "assets": records,
        }
        _atomic_write_json(REPORT_PATH, result)
        raise RuntimeError(result["error"])

    for asset_path in DATA_ASSETS:
        asset = unreal.EditorAssetLibrary.load_asset(asset_path)
        record: dict[str, Any] = {"asset": asset_path, "loaded": asset is not None}
        if asset is None:
            record.update(valid=False, error="Asset failed to load")
        else:
            metadata = asset.get_editor_property("metadata")
            valid, error = _validation_result(library.validate_navigation_data(asset))
            record.update(
                valid=valid,
                error=error,
                definition_revision=int(
                    metadata.get_editor_property("definition_revision")
                ),
                content_checksum=int(metadata.get_editor_property("content_checksum")),
                node_count=len(asset.get_editor_property("nodes")),
                cell_count=len(asset.get_editor_property("cells")),
                portal_count=len(asset.get_editor_property("portals")),
                link_count=len(asset.get_editor_property("links")),
            )
        records.append(record)

    result = {
        "schema": SCHEMA,
        "success": all(record.get("valid", False) for record in records),
        "engine_version": unreal.SystemLibrary.get_engine_version(),
        "assets": records,
    }
    _atomic_write_json(REPORT_PATH, result)
    if not result["success"]:
        raise RuntimeError("One or more saved FlightNav assets failed validation")
    unreal.log("Saved FlightNav round-trip validation report: " + REPORT_PATH)
    return result


RESULT = main()
