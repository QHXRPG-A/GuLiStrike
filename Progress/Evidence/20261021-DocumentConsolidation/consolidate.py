"""One-off, source-hash-guarded Progress consolidation for 2026-10-21."""
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

import yaml


ROOT = Path(__file__).resolve().parents[3]
EVIDENCE = Path(__file__).resolve().parent
DATE = "2026-10-21"
TARGETS = {
    "requirement": ROOT / "Progress/RequirementDocument/20261021-近期性能优化.md",
    "development": ROOT / "Progress/DevelopmentDocumentation/20261021-近期性能优化.md",
    "archive": ROOT / "Progress/Archive/20261021-近期性能优化整合归档.md",
}
INVENTORY = ROOT / "Progress/Archive/9月归档清单.md"
ZIPS = {
    "recent": ROOT / "Progress/Archive/20261021-近期优化原文.zip",
    "september": ROOT / "Progress/Archive/202609-归档.zip",
}
spec = importlib.util.spec_from_file_location(
    "progress_docs", ROOT / ".agents/skills/gulistrike-progress/scripts/progress_docs.py"
)
pd = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = pd
spec.loader.exec_module(pd)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def normalized(data: bytes) -> str:
    return data.decode("utf-8-sig").replace("\r\n", "\n")


def dump_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def cell(value: object) -> str:
    return str(value).replace("\n", " ").replace("|", "\\|")


def source_anchor(item: dict) -> str:
    return str(item["metadata"]["id"]).lower()


def source_register(items: list[dict], kind: str) -> str:
    lines = ["## 原文与稳定ID索引", "", "原文及原始元数据见ZIP；下表状态为合并前记录，当前执行要求以前文为准。", ""]
    if kind == "archive":
        lines.extend(["| 日期 / 原ID | 原文件（ZIP内Archive目录） | 当时记录的摘要 |", "|---|---|---|"])
    else:
        lines.extend(["| 原ID / 工作项 | 原文件（ZIP内同类目录） | 合并前状态 / 验证 |", "|---|---|---|"])
    for item in sorted(items, key=lambda value: (str(value["metadata"].get("created", "")), value["metadata"]["id"])):
        meta = item["metadata"]
        anchor = f'<a id="{source_anchor(item)}"></a>'
        name = Path(item["path"]).relative_to("Progress/" + {"requirement": "RequirementDocument", "development": "DevelopmentDocumentation", "archive": "Archive"}[kind]).as_posix()
        if kind == "archive":
            lines.append(f"| {anchor}{meta['created']} / `{meta['id']}` | `{cell(name)}` | {cell(meta.get('summary', ''))} |")
        else:
            lines.append(f"| {anchor}`{meta['id']}` / `{meta.get('work_id', '')}` | `{cell(name)}` | `{meta['status']} / {meta['verification']}` |")
    return "\n".join(lines) + "\n"


def make_inventory(items: list[dict]) -> bytes:
    zip_path = ZIPS["september"]
    meta = {
        "schema": pd.SCHEMA, "id": "ARC-20261021-020", "work_id": "", "kind": "archive", "role": "root",
        "title": "9月归档清单", "areas": ["project"], "categories": [], "status": "recorded",
        "verification": "not_applicable", "created": DATE, "updated": DATE,
        "summary": f"2026年9月{len(items)}篇归档已按原路径转存ZIP；本页提供原ID、文件、状态、字节数和SHA-256定位清单。",
        "next_action": "需要历史细节时解压ZIP到临时目录，按清单核对原文；不将文档完整性检查当作玩法验收。",
        "relations": {"work_items": []},
        "status_note": "仅改变历史文档存储形式；原始正文和元数据按字节保留，源归档的验证状态不提升。",
        "archive_mode": "inventory", "source_count": len(items), "source_period": "2026-09-01..2026-09-30",
        "source_archive": relative(zip_path), "source_archive_sha256": digest(zip_path.read_bytes()),
        "legacy_ids": [item["metadata"]["id"] for item in items],
    }
    by_day = Counter(str(item["metadata"]["created"]) for item in items)
    lines = ["# 9月归档清单", "", f"范围：2026年9月，按原归档`created`日期归集，与文件名月份核对一致。共 **{len(items)}篇MD**，原文 **{sum(i['bytes'] for i in items):,}字节**。", "",
             f"- [下载/打开9月归档ZIP]({zip_path.name})：{zip_path.stat().st_size:,}字节；250篇原文及一个`manifest.json`。",
             f"- ZIP SHA-256：`{meta['source_archive_sha256']}`。",
             "- ZIP内路径统一为`Progress/Archive/<下表原文件名>`，每篇正文和原始front matter保持原字节；清单中的验证状态是历史状态。",
             "- 每个原ID有独立锚点，原文档入链已迁移至对应行。原章节锚点和附件引用保留在ZIP原文中。",
             "- 解压到临时目录即可阅读；需核对时比较解压文件SHA-256与下表/ZIP内manifest。不要直接解压覆盖当前Progress。", "", "## 按日统计", "", "| 日期 | 篇数 |", "|---|---:|"]
    lines.extend(f"| {day} | {count} |" for day, count in sorted(by_day.items()))
    lines.extend(["", "历史维护脚本`Scripts/CommanderLOD/repair_document_erratum.py`按旧散文件路径工作；如需复用，先从ZIP提取相应原文副本，不能对本清单运行原文修补。", ""])
    for day in sorted(by_day):
        lines.extend([f"## {day}", "", "| 原ID | 原文件名 | 原状态 / 验证 | 字节数 | SHA-256 |", "|---|---|---|---:|---|"])
        for item in sorted((i for i in items if str(i["metadata"]["created"]) == day), key=lambda value: value["path"]):
            m = item["metadata"]
            lines.append(f'| <a id="{source_anchor(item)}"></a>`{m["id"]}` | `{cell(Path(item["path"]).name)}` | `{m["status"]} / {m["verification"]}` | {item["bytes"]} | `{item["sha256"]}` |')
        lines.append("")
    return pd.render_document(meta, "\n".join(lines)).encode("utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    if (EVIDENCE / "result.json").exists():
        raise SystemExit("Migration already applied; inspect result.json instead of rerunning")
    manifest = json.loads((EVIDENCE / "source-manifest.json").read_text(encoding="utf-8"))
    sources = {ROOT / item["path"]: item for items in manifest["sets"].values() for item in items}
    before: dict[Path, bytes] = {}
    planned: dict[Path, bytes] = {}
    replacements: list[dict] = []
    redirects = {}
    zip_verification = {}
    for group, items in manifest["sets"].items():
        target_zip = ZIPS[group]
        with zipfile.ZipFile(target_zip) as archive:
            if archive.testzip() is not None:
                raise SystemExit(f"Corrupt ZIP: {target_zip}")
            stored = json.loads(archive.read("manifest.json").decode("utf-8"))["files"]
            if stored != items:
                raise SystemExit("ZIP manifest mismatch")
            expected_names = {item["path"] for item in items} | {"manifest.json"}
            if set(archive.namelist()) != expected_names:
                raise SystemExit("Unexpected or missing ZIP member")
            for item in items:
                path = ROOT / item["path"]
                path.resolve().relative_to((ROOT / "Progress").resolve())
                if path.suffix != ".md" or digest(path.read_bytes()) != item["sha256"]:
                    raise SystemExit(f"Source changed since snapshot: {item['path']}")
                if digest(archive.read(item["path"])) != item["sha256"]:
                    raise SystemExit(f"ZIP contents differ: {item['path']}")
                destination = INVENTORY if group == "september" else TARGETS[item["metadata"]["kind"]]
                redirects[path.resolve()] = (destination, source_anchor(item))
            zip_verification[group] = {"path": relative(target_zip), "documents": len(items), "bytes": target_zip.stat().st_size,
                                       "sha256": digest(target_zip.read_bytes()), "crc": "passed", "all_source_sha256": "passed"}

    def stage(path: Path, data: bytes) -> None:
        if path not in before and path.exists():
            before[path] = path.read_bytes()
        planned[path] = data

    recent = manifest["sets"]["recent"]
    requirements = [i for i in recent if i["metadata"]["kind"] == "requirement"]
    developments = {i["metadata"]["work_id"]: i["metadata"] for i in recent if i["metadata"]["kind"] == "development" and i["metadata"]["role"] == "root"}
    work_items = [{"work_id": i["metadata"]["work_id"], "requirement_id": i["metadata"]["id"],
                   "development_id": developments[i["metadata"]["work_id"]]["id"],
                   "requirement_status": i["metadata"]["status"],
                   "development_status": developments[i["metadata"]["work_id"]]["status"],
                   "verification": developments[i["metadata"]["work_id"]]["verification"]} for i in requirements]
    for kind, path in TARGETS.items():
        meta, body = pd.split_front_matter(pd.read_text(path))
        items = [i for i in recent if i["metadata"]["kind"] == kind]
        meta.update({"source_count": len(items), "source_period": manifest["recent_scope"],
                     "source_archive": relative(ZIPS["recent"]), "source_archive_sha256": zip_verification["recent"]["sha256"],
                     "legacy_ids": [i["metadata"]["id"] for i in items if i["metadata"]["id"] != meta["id"]]})
        if kind != "archive":
            meta["legacy_work_ids"] = [i["work_id"] for i in work_items if i["work_id"] != meta["work_id"]]
            meta["consolidated_work_items"] = work_items
        else:
            meta["relations"]["work_items"] = sorted(set(meta["relations"]["work_items"]) | {wid for i in items for wid in i["metadata"].get("relations", {}).get("work_items", [])})
        marker = "<!-- consolidation:archive-register -->" if kind == "archive" else "<!-- consolidation:source-register -->"
        if body.count(marker) != 1:
            raise SystemExit(f"Missing/duplicate source register marker: {path}")
        stage(path, pd.render_document(meta, body.replace(marker, source_register(items, kind).rstrip())).encode("utf-8"))
    if INVENTORY.exists():
        raise SystemExit("Refuse to overwrite an existing inventory")
    stage(INVENTORY, make_inventory(manifest["sets"]["september"]))

    link_re = re.compile(r"(!?\[([^\]]*)\]\()([^)]+)(\))")
    missing_reports = {"D:/UE5.7/PerformanceCaptures/20261017-Hotspots-v1/report.md", "D:/UE5.7/PerformanceCaptures/20261018-VisualRegression-v1/report.md"}

    def rewrite(text: str, path: Path) -> str:
        def replace(match: re.Match) -> str:
            target, suffix, wrapped = pd.split_destination(match.group(3))
            if target in missing_reports:
                value = f"{match.group(2)}（原报告已缺失；历史路径：`{target}`）"
                replacements.append({"path": relative(path), "from": target, "to": "missing report path text", "kind": "existing_broken_link"})
                return value
            path_part = unquote(target.partition("#")[0])
            if not path_part or re.match(r"^(?:https?://|mailto:|codex:)", path_part, re.I):
                return match.group(0)
            old = Path(path_part) if re.match(r"^[A-Za-z]:[/\\]", path_part) else path.parent / path_part
            found = redirects.get(old.resolve())
            if not found:
                return match.group(0)
            dest, anchor = found
            new = os.path.relpath(dest, path.parent).replace("\\", "/") + "#" + anchor
            if wrapped or " " in new:
                new = "<" + new + ">"
            replacements.append({"path": relative(path), "from": target, "to": new, "kind": "document_relocation"})
            return match.group(1) + new + suffix + match.group(4)
        lines = []
        fence = None
        for line in text.splitlines(keepends=True):
            stripped = line.lstrip()
            if stripped.startswith(("```", "~~~")):
                mark = stripped[:3]
                fence = None if fence == mark else mark if fence is None else fence
                lines.append(line)
            elif fence:
                lines.append(line)
            else:
                parts = re.split(r"(`[^`]*`)", line)
                lines.append("".join(part if index % 2 else link_re.sub(replace, part) for index, part in enumerate(parts)))
        return "".join(lines)

    for path in sorted(set((ROOT / "Progress").rglob("*.md")) | set(planned)):
        if "_Index" in path.parts or path in sources:
            continue
        data = planned.get(path, path.read_bytes() if path.exists() else b"")
        new = rewrite(data.decode("utf-8"), path).encode("utf-8")
        if new != data:
            stage(path, new)

    auxiliary_paths = [ROOT / ".agents/skills/guli-map-authoring/SKILL.md", ROOT / ".agents/skills/guli-map-authoring/references/architecture.md",
                       ROOT / ".agents/skills/guli-map-authoring/references/maintenance.md", ROOT / "Scripts/Performance/author_hybrid_cpu_review_entry.py",
                       ROOT / "Scripts/Performance/author_commander_combat_navigation_entry.py"]
    for path in auxiliary_paths:
        original = path.read_bytes()
        content = original.decode("utf-8")
        for source, (destination, anchor) in redirects.items():
            old = relative(source)
            if path.suffix == ".md":
                target = os.path.relpath(destination, path.parent).replace("\\", "/") + "#" + anchor
                content = content.replace("`" + old + "`", f"[{source.stem}]({target})")
            else:
                content = content.replace(old, relative(destination) + "#" + anchor)
        updated = content.encode("utf-8")
        if updated != original:
            if path.suffix == ".py":
                ast.parse(content)
            stage(path, updated)

    # Existing split contracts were already stale. Preserve both the old claimed
    # hash and the actual pre-migration payload before rebasing link-only changes.
    split_baseline = [pd.verify_split(ROOT, split) for split in pd.SPLIT_SPECS]
    split_changes = []
    for split in pd.SPLIT_SPECS:
        parent = ROOT / split.relative_path
        parent_before = pd.read_text(parent)
        parent_meta, _ = pd.split_front_matter(parent_before)
        normalized_before, normalized_after = [], []
        for group in split.groups:
            child = parent.with_suffix("") / group.filename
            old_text = pd.read_text(child)
            old_meta, old_body = pd.split_front_matter(old_text)
            old_segment = old_body.split(pd.SPLIT_CONTENT_MARKER, 1)[1]
            new_text = normalized(planned.get(child, child.read_bytes()))
            _, new_body = pd.split_front_matter(new_text)
            new_segment = new_body.split(pd.SPLIT_CONTENT_MARKER, 1)[1]
            old_actual = pd.normalized_payload_hash(old_segment, child)
            new_actual = pd.normalized_payload_hash(new_segment, child)
            normalized_before.append(pd.transform_markdown_links(old_segment, child))
            normalized_after.append(pd.transform_markdown_links(new_segment, child))
            if new_actual != old_meta["split_segment_sha256"]:
                new_text = new_text.replace(old_meta["split_segment_sha256"], new_actual, 1)
                additions = yaml.safe_dump({"split_legacy_segment_sha256": old_meta["split_segment_sha256"],
                                             "split_pre_consolidation_segment_sha256": old_actual,
                                             "split_contract_updated": DATE}, sort_keys=False)
                end = new_text.find("\n---\n", 4)
                new_text = new_text[:end] + "\n" + additions.rstrip() + new_text[end:]
                stage(child, new_text.encode("utf-8"))
        actual_before = digest("".join(normalized_before).encode("utf-8"))
        actual_after = digest("".join(normalized_after).encode("utf-8"))
        parent_data = normalized(planned.get(parent, parent.read_bytes()))
        parent_data = parent_data.replace(parent_meta["split_payload_sha256"], actual_after)
        additions = yaml.safe_dump({"split_legacy_payload_sha256": parent_meta["split_payload_sha256"],
                                     "split_pre_consolidation_payload_sha256": actual_before,
                                     "split_contract_updated": DATE}, sort_keys=False)
        end = parent_data.find("\n---\n", 4)
        parent_data = parent_data[:end] + "\n" + additions.rstrip() + parent_data[end:]
        inventory_link = os.path.relpath(INVENTORY, parent.parent).replace("\\", "/")
        parent_data = parent_data.rstrip() + f"\n\n> 2026-10-21文档整理：原合同与整理前现有正文已有差异；旧合同与当前正文哈希均已保留，原文备份见Progress/Evidence/20261021-DocumentConsolidation/link-migration-before.zip。本次只迁移至[9月归档清单]({inventory_link})等新入口并重建当前合同，不追认旧正文与最初拆分版本一致。\n"
        stage(parent, parent_data.encode("utf-8"))
        split_changes.append({"path": split.relative_path, "legacy_expected": parent_meta["split_payload_sha256"],
                              "actual_before": actual_before, "actual_after": actual_after})

    readme = ROOT / "Progress/README.md"
    readme_text = normalized(planned.get(readme, readme.read_bytes()))
    entry = "## 快速入口\n\n"
    new_entries = "- 近期优化（10月18–21日）：[需求](RequirementDocument/20261021-近期性能优化.md) · [开发](DevelopmentDocumentation/20261021-近期性能优化.md) · [归档](Archive/20261021-近期性能优化整合归档.md)。\n- 9月历史：[归档清单](Archive/9月归档清单.md) · [原文ZIP](Archive/202609-归档.zip)。\n"
    readme_text = readme_text.replace(entry, entry + new_entries, 1)
    readme_text = readme_text.replace("增量归档创建后原则上不可改写。", "增量归档创建后原则上不可改写。用户明确要求合并或压缩时，可在保存原文ZIP、校验SHA-256并迁移入链后转为压缩存储；合并不提升原有验收结论。")
    stage(readme, readme_text.encode("utf-8"))

    plan = {"date": DATE, "sources": len(sources), "source_groups": {g: len(v) for g, v in manifest["sets"].items()},
            "zip_verification": zip_verification, "source_redirects": {relative(p): relative(d) + "#" + a for p, (d, a) in redirects.items()},
            "link_replacements": replacements, "split_baseline": split_baseline, "split_changes": split_changes,
            "files": [{"path": relative(p), "before_sha256": digest(before[p]) if p in before else None,
                       "after_sha256": digest(data), "after_bytes": len(data)} for p, data in sorted(planned.items())]}
    dump_json(EVIDENCE / "plan.json", plan)
    print(json.dumps({"mode": "apply" if args.apply else "dry-run", "source_groups": plan["source_groups"],
                      "planned_files": len(planned), "markdown_link_changes": len(replacements),
                      "new_document_bytes": {relative(p): len(planned[p]) for p in [*TARGETS.values(), INVENTORY]},
                      "existing_split_contract_errors": sum(len(s["errors"]) for s in split_baseline)}, ensure_ascii=False, indent=2))
    if not args.apply:
        return

    for path, original in before.items():
        if path.read_bytes() != original:
            raise SystemExit(f"Changed while planning: {relative(path)}")
    for path, item in sources.items():
        if digest(path.read_bytes()) != item["sha256"]:
            raise SystemExit(f"Changed while planning: {relative(path)}")
    backup = EVIDENCE / "link-migration-before.zip"
    with zipfile.ZipFile(backup, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path, original in sorted(before.items()):
            archive.writestr(relative(path), original)
    with zipfile.ZipFile(backup) as archive:
        if archive.testzip() is not None or any(archive.read(relative(p)) != data for p, data in before.items()):
            raise SystemExit("Link migration backup verification failed")
    for path, data in sorted(planned.items()):
        temp = path.with_name(path.name + ".consolidation.tmp")
        if temp.exists():
            raise SystemExit(f"Unexpected temporary file: {temp}")
        temp.write_bytes(data)
        os.replace(temp, path)
        if path.read_bytes() != data:
            raise SystemExit(f"Write verification failed: {relative(path)}")
    relocated = []
    for path, item in sorted(sources.items()):
        path.resolve().relative_to((ROOT / "Progress").resolve())
        if digest(path.read_bytes()) != item["sha256"]:
            raise SystemExit(f"Source changed before ZIP relocation: {relative(path)}")
        group = "september" if item in manifest["sets"]["september"] else "recent"
        with zipfile.ZipFile(ZIPS[group]) as archive:
            if digest(archive.read(item["path"])) != item["sha256"]:
                raise SystemExit("Final ZIP verification failed")
        path.unlink()
        if path.exists():
            raise SystemExit("Loose source relocation failed")
        relocated.append(item["path"])
    plan.update({"applied": True, "relocated_sources": relocated, "link_backup": relative(backup),
                 "link_backup_sha256": digest(backup.read_bytes()), "only_documentation_and_document_references": True})
    dump_json(EVIDENCE / "result.json", plan)
    print(f"Applied: {len(relocated)} original documents retained in verified ZIPs; {len(planned)} files written.")


if __name__ == "__main__":
    main()
