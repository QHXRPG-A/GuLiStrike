"""Create and map the V1 Wingman missile InputAction without touching source assets.

Run with PIE stopped:
    python Scripts/ue_exec.py Scripts/deploy_ship_wingman_missile_input.py
"""

from __future__ import annotations

import json
import os
import traceback

import unreal


OWNER = "GuLi.ShipWingmanMissileInput.v1"
SOURCE_ACTION = "/Game/GuLiStrike/Input/Actions/IA_Ship_Fire"
MISSILE_ACTION = "/Game/GuLiStrike/Input/Actions/IA_Ship_WingmanMissile"
MAPPING_CONTEXT = "/Game/GuLiStrike/Input/IMC_Ship"
MISSILE_KEY = "RightMouseButton"
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
REPORT_PATH = os.path.join(
    PROJECT_DIR,
    "TestResults",
    "WingmanPlan",
    "ShipMissileInputDeployment",
    "index.json",
)


class DeploymentError(RuntimeError):
    pass


def _package_name(package):
    return str(package.get_name()) if package is not None else ""


def _dirty_packages():
    packages = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    packages.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    unique = {_package_name(package): package for package in packages}
    return [unique[name] for name in sorted(unique)]


def _require_clean(context):
    dirty = [_package_name(package) for package in _dirty_packages()]
    if dirty:
        raise DeploymentError(f"{context}: editor has dirty packages: {dirty}")


def _mappings(context):
    data = context.get_editor_property("default_key_mappings")
    return list(data.get_editor_property("mappings"))


def _key_name(mapping):
    key = mapping.get_editor_property("key")
    return str(key.get_editor_property("key_name"))


def _action_path(mapping):
    action = mapping.get_editor_property("action")
    return str(action.get_path_name()) if action is not None else ""


def _make_key(name):
    key = unreal.Key()
    key.set_editor_property("key_name", unreal.Name(name))
    return key


def _write_report(report):
    os.makedirs(os.path.dirname(REPORT_PATH), exist_ok=True)
    with open(REPORT_PATH, "w", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2, sort_keys=True)
        stream.write("\n")


def main():
    report = {
        "schema": 1,
        "success": False,
        "owner": OWNER,
        "action": MISSILE_ACTION,
        "mapping_context": MAPPING_CONTEXT,
        "key": MISSILE_KEY,
        "engine_version": unreal.SystemLibrary.get_engine_version(),
    }
    try:
        editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        if editor.get_game_world() is not None:
            raise DeploymentError("PIE/SIE is active")
        # A previous interrupted run may leave only this script's two exact
        # packages dirty. Resume that transaction, but never adopt unrelated edits.
        allowed_preflight_dirty = {MISSILE_ACTION, MAPPING_CONTEXT}
        preflight_dirty = {
            _package_name(package) for package in _dirty_packages()
        }
        unexpected_preflight_dirty = sorted(
            preflight_dirty - allowed_preflight_dirty
        )
        if unexpected_preflight_dirty:
            raise DeploymentError(
                "preflight: editor has unrelated dirty packages: "
                + repr(unexpected_preflight_dirty)
            )
        report["preflight_dirty_packages"] = sorted(preflight_dirty)

        source = unreal.load_asset(SOURCE_ACTION)
        context = unreal.load_asset(MAPPING_CONTEXT)
        if source is None or context is None:
            raise DeploymentError("Source InputAction or IMC_Ship is missing")

        created = False
        if unreal.EditorAssetLibrary.does_asset_exist(MISSILE_ACTION):
            action = unreal.load_asset(MISSILE_ACTION)
            owner = str(
                unreal.EditorAssetLibrary.get_metadata_tag(
                    action, "GuLi.Deployment.Owner"
                )
                or ""
            )
            if owner != OWNER:
                raise DeploymentError(
                    f"Refusing to adopt existing unowned action (owner={owner!r})"
                )
        else:
            action = unreal.EditorAssetLibrary.duplicate_asset(
                SOURCE_ACTION, MISSILE_ACTION
            )
            if action is None:
                raise DeploymentError("Unable to duplicate IA_Ship_Fire")
            created = True

        if action.get_class().get_path_name() != source.get_class().get_path_name():
            raise DeploymentError("Derived action class differs from source action")
        unreal.EditorAssetLibrary.set_metadata_tag(
            action, "GuLi.Deployment.Owner", OWNER
        )
        unreal.EditorAssetLibrary.set_metadata_tag(
            action, "GuLi.Deployment.Source", SOURCE_ACTION
        )

        action_object_path = str(action.get_path_name())
        before = _mappings(context)
        conflicts = [
            _action_path(mapping)
            for mapping in before
            if _key_name(mapping) == MISSILE_KEY
            and _action_path(mapping) != action_object_path
        ]
        if conflicts:
            raise DeploymentError(
                f"{MISSILE_KEY} is already mapped to another action: {conflicts}"
            )

        added_mapping = not any(
            _key_name(mapping) == MISSILE_KEY
            and _action_path(mapping) == action_object_path
            for mapping in before
        )
        if added_mapping:
            context.modify()
            context.map_key(action, _make_key(MISSILE_KEY))

        after = _mappings(context)
        matching = [
            mapping
            for mapping in after
            if _key_name(mapping) == MISSILE_KEY
            and _action_path(mapping) == action_object_path
        ]
        if len(matching) != 1:
            raise DeploymentError(
                f"Expected exactly one missile mapping after update, got {len(matching)}"
            )

        expected_packages = {
            _package_name(action.get_outermost()),
            _package_name(context.get_outermost()),
        }
        dirty = _dirty_packages()
        dirty_names = {_package_name(package) for package in dirty}
        if added_mapping and _package_name(context.get_outermost()) not in dirty_names:
            raise DeploymentError("IMC_Ship was changed but its package was not marked dirty")
        unexpected = [
            _package_name(package)
            for package in dirty
            if _package_name(package) not in expected_packages
        ]
        if unexpected:
            raise DeploymentError(f"Unrelated packages became dirty: {unexpected}")
        if dirty and not unreal.EditorLoadingAndSavingUtils.save_packages(dirty, True):
            raise DeploymentError("save_packages returned false")
        _require_clean("post-save")

        report.update(
            {
                "success": True,
                "created_action": created,
                "added_mapping": added_mapping,
                "action_object_path": action_object_path,
                "mapping_count": len(after),
                "saved_packages": sorted(dirty_names),
            }
        )
    except Exception as error:
        report["error"] = str(error)
        report["traceback"] = traceback.format_exc()
        report["dirty_packages_after_failure"] = [
            _package_name(package) for package in _dirty_packages()
        ]
    _write_report(report)
    if not report["success"]:
        raise DeploymentError(report["error"])
    unreal.log("Ship missile input deployment: " + REPORT_PATH)
    return report


RESULT = main()
