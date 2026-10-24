"""Hash-guarded August archive packing and reference migration, 2026-10-22."""
from __future__ import annotations

import argparse
import ast
from collections import Counter
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import sys
from urllib.parse import unquote
import zipfile


ROOT = Path(__file__).resolve().parents[3]
EVIDENCE = Path(__file__).resolve().parent
INVENTORY = ROOT / "Progress/Archive/8月归档清单.md"
PACKAGE = ROOT / "Progress/Archive/202608-归档.zip"
DATE = "2026-10-22"
spec = importlib.util.spec_from_file_location("progress_docs", ROOT / ".agents/skills/gulistrike-progress/scripts/progress_docs.py")
pd = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = pd
spec.loader.exec_module(pd)


def sha(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def normal(raw: bytes) -> str:
    return raw.decode("utf-8-sig").replace("\r\n", "\n")


def save_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    if INVENTORY.exists() or (EVIDENCE / "result.json").exists():
        raise SystemExit("Inventory/migration already exists; inspect results instead of rerunning")
    manifest = json.loads((EVIDENCE / "source-manifest.json").read_text(encoding="utf-8"))
    items = manifest["files"]
    source_paths = {ROOT / i["path"]: i for i in items}
    inventory_id = "ARC-20261022-001"
    if any(d.metadata.get("id") == inventory_id for d in pd.load_documents(ROOT)):
        raise SystemExit("Inventory ID has been allocated by another document")
    with zipfile.ZipFile(PACKAGE) as package:
        if package.testzip() is not None:
            raise SystemExit("ZIP CRC verification failed")
        if json.loads(package.read("manifest.json").decode("utf-8"))["files"] != items:
            raise SystemExit("ZIP source manifest differs")
        if set(package.namelist()) != {i["path"] for i in items} | {"manifest.json"}:
            raise SystemExit("Unexpected/missing ZIP members")
        for path, item in source_paths.items():
            path.resolve().relative_to((ROOT / "Progress/Archive").resolve())
            if path.suffix != ".md" or sha(path.read_bytes()) != item["sha256"]:
                raise SystemExit(f"Source changed after snapshot: {item['path']}")
            if sha(package.read(item["path"])) != item["sha256"]:
                raise SystemExit(f"ZIP member mismatch: {item['path']}")
    package_hash = sha(PACKAGE.read_bytes())
    for original in manifest["existing_packages"]:
        if sha((ROOT / original["path"]).read_bytes()) != original["sha256"]:
            raise SystemExit("Existing September/recent archive package changed")

    metadata = {
        "schema": pd.SCHEMA, "id": inventory_id, "work_id": "", "kind": "archive", "role": "root",
        "title": "8月归档清单", "areas": ["project"], "categories": [], "status": "recorded",
        "verification": "not_applicable", "created": DATE, "updated": DATE,
        "summary": f"2026年8月{len(items)}篇归档按原路径转存ZIP；本页保留原ID、文件、状态、字节数及SHA-256定位清单。",
        "next_action": "需要原始细节时将ZIP解压到临时目录，按清单或manifest核对原文。",
        "relations": {"work_items": []},
        "status_note": "用户要求沿用9月归档方式处理8月。仅改变文档存储形式，原文及元数据按字节保留，不提升原有玩法或表现验收状态。",
        "archive_mode": "inventory", "source_count": len(items), "source_period": "2026-08-01..2026-08-31",
        "source_archive": rel(PACKAGE), "source_archive_sha256": package_hash,
        "legacy_ids": [i["metadata"]["id"] for i in items],
    }
    by_day = Counter(str(i["metadata"]["created"]) for i in items)
    lines = ["# 8月归档清单", "", f"范围：2026年8月，按原归档`created`日期归集，与文件名月份核对一致。共 **{len(items)}篇MD**，原文 **{sum(i['bytes'] for i in items):,}字节**。", "",
             f"- [下载/打开8月归档ZIP]({PACKAGE.name})：{PACKAGE.stat().st_size:,}字节，包含{len(items)}篇原文及一个`manifest.json`。",
             f"- ZIP SHA-256：`{package_hash}`。",
             "- ZIP内路径统一为`Progress/Archive/<下表原文件名>`，正文和原始front matter保持原字节；下表状态均为历史记录。",
             "- 每个原ID有独立锚点，现存Markdown入链已迁移至对应行；原章节锚点及附件引用保留在ZIP原文。",
             "- 解压到临时目录即可阅读，按本清单/ZIP内manifest核对SHA-256；不要直接覆盖当前Progress。需要跨月原文时按各月清单提取相应文件。", "", "## 按日统计", "", "| 日期 | 篇数 |", "|---|---:|"]
    lines.extend(f"| {day} | {count} |" for day, count in sorted(by_day.items()))
    lines.extend(["", "旧维护脚本`Scripts/CommanderLOD/repair_document_erratum.py`按散文件路径修补原文；如需复用，先提取所需原文副本，不能对本清单运行该修补。", "",
                  "本次仅做文档压缩、引用迁移和索引维护，不编译或运行游戏。逐文件来源、链接迁移前备份及检查结果保存在[整理证据](../Evidence/20261022-AugustArchive/source-manifest.json)及同目录的`result.json`、`verification.json`。", ""])
    for day in sorted(by_day):
        lines.extend([f"## {day}", "", "| 原ID | 原文件名 | 原状态 / 验证 | 字节数 | SHA-256 |", "|---|---|---|---:|---|"])
        for item in (i for i in items if str(i["metadata"]["created"]) == day):
            m = item["metadata"]
            name = Path(item["path"]).name.replace("|", "\\|")
            lines.append(f'| <a id="{m["id"].lower()}"></a>`{m["id"]}` | `{name}` | `{m["status"]} / {m["verification"]}` | {item["bytes"]} | `{item["sha256"]}` |')
        lines.append("")

    before: dict[Path, bytes] = {}
    planned: dict[Path, bytes] = {INVENTORY: pd.render_document(metadata, "\n".join(lines)).encode("utf-8")}
    replacements = []
    source_anchors = {path.resolve(): item["metadata"]["id"].lower() for path, item in source_paths.items()}

    def stage(path: Path, data: bytes) -> None:
        if path not in before and path.exists():
            before[path] = path.read_bytes()
        planned[path] = data

    pattern = re.compile(r"(!?\[[^\]]*\]\()([^)]+)(\))")

    def rewrite(text: str, path: Path) -> str:
        def replace(match: re.Match) -> str:
            target, suffix, wrapped = pd.split_destination(match.group(2))
            filename = unquote(target.partition("#")[0])
            if not filename or re.match(r"^(?:https?://|mailto:|codex:)", filename, re.I):
                return match.group(0)
            old_path = Path(filename) if re.match(r"^[A-Za-z]:[/\\]", filename) else path.parent / filename
            anchor = source_anchors.get(old_path.resolve())
            if not anchor:
                return match.group(0)
            new = os.path.relpath(INVENTORY, path.parent).replace("\\", "/") + "#" + anchor
            if wrapped or " " in new:
                new = "<" + new + ">"
            replacements.append({"path": rel(path), "from": target, "to": new})
            return match.group(1) + new + suffix + match.group(3)
        output, fence = [], None
        for line in text.splitlines(keepends=True):
            stripped = line.lstrip()
            if stripped.startswith(("```", "~~~")):
                mark = stripped[:3]
                fence = None if fence == mark else mark if fence is None else fence
                output.append(line)
            elif fence:
                output.append(line)
            else:
                parts = re.split(r"(`[^`]*`)", line)
                output.append("".join(part if n % 2 else pattern.sub(replace, part) for n, part in enumerate(parts)))
        return "".join(output)

    for path in sorted((ROOT / "Progress").rglob("*.md")):
        if "_Index" in path.parts or path in source_paths:
            continue
        raw = path.read_bytes()
        updated = rewrite(raw.decode("utf-8"), path).encode("utf-8")
        if updated != raw:
            stage(path, updated)

    helper = ROOT / "Scripts/import_data_to_engine.py"
    helper_text = helper.read_bytes().decode("utf-8")
    for path, anchor in source_anchors.items():
        helper_text = helper_text.replace(rel(path), rel(INVENTORY) + "#" + anchor)
    if helper_text.encode("utf-8") != helper.read_bytes():
        ast.parse(helper_text)
        stage(helper, helper_text.encode("utf-8"))

    split_changes = []
    for split in pd.SPLIT_SPECS:
        baseline = pd.verify_split(ROOT, split)
        if baseline["errors"]:
            raise SystemExit(f"Split contract changed before migration: {split.relative_path}")
        parent = ROOT / split.relative_path
        old_meta, _ = pd.split_front_matter(pd.read_text(parent))
        segments = []
        changed_children = []
        for group in split.groups:
            child = parent.with_suffix("") / group.filename
            old_child_meta, _ = pd.split_front_matter(pd.read_text(child))
            text = normal(planned.get(child, child.read_bytes()))
            _, body = pd.split_front_matter(text)
            segment = body.split(pd.SPLIT_CONTENT_MARKER, 1)[1]
            new_hash = pd.normalized_payload_hash(segment, child)
            segments.append(pd.transform_markdown_links(segment, child))
            if new_hash != old_child_meta["split_segment_sha256"]:
                text = text.replace(old_child_meta["split_segment_sha256"], new_hash, 1)
                text = re.sub(r"(?m)^split_contract_updated:.*$", f"split_contract_updated: '{DATE}'", text)
                stage(child, text.encode("utf-8"))
                changed_children.append({"path": rel(child), "before": old_child_meta["split_segment_sha256"], "after": new_hash})
        if changed_children:
            current_hash = sha("".join(segments).encode("utf-8"))
            text = normal(planned.get(parent, parent.read_bytes())).replace(old_meta["split_payload_sha256"], current_hash)
            text = re.sub(r"(?m)^split_contract_updated:.*$", f"split_contract_updated: '{DATE}'", text)
            target = os.path.relpath(INVENTORY, parent.parent).replace("\\", "/")
            text = text.rstrip() + f"\n\n> 2026-10-22：[8月归档清单]({target})替代原散文件链接；仅迁移入链并更新当前哈希，旧合同和原文保存在Progress/Evidence/20261022-AugustArchive。\n"
            stage(parent, text.encode("utf-8"))
            split_changes.append({"path": rel(parent), "before": old_meta["split_payload_sha256"], "after": current_hash, "children": changed_children})

    readme = ROOT / "Progress/README.md"
    text = readme.read_bytes().decode("utf-8")
    needle = "- 9月历史：[归档清单](Archive/9月归档清单.md) · [原文ZIP](Archive/202609-归档.zip)。"
    if text.count(needle) != 1:
        raise SystemExit("README monthly entry changed; reread before merging")
    text = text.replace(needle, needle + "\n- 8月历史：[归档清单](Archive/8月归档清单.md) · [原文ZIP](Archive/202608-归档.zip)。")
    stage(readme, text.encode("utf-8"))

    result = {"date": DATE, "month": "2026-08", "documents": len(items), "source_bytes": sum(i["bytes"] for i in items),
              "package": {"path": rel(PACKAGE), "bytes": PACKAGE.stat().st_size, "sha256": package_hash, "per_source_hashes": "passed"},
              "inventory": {"path": rel(INVENTORY), "id": inventory_id, "bytes": len(planned[INVENTORY])},
              "redirects": {item["path"]: rel(INVENTORY) + "#" + item["metadata"]["id"].lower() for item in items},
              "link_replacements": replacements, "split_changes": split_changes,
              "files": [{"path": rel(path), "before_sha256": sha(before[path]) if path in before else None, "after_sha256": sha(raw)} for path, raw in sorted(planned.items())]}
    save_json(EVIDENCE / "plan.json", result)
    print(json.dumps({"mode": "apply" if args.apply else "dry-run", "documents": len(items), "zip_bytes": PACKAGE.stat().st_size,
                      "inventory_bytes": len(planned[INVENTORY]), "link_changes": len(replacements), "split_groups": len(split_changes), "files_to_write": len(planned)}, ensure_ascii=False, indent=2))
    if not args.apply:
        return
    for path, raw in before.items():
        if path.read_bytes() != raw:
            raise SystemExit(f"Reference changed while planning: {rel(path)}")
    for path, item in source_paths.items():
        if sha(path.read_bytes()) != item["sha256"]:
            raise SystemExit(f"Source changed while planning: {item['path']}")
    backup = EVIDENCE / "link-migration-before.zip"
    with zipfile.ZipFile(backup, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path, raw in sorted(before.items()):
            archive.writestr(rel(path), raw)
    with zipfile.ZipFile(backup) as archive:
        if archive.testzip() is not None or any(archive.read(rel(path)) != raw for path, raw in before.items()):
            raise SystemExit("Reference backup verification failed")
    for path, raw in sorted(planned.items()):
        temporary = path.with_name(path.name + ".august-archive.tmp")
        if temporary.exists():
            raise SystemExit(f"Unexpected temporary file: {temporary}")
        temporary.write_bytes(raw)
        os.replace(temporary, path)
        if path.read_bytes() != raw:
            raise SystemExit(f"Write verification failed: {rel(path)}")
    with zipfile.ZipFile(PACKAGE) as archive:
        for path, item in sorted(source_paths.items()):
            path.resolve().relative_to((ROOT / "Progress/Archive").resolve())
            if sha(path.read_bytes()) != item["sha256"] or sha(archive.read(item["path"])) != item["sha256"]:
                raise SystemExit(f"Source/ZIP changed before relocation: {item['path']}")
            path.unlink()
            if path.exists():
                raise SystemExit(f"Loose source still present: {item['path']}")
    result.update({"applied": True, "loose_originals_transferred_to_verified_zip": len(items), "reference_backup": rel(backup), "reference_backup_sha256": sha(backup.read_bytes())})
    save_json(EVIDENCE / "result.json", result)
    print("Applied; all original August documents remain byte-identical in the verified ZIP.")


if __name__ == "__main__":
    main()
