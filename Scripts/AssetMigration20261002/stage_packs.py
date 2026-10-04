"""Stage the requested archives and deliver only assets rewritten by Unreal."""

import argparse
import collections
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tempfile
import zipfile


PROJECT = Path("D:/UE5.7/test1")
REPORTS = PROJECT / "TestResults/AssetMigration20261002"
MANIFEST = REPORTS / "manifest.json"
EXTENSIONS = {".uasset", ".umap", ".uexp", ".ubulk"}
SOURCES = [
    ("SSF_Buildings", "D:/BaiduNetdiskDownload/Set of Sci-Fi Buildings 5.3/SSF_Buildings.7z", "SSF_Buildings/"),
    ("ControlRig", "D:/BaiduNetdiskDownload/Control Rig Samples Pack.zip", "Control Rig Samples Pack/Content/ControlRig/"),
    ("RSG_UnderWater_Pack", "D:/BaiduNetdiskDownload/Stylized Sci Fi Mech Robot Asset.zip", "Stylized Sci Fi Mech Robot Asset/Content/RSG_UnderWater_Pack/"),
]


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def safe_relative(name):
    path = PurePosixPath(name.replace("\\", "/"))
    if path.is_absolute() or not path.parts or any(p in (".", "..") or ":" in p for p in path.parts):
        raise RuntimeError(f"Unsafe archive path: {name}")
    return path


def inventory(source, prefix):
    source = Path(source)
    if source.suffix == ".zip":
        with zipfile.ZipFile(source) as archive:
            entries = [{"name": item.filename.replace("\\", "/"), "size": item.file_size, "crc": item.CRC}
                       for item in archive.infolist() if not item.is_dir()]
    else:
        listing = subprocess.run(["tar", "-tvf", str(source)], capture_output=True, check=True).stdout.decode("utf-8", "replace")
        entries = []
        for line in listing.splitlines():
            fields = line.split(None, 8)
            if len(fields) != 9 or fields[0][0] not in "-d":
                raise RuntimeError(f"Unsupported archive entry: {line}")
            safe_relative(fields[8])
            if fields[0][0] == "-":
                entries.append({"name": fields[8], "size": int(fields[4])})
    selected = []
    keys = set()
    for entry in entries:
        if not entry["name"].startswith(prefix):
            if PurePosixPath(entry["name"]).suffix.lower() in EXTENSIONS:
                raise RuntimeError(f"Unexpected package outside selected content: {entry['name']}")
            continue
        relative = safe_relative(entry["name"][len(prefix):])
        if relative.suffix.lower() not in EXTENSIONS:
            raise RuntimeError(f"Unexpected file in asset folder: {entry['name']}")
        if str(relative).casefold() in keys:
            raise RuntimeError(f"Duplicate Windows path: {relative}")
        keys.add(str(relative).casefold())
        selected.append(dict(entry, relative=str(relative)))
    return selected, len(entries) - len(selected)


def stage():
    if MANIFEST.exists():
        raise RuntimeError("Migration manifest already exists; reuse it instead of staging again")
    plans = []
    for root, source, prefix in SOURCES:
        if (PROJECT / "Content/Assets" / root).exists():
            raise RuntimeError(f"Destination already exists: {root}")
        entries, ignored = inventory(source, prefix)
        plans.append({"root": root, "archive": source, "prefix": prefix, "entries": entries,
                      "source": "/Game/" + root, "target": "/Game/Assets/" + root,
                      "ignored_non_content_files": ignored,
                      "package_count": sum(PurePosixPath(e["relative"]).suffix in {".uasset", ".umap"} for e in entries)})
    older, _ = inventory("D:/BaiduNetdiskDownload/Set of Sci-Fi Buildings 5.1/SSF_Buildings.zip", "SSF_Buildings/")
    old_sizes = {e["relative"]: e["size"] for e in older}
    latest_sizes = {e["relative"]: e["size"] for e in plans[0]["entries"]}
    older_only = sorted(old_sizes.keys() - latest_sizes.keys())
    for entry in older:
        if entry["relative"] in older_only:
            plans[0]["entries"].append(dict(entry, source_archive="D:/BaiduNetdiskDownload/Set of Sci-Fi Buildings 5.1/SSF_Buildings.zip"))
            if PurePosixPath(entry["relative"]).suffix in {".uasset", ".umap"}:
                plans[0]["package_count"] += 1
    task_temp_parent = Path("D:/UE5.7/tmp")
    task_temp_parent.mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix="guli_asset_migration_20261002_", dir=task_temp_parent))
    content = scratch / "Content"
    content.mkdir()
    manifest = {"project": str(PROJECT), "scratch": str(scratch), "packs": plans,
                "building_version_selection": {"selected": "5.3", "older_version": "5.1",
                    "same_package_paths": old_sizes.keys() == latest_sizes.keys(),
                    "preserved_older_only_files": older_only,
                    "size_changed_files": sorted(k for k in old_sizes.keys() & latest_sizes.keys() if old_sizes[k] != latest_sizes[k])},
                "empty_source_folder": "D:/BaiduNetdiskDownload/Set of Sci-Fi Buildings 5.2"}
    write_json(MANIFEST, manifest)
    for plan in plans:
        destination = content / plan["root"]
        destination.mkdir()
        if Path(plan["archive"]).suffix == ".zip":
            with zipfile.ZipFile(plan["archive"]) as archive:
                for entry in plan["entries"]:
                    target = destination / entry["relative"]
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with archive.open(entry["name"]) as incoming, target.open("xb") as output:
                        shutil.copyfileobj(incoming, output)
        else:
            subprocess.run(["tar", "-xf", plan["archive"], "-C", str(content)], check=True)
            for entry in plan["entries"]:
                if "source_archive" in entry:
                    target = destination / entry["relative"]
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with zipfile.ZipFile(entry["source_archive"]) as archive:
                        with archive.open(entry["name"]) as incoming, target.open("xb") as output:
                            shutil.copyfileobj(incoming, output)
        for entry in plan["entries"]:
            if (destination / entry["relative"]).stat().st_size != entry["size"]:
                raise RuntimeError(f"Extraction size mismatch: {entry['name']}")
        plan["staged"] = True
        write_json(MANIFEST, manifest)
    descriptor = {"FileVersion": 3, "Plugins": [{"Name": name, "Enabled": True} for name in
                  ["PythonScriptPlugin", "EditorScriptingUtilities", "ControlRig", "FullBodyIK", "IKRig"]]}
    write_json(scratch / "AssetMigration.uproject", descriptor)
    manifest["scratch_project"] = str(scratch / "AssetMigration.uproject")
    manifest["staged"] = True
    write_json(MANIFEST, manifest)
    print(json.dumps({"scratch_project": manifest["scratch_project"], "packs": [
        {"target": p["target"], "packages": p["package_count"], "bytes": sum(e["size"] for e in p["entries"])} for p in plans],
        "building_version_selection": manifest["building_version_selection"]}, ensure_ascii=False))


def deliver():
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    report = json.loads((REPORTS / "unreal_migration.json").read_text(encoding="utf-8"))
    if not report.get("migration_complete") or not report.get("passed"):
        raise RuntimeError("Unreal migration or dependency validation did not pass")
    scratch = Path(manifest["scratch"]).resolve()
    if not scratch.is_relative_to(Path("D:/UE5.7/tmp").resolve()):
        raise RuntimeError("Scratch path escaped the migration directory")
    exports = []
    for pack in report["packs"]:
        if not pack.get("saved") or pack.get("load_failures") or pack.get("old_path_dependencies"):
            raise RuntimeError(f"Asset validation failed for {pack['root']}")
        final_root = PROJECT / "Content/Assets" / pack["root"]
        if final_root.exists():
            raise RuntimeError(f"Destination already exists: {final_root}")
        for package in pack["destination_packages"]:
            if not package.startswith("/Game/Assets/" + pack["root"] + "/"):
                raise RuntimeError(f"Unexpected export package: {package}")
            relative = Path(package.removeprefix("/Game/"))
            primary = [extension for extension in (".uasset", ".umap") if (scratch / "Content" / relative).with_suffix(extension).is_file()]
            if len(primary) != 1:
                raise RuntimeError(f"No unique primary file for {package}")
            for extension in primary + [e for e in (".uexp", ".ubulk") if (scratch / "Content" / relative).with_suffix(e).is_file()]:
                source = (scratch / "Content" / relative).with_suffix(extension)
                target = (PROJECT / "Content" / relative).with_suffix(extension)
                if target.exists():
                    raise RuntimeError(f"Export would overwrite {target}")
                exports.append((source, target, package))
    delivered = []
    for source, target, package in exports:
        target.parent.mkdir(parents=True, exist_ok=True)
        with source.open("rb") as incoming, target.open("xb") as output:
            shutil.copyfileobj(incoming, output)
        if source.stat().st_size != target.stat().st_size:
            raise RuntimeError(f"Delivery size mismatch: {target}")
        delivered.append({"package": package, "file": str(target), "bytes": target.stat().st_size})
    result = {"passed": True, "files": delivered, "file_count": len(delivered), "bytes": sum(e["bytes"] for e in delivered)}
    write_json(REPORTS / "delivery.json", result)
    print(json.dumps({k: v for k, v in result.items() if k != "files"}, ensure_ascii=False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["stage", "deliver"])
    args = parser.parse_args()
    stage() if args.action == "stage" else deliver()
