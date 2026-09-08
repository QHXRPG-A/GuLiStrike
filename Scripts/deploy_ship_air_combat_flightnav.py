"""Bake map-specific FlightNav data for the Ship air-combat prototype."""

from __future__ import annotations

import json
import os
import sys
import types

import unreal


PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
DEPLOY_SCRIPT = os.path.join(PROJECT_DIR, "Scripts", "deploy_wingman_flightnav_assets.py")

os.environ["GULI_FLIGHTNAV_TARGET_MAPS"] = (
    "/Game/Maps/LVL_ShipWingmanAirCombatPrototype"
)
os.environ["GULI_FLIGHTNAV_SKIP_DERIVED_MESH"] = "1"
os.environ["GULI_FLIGHTNAV_REPORT_PATH"] = os.path.join(
    PROJECT_DIR,
    "TestResults",
    "ShipAirCombatLevel",
    "flightnav_report.json",
)

module = types.ModuleType("ship_air_combat_flightnav_deployment")
module.__file__ = DEPLOY_SCRIPT
sys.modules[module.__name__] = module
with open(DEPLOY_SCRIPT, "r", encoding="utf-8-sig") as stream:
    source = stream.read()
exec(compile(source, DEPLOY_SCRIPT, "exec"), module.__dict__)

RESULT = module.__dict__["RESULT"]
unreal.MCPythonHelper.submit_result(json.dumps(RESULT, ensure_ascii=False))
