"""Stage the two requested archives without overwriting existing project assets.

The UE package directory is staged at its original /Game path. Run the matching
Unreal Editor migration script afterward to move it into /Game/Assets/VFX.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import tempfile
import zipfile
from pathlib import Path, PurePosixPath


PROJECT = Path(__file__).resolve().parents[1]
CONTENT = PROJECT / "Content"
TEMP_PARENT = Path("D:/UE5.7/tmp").resolve()
REPORT = PROJECT / "TestResults/AssetMigration20260924/extraction_report.json"

ARCHIVES = (
    {
        "source": Path("D:/BaiduNetdiskDownload/尼亚加拉升级辉光_UE5.1+.zip"),
        "prefix": "尼亚加拉升级辉光_UE5.1+/Content/NiagaraUpgradeGlow/",
        "destination": CONTENT / "NiagaraUpgradeGlow",
        "extensions": {".uasset", ".umap"},
        "expected_files": 202,
    },
    {
        "source": Path("D:/BaiduNetdiskDownload/5M2CUWY.zip"),
        "prefix": "HUDuiPRO_cockpit2 folder/",
        "destination": CONTENT / "Assets/UI/HUDuiPRO/Source",
        "extensions": {".aep", ".mov", ".png", ".txt"},
        "expected_files": 5,
    },
)


def archive_files(config: dict) -> list[tuple[zipfile.ZipInfo, Path]]:
    source = config["source"]
    with zipfile.ZipFile(source) as archive:
        selected = []
        seen = set()
        for entry in archive.infolist():
            name = entry.filename.replace("\\", "/")
            if entry.is_dir():
                continue
            if not name.startswith(config["prefix"]):
                if name == "尼亚加拉升级辉光_UE5.1+/安装说明.txt":
                    continue
                raise RuntimeError(f"Unexpected archive entry: {source}: {name}")
            relative = PurePosixPath(name[len(config["prefix"]):])
            if (
                not relative.parts
                or any(part in ("", ".", "..") or ":" in part for part in relative.parts)
                or relative.suffix.lower() not in config["extensions"]
            ):
                raise RuntimeError(f"Unsafe or unsupported archive entry: {name}")
            collision_key = str(relative).casefold()
            if collision_key in seen:
                raise RuntimeError(f"Windows path collision in archive: {name}")
            seen.add(collision_key)
            selected.append((entry, Path(*relative.parts)))
    if len(selected) != config["expected_files"]:
        raise RuntimeError(
            f"Unexpected file count in {source}: {len(selected)}; "
            f"expected {config['expected_files']}"
        )
    return selected


def preflight() -> list[dict]:
    if not TEMP_PARENT.is_dir():
        raise RuntimeError(f"Temporary parent is missing: {TEMP_PARENT}")
    if (CONTENT / "Assets/VFX/NiagaraUpgradeGlow").exists():
        raise RuntimeError("Unreal destination already exists; no overwrite is allowed")
    report = []
    for config in ARCHIVES:
        source = config["source"]
        destination = config["destination"].resolve()
        if not source.is_file():
            raise RuntimeError(f"Source ZIP is missing: {source}")
        if not destination.is_relative_to(CONTENT.resolve()):
            raise RuntimeError(f"Destination escapes project Content: {destination}")
        if destination.exists():
            raise RuntimeError(f"Destination already exists: {destination}")
        files = archive_files(config)
        report.append(
            {
                "source": str(source),
                "destination": str(destination),
                "file_count": len(files),
                "uncompressed_bytes": sum(entry.file_size for entry, _ in files),
                "extensions": sorted({relative.suffix.lower() for _, relative in files}),
            }
        )
    return report


def extract(report: list[dict]) -> None:
    temporary_root = Path(tempfile.mkdtemp(prefix="guli_asset_import_20260924_", dir=TEMP_PARENT))
    if not temporary_root.resolve().is_relative_to(TEMP_PARENT):
        raise RuntimeError("Temporary directory escaped the intended parent")
    try:
        for index, config in enumerate(ARCHIVES):
            temporary_destination = temporary_root / str(index)
            temporary_destination.mkdir()
            files = archive_files(config)
            with zipfile.ZipFile(config["source"]) as archive:
                for entry, relative in files:
                    target = temporary_destination / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with archive.open(entry) as source, target.open("xb") as output:
                        shutil.copyfileobj(source, output)
                    if target.stat().st_size != entry.file_size:
                        raise RuntimeError(f"Extracted size mismatch: {entry.filename}")
            report[index]["verified_files"] = len(files)
        for index, config in enumerate(ARCHIVES):
            destination = config["destination"]
            destination.parent.mkdir(parents=True, exist_ok=True)
            if destination.exists():
                raise RuntimeError(f"Destination appeared during extraction: {destination}")
            os.replace(temporary_root / str(index), destination)
            report[index]["placed"] = True
    finally:
        # The generated temporary parent is empty after successful moves. Leave it
        # intact on failure so no partially extracted source is destroyed.
        if temporary_root.exists() and not any(temporary_root.iterdir()):
            temporary_root.rmdir()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--extract", action="store_true")
    args = parser.parse_args()
    report = preflight()
    if args.extract:
        extract(report)
        REPORT.parent.mkdir(parents=True, exist_ok=True)
        REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
