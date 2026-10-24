"""Extract the supplied original package and migrate Content without rewriting it."""
from pathlib import Path
import collections
import hashlib
import json
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(r"D:\BaiduNetdiskDownload\UE4UE5可操控第三人称射击机甲机器人带特效MechaDestroyer.zip")
ART = ROOT / "ArtSource/Characters/GroundMech_MechaDestroyer_20261023"
STAGE = ART / "OriginalPackage"

def main():
    destination = ROOT / "Content/MechaDestroyer"
    if destination.exists() or STAGE.exists():
        raise RuntimeError("Destination already exists; inspect before retrying migration")
    ART.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(SOURCE) as archive:
        entries = archive.infolist()
        for entry in entries:
            target = (STAGE / entry.filename).resolve()
            if not target.is_relative_to(STAGE.resolve()):
                raise RuntimeError(f"Unsafe archive path: {entry.filename}")
        archive.extractall(STAGE)
    shutil.copytree(STAGE / "Content/MechaDestroyer", destination)
    report = {
        "source": str(SOURCE), "source_sha256": hashlib.file_digest(SOURCE.open("rb"), "sha256").hexdigest(),
        "original_package": str(STAGE), "content_destination": str(destination),
        "package_root": "/Game/MechaDestroyer", "unpacked_bytes": sum(e.file_size for e in entries),
        "entries": [{"path": e.filename, "bytes": e.file_size, "crc32": f"{e.CRC:08x}"} for e in entries],
        "folder_counts": dict(collections.Counter("/".join(e.filename.split("/")[:3]) for e in entries if not e.is_dir())),
        "config_migrated": False, "project_file_migrated": False,
        "note": "Original resource paths retained. Gameplay configuration remains GuLiStrike-owned."
    }
    (ART / "package_manifest.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    for entry in entries:
        if not entry.is_dir():
            assert (STAGE / entry.filename).stat().st_size == entry.file_size
            if entry.filename.startswith("Content/MechaDestroyer/"):
                assert (ROOT / entry.filename).stat().st_size == entry.file_size
    print(json.dumps({"success": True, "archive_entries": len(entries), "asset_files": sum(e.filename.endswith((".uasset", ".umap")) for e in entries), "destination": str(destination)}, ensure_ascii=False))

if __name__ == "__main__":
    main()
