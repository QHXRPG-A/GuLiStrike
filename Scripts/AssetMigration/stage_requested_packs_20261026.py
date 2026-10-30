"""Inventory and copy only the four requested UE packs, without overwrites.

Only ZIP metadata is inspected. Binary package payloads are transferred verbatim
and are subsequently interpreted by the live Unreal Editor, never by this script.
No directory under Content/Assets is traversed.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import zipfile


PROJECT = Path(__file__).resolve().parents[2]
REPORT_DIR = PROJECT / "data/ExternalAssets/migrations/20261026"
DOWNLOADS = Path("D:/BaiduNetdiskDownload")
ALLOWED_EXTENSIONS = {".uasset", ".umap", ".uexp", ".ubulk", ".uptnl"}
PACKS = [
    {
        "id": "drone-bot",
        "name": "Drone Bot",
        "category": "Models/Robots",
        "source": DOWNLOADS / "Drone Bot/Drone Bot 5.3/DroneBotContent.zip",
        "prefix": "DroneBotContent/",
        "source_root": "/Game/DroneBotContent",
        "target_root": "/Game/Assets/Models/Robots/DroneBotContent",
        "expected_packages": 58,
        "version_note": "采用所提供的最高版本目录5.3；4.27、5.03、5.1、5.2不重复迁入。",
    },
    {
        "id": "advanced-dash-vfx",
        "name": "Advanced Dash VFX Pack",
        "category": "VFX/Movement",
        "source": DOWNLOADS / "UE5虚幻4 Advanced Dash VFX Pack 高级冲刺闪现视觉特效合集/【5.0-5.3】Advanced Dash VFX Pack.zip",
        "prefix": "AdvancedDashVFX/",
        "source_root": "/Game/AdvancedDashVFX",
        "target_root": "/Game/Assets/VFX/Movement/AdvancedDashVFX",
        "expected_packages": 186,
        "duplicate": DOWNLOADS / "UE5虚幻4 Advanced Dash VFX Pack 高级冲刺闪现视觉特效合集/【5.0-5.3】Advanced Dash VFX Pack(1).zip",
        "version_note": "原包标注5.0–5.3；同名(1)副本核对SHA256后去重。",
    },
    {
        "id": "muzzle-flashes",
        "name": "Muzzle Flashes",
        "category": "VFX/Weapons",
        "source": DOWNLOADS / "a510_枪火特效/Muzzle Flashes.zip",
        "prefix": "Muzzle Flashes/muzzles/",
        "source_root": "/Game/muzzles",
        "target_root": "/Game/Assets/VFX/Weapons/MuzzleFlashes",
        "expected_packages": 241,
        "version_note": "归档未提供uproject或版本配置；类型与引用由UE5.7编辑器核对。",
    },
    {
        "id": "mech-constructor-spiders-tanks",
        "name": "Mech Constructor Spiders and Tanks",
        "category": "Models/Mechs",
        "source": DOWNLOADS / "Mech Constructor Spiders and Tanks.zip",
        "prefix": "Mech Constructor Spiders and Tanks/Content/Mech_Constructor_Spiders/",
        "source_root": "/Game/Mech_Constructor_Spiders",
        "target_root": "/Game/Assets/Models/Mechs/Mech_Constructor_Spiders",
        "expected_packages": 322,
        "version_note": "完整保留Animations、Blueprints、网格、材质与演示内容；未提供uproject。",
    },
]


def write_report(name: str, report: object) -> None:
    REPORT_DIR.mkdir(parents=True, exist_ok=True)
    (REPORT_DIR / name).write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def content_path(root: str) -> Path:
    if not root.startswith("/Game/"):
        raise ValueError(root)
    path = (PROJECT / "Content" / root.removeprefix("/Game/")).resolve()
    if not path.is_relative_to((PROJECT / "Content").resolve()):
        raise ValueError(f"Path escapes Content: {path}")
    return path


def inspect_pack(config: dict) -> dict:
    source = config["source"]
    stat_before = source.stat()
    report = {k: str(v) if isinstance(v, Path) else v for k, v in config.items()}
    report.update(
        source_bytes=stat_before.st_size,
        source_mtime_ns=stat_before.st_mtime_ns,
        source_sha256=sha256(source),
        entries=[],
        omitted_archive_entries=[],
    )
    seen = set()
    with zipfile.ZipFile(source) as archive:
        for info in archive.infolist():
            if info.is_dir():
                continue
            name = info.filename.replace("\\", "/")
            if not name.startswith(config["prefix"]):
                report["omitted_archive_entries"].append(name)
                continue
            relative = PurePosixPath(name[len(config["prefix"]):])
            if (
                not relative.parts
                or relative.is_absolute()
                or any(part in ("", ".", "..") or ":" in part for part in relative.parts)
                or relative.suffix.lower() not in ALLOWED_EXTENSIONS
                or (info.external_attr >> 16) & 0o170000 == 0o120000
            ):
                raise RuntimeError(f"Unsafe or unexpected member: {name}")
            key = str(relative).casefold()
            if key in seen:
                raise RuntimeError(f"Windows filename collision: {name}")
            seen.add(key)
            package_suffix = str(relative.with_suffix(""))
            report["entries"].append({
                "archive_entry": name,
                "relative_file": str(relative),
                "bytes": info.file_size,
                "zip_crc32": f"{info.CRC:08x}",
                "source_package": config["source_root"] + "/" + package_suffix,
                "target_package": config["target_root"] + "/" + package_suffix,
            })
    packages = [e for e in report["entries"] if PurePosixPath(e["relative_file"]).suffix.lower() in {".uasset", ".umap"}]
    if len(packages) != config["expected_packages"]:
        raise RuntimeError(f"Unexpected package count: {config['id']}: {len(packages)}")
    report["file_count"] = len(report["entries"])
    report["package_count"] = len(packages)
    report["uncompressed_bytes"] = sum(e["bytes"] for e in report["entries"])
    report["folder_counts"] = dict(sorted(Counter(str(PurePosixPath(e["relative_file"]).parent) for e in report["entries"]).items()))
    report["maps"] = [e["target_package"] for e in packages if e["relative_file"].endswith(".umap")]
    report["external_actor_object_files"] = [e["relative_file"] for e in report["entries"] if any(p in {"__ExternalActors__", "__ExternalObjects__"} for p in PurePosixPath(e["relative_file"]).parts)]
    report["archive_config_entries"] = []
    if "duplicate" in config:
        duplicate = config["duplicate"]
        report["duplicate_sha256"] = sha256(duplicate)
        report["duplicate_is_identical"] = report["duplicate_sha256"] == report["source_sha256"]
        if not report["duplicate_is_identical"]:
            raise RuntimeError("Dash archive copies differ; inspect before deduplicating")
    if source.stat().st_mtime_ns != stat_before.st_mtime_ns or source.stat().st_size != stat_before.st_size:
        raise RuntimeError(f"Source archive changed during inventory: {source}")
    for root in (config["source_root"], config["target_root"]):
        if content_path(root).exists():
            raise RuntimeError(f"Destination exists; refusing overwrite: {content_path(root)}")
    return report


def extract_pack(report: dict) -> None:
    source = Path(report["source"])
    if source.stat().st_size != report["source_bytes"] or source.stat().st_mtime_ns != report["source_mtime_ns"]:
        raise RuntimeError(f"Source archive changed: {source}")
    destination = content_path(report["source_root"])
    destination.mkdir(parents=True, exist_ok=False)
    copied = 0
    with zipfile.ZipFile(source) as archive:
        for entry in report["entries"]:
            target = destination / entry["relative_file"]
            if not target.resolve().is_relative_to(destination):
                raise RuntimeError(f"Extracted path escapes destination: {target}")
            target.parent.mkdir(parents=True, exist_ok=True)
            # Reading to EOF also lets zipfile verify the archive member CRC.
            with archive.open(entry["archive_entry"]) as incoming, target.open("xb") as outgoing:
                shutil.copyfileobj(incoming, outgoing, 1024 * 1024)
            if target.stat().st_size != entry["bytes"]:
                raise RuntimeError(f"Extracted size mismatch: {target}")
            copied += 1
    report["copied_files"] = copied
    report["zip_crc_verified_during_copy"] = True
    report["staged"] = True


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--extract", action="store_true")
    args = parser.parse_args()
    reports = [inspect_pack(config) for config in PACKS]
    write_report("preflight.json", reports)
    if args.extract:
        for report in reports:
            extract_pack(report)
            write_report("extraction_report.json", reports)
            print(f"STAGED {report['id']}: {report['file_count']} files", flush=True)
    print(json.dumps([{
        k: r[k] for k in ("id", "source_root", "target_root", "package_count", "uncompressed_bytes", "maps", "folder_counts", "source_sha256", "version_note")
    } for r in reports], ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
