"""Build and audit the authored Wingman group StateTree through public editor APIs."""

import json
import os
import unreal


ASSET_PATH = "/Game/GuLiStrike/Wingman/ST_WingmanGroupBehavior"
OBJECT_PATH = ASSET_PATH + ".ST_WingmanGroupBehavior"
REPORT_PATH = os.path.join(
    unreal.Paths.project_dir(),
    "TestResults",
    "WingmanPlan",
    "WingmanStateTreeAsset",
    "state_tree_asset_audit.json",
)
REQUIRED_STATES = {
    "JoiningEscort",
    "EscortOrbit",
    "EmergencyAvoid",
    "OwnerUnavailable",
    "Dead",
}


def prop(obj, name, default=None):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


previous_report_time = os.path.getmtime(REPORT_PATH) if os.path.exists(REPORT_PATH) else -1.0
unreal.SystemLibrary.execute_console_command(None, "gs.Wingman.BuildStateTree")
asset = unreal.EditorAssetLibrary.load_asset(ASSET_PATH)
if not os.path.exists(REPORT_PATH):
    raise RuntimeError("C++ StateTree builder did not produce an audit report")
with open(REPORT_PATH, encoding="utf-8") as handle:
    report = json.load(handle)
success = bool(
    asset
    and report.get("success")
    and report.get("asset_path") == OBJECT_PATH
    and REQUIRED_STATES == set(report.get("states", []))
    and report.get("policy_task_count") == 5
    and report.get("policy_condition_count") == 5
    and report.get("root_event_transition_count") == 5
    and report.get("compiled_editor_data_hash", 0) != 0
    and report.get("ready_to_run") is True
    and report.get("package_dirty") is False
    and os.path.getmtime(REPORT_PATH) >= previous_report_time
)
if not success:
    raise RuntimeError("Wingman authored StateTree audit failed: " + json.dumps(report, ensure_ascii=False))
unreal.log("Wingman authored StateTree deploy/audit succeeded: " + REPORT_PATH)
