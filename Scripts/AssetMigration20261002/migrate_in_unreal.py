"""Run only in the isolated scratch project with UnrealEditor-Cmd."""

import collections
import json
import os
from pathlib import Path
import traceback
import unreal


REPORTS = Path("D:/UE5.7/test1/TestResults/AssetMigration20261002")
manifest = json.loads((REPORTS / "manifest.json").read_text(encoding="utf-8"))
report = {"migration_complete": False, "packs": [], "preflight": {}, "engine_version": unreal.SystemLibrary.get_engine_version()}
REFERENCE_REPAIRS = {
    '/Game/SSF_Buildings/Buildings/CommandCenter/Meshes/TB1_CommandCenter.TB1_CommandCenter':
        '/Game/SSF_Buildings/Buildings/CommandCenter/Meshes/SK_TB1_CommandCenter.SK_TB1_CommandCenter',
    '/Game/Characters/Heroes/Mannequin/Meshes/SKM_Manny.SKM_Manny':
        '/Game/ControlRig/Characters/Mannequins/Meshes/SKM_Manny.SKM_Manny',
    '/Game/Characters/Heroes/Mannequin/Meshes/SKM_Quinn.SKM_Quinn':
        '/Game/ControlRig/Characters/Mannequins/Meshes/SKM_Quinn.SKM_Quinn',
}


def checkpoint(phase):
    report["phase"] = phase
    temporary = REPORTS / "unreal_migration.json.tmp"
    temporary.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, REPORTS / "unreal_migration.json")
    unreal.log("ASSET_MIGRATION: " + phase)


def inventory(registry, root):
    return [data for data in registry.get_assets_by_path(root, recursive=True)
            if not str(data.asset_class_path.asset_name).endswith('GeneratedClass')]


def class_name(data):
    return str(data.asset_class_path.asset_name)


def dependencies(registry, packages):
    options = unreal.AssetRegistryDependencyOptions(include_hard_package_references=True, include_soft_package_references=True)
    result = {}
    for package in packages:
        result[package] = sorted(str(value) for value in registry.get_dependencies(package, options) or [])
    return result


try:
    actual_project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.get_project_file_path())).resolve()
    if actual_project != Path(manifest["scratch_project"]).resolve():
        raise RuntimeError("Refusing to run migration outside the isolated scratch project")
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    registry.wait_for_completion()
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    for plan in manifest["packs"]:
        assets = inventory(registry, plan["source"])
        source_packages = sorted({str(a.package_name) for a in assets})
        expected = {plan['source'] + '/' + str(Path(e['relative']).with_suffix('')).replace('\\', '/')
                    for e in plan['entries'] if Path(e['relative']).suffix in {'.uasset', '.umap'}}
        excluded_redirectors = {}
        empty_package_files = []
        report['preflight'][plan['root']] = {'unindexed_packages': sorted(expected - set(source_packages)), 'objects': {}}
        for package in sorted(expected - set(source_packages)):
            object_path = package + '.' + package.rsplit('/', 1)[-1]
            obj = unreal.load_object(None, object_path, follow_redirectors=False)
            diagnostic = {'class': obj.get_class().get_name() if obj else None}
            report['preflight'][plan['root']]['objects'][package] = diagnostic
            if obj and isinstance(obj, unreal.ObjectRedirector):
                destination_object = obj.get_editor_property('destination_object')
                diagnostic['destination'] = destination_object.get_path_name() if destination_object else None
                excluded_redirectors[package] = diagnostic['destination']
            elif obj is None:
                loaded_package = unreal.load_package(package)
                members = [{'name': o.get_name(), 'class': o.get_class().get_name()}
                           for o in unreal.ObjectIterator() if loaded_package and o.get_outer() == loaded_package]
                diagnostic['package_loaded'] = loaded_package is not None
                diagnostic['package_objects'] = members
                if loaded_package and all(m['class'] == 'MetaData' for m in members):
                    empty_package_files.append(package)
            checkpoint('preflight ' + plan['root'])
            if package not in excluded_redirectors and package not in empty_package_files:
                raise RuntimeError('Archive package was not indexed and is not a redirector: ' + package)
        if set(source_packages) - expected or len(source_packages) + len(excluded_redirectors) + len(empty_package_files) != plan['package_count']:
            raise RuntimeError('Source package inventory does not match the archives')
        if inventory(registry, plan["target"]):
            raise RuntimeError("Scratch migration destination already contains assets")
        state = {"root": plan["root"], "source": plan["source"], "target": plan["target"],
                 "source_packages": source_packages, "source_classes": dict(collections.Counter(class_name(a) for a in assets)),
                 "source_redirectors": list(excluded_redirectors), "redirector_targets": excluded_redirectors,
                 "empty_package_files": empty_package_files,
                 "archive_package_files": plan['package_count'], "destination_packages": [], "saved": False}
        report["packs"].append(state)
        initial_dependencies = dependencies(registry, source_packages)
        state["source_missing_game_dependencies"] = {
            package: [d for d in deps if d.startswith('/Game/') and not unreal.EditorAssetLibrary.does_asset_exist(d)]
            for package, deps in initial_dependencies.items()
            if any(d.startswith('/Game/') and not unreal.EditorAssetLibrary.does_asset_exist(d) for d in deps)}
        checkpoint("loading " + plan["root"])
        renames = []
        loaded_assets = []
        for index, data in enumerate(assets):
            package = str(data.package_name)
            if class_name(data) == "ObjectRedirector":
                state["source_redirectors"].append(package)
                obj = unreal.load_object(None, package + '.' + str(data.asset_name), follow_redirectors=False)
                destination_object = obj.get_editor_property('destination_object') if obj else None
                state['redirector_targets'][package] = destination_object.get_path_name() if destination_object else None
                continue
            loaded = data.get_asset()
            if not loaded:
                raise RuntimeError("Source asset could not load: " + package)
            destination = plan["target"] + package[len(plan["source"]):]
            new_path, new_name = destination.rsplit('/', 1)
            renames.append(unreal.AssetRenameData(asset=loaded, new_package_path=new_path, new_name=new_name))
            loaded_assets.append(loaded)
            state["destination_packages"].append(destination)
            if index % 25 == 0:
                checkpoint(f"loading {plan['root']} {index + 1}/{len(assets)}")
        checkpoint("renaming " + plan["root"])
        state["renamed"] = bool(asset_tools.rename_assets(renames))
        if not state["renamed"]:
            raise RuntimeError("Unreal asset rename failed for " + plan["root"])
        alias_map = {}
        for package, destination_object_path in state['redirector_targets'].items():
            if destination_object_path:
                destination = destination_object_path.replace(plan['source'] + '/', plan['target'] + '/', 1)
                alias_map[unreal.SoftObjectPath(package + '.' + package.rsplit('/', 1)[-1])] = unreal.SoftObjectPath(destination)
        state['repaired_reference_paths'] = {}
        missing_paths = {path for paths in state['source_missing_game_dependencies'].values() for path in paths}
        for old_path, new_path in REFERENCE_REPAIRS.items():
            if old_path.split('.', 1)[0] in missing_paths:
                destination = new_path.replace(plan['source'] + '/', plan['target'] + '/', 1)
                if not unreal.EditorAssetLibrary.does_asset_exist(destination):
                    raise RuntimeError('Replacement mesh was not supplied by the archives: ' + destination)
                alias_map[unreal.SoftObjectPath(old_path)] = unreal.SoftObjectPath(destination)
                state['repaired_reference_paths'][old_path] = destination
        if alias_map:
            asset_tools.rename_referencing_soft_object_paths([asset.get_outer() for asset in loaded_assets], alias_map)
        checkpoint("saving " + plan["root"])
        state["saved"] = bool(unreal.EditorAssetLibrary.save_directory(plan["target"], only_if_is_dirty=False, recursive=True))
        if not state["saved"]:
            raise RuntimeError("Unreal save failed for " + plan["root"])
        registry.scan_paths_synchronous([plan["source"], plan["target"]], True)
        targets = inventory(registry, plan["target"])
        actual_packages = sorted({str(data.package_name) for data in targets if class_name(data) != 'ObjectRedirector'})
        if actual_packages != sorted(state["destination_packages"]):
            raise RuntimeError("Destination inventory differs from renamed assets")
        state["destination_packages"] = actual_packages
        state["destination_classes"] = dict(collections.Counter(class_name(a) for a in targets))
        final_dependencies = dependencies(registry, actual_packages)
        state["old_path_dependencies"] = {p: [d for d in deps if d.startswith(plan['source'] + '/')]
                                         for p, deps in final_dependencies.items() if any(d.startswith(plan['source'] + '/') for d in deps)}
        state["missing_game_dependencies"] = {p: [d for d in deps if d.startswith('/Game/') and not unreal.EditorAssetLibrary.does_asset_exist(d)]
                                            for p, deps in final_dependencies.items() if any(d.startswith('/Game/') and not unreal.EditorAssetLibrary.does_asset_exist(d) for d in deps)}
        state["load_failures"] = [p for p in actual_packages if unreal.EditorAssetLibrary.load_asset(p) is None]
        state["package_count"] = len(actual_packages)
        checkpoint("validated " + plan["root"])
    report["migration_complete"] = True
    report["passed"] = all(not p["old_path_dependencies"] and not p["missing_game_dependencies"] and not p["load_failures"] for p in report["packs"])
    checkpoint("complete")
except Exception:
    report["error"] = traceback.format_exc()
    checkpoint("failed")
    unreal.log_error(report["error"])
    raise
