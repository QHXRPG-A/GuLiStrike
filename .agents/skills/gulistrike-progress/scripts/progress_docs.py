#!/usr/bin/env python3
"""GuLiStrike Progress metadata, index, validation, and dashboard data tool."""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
import re
import sys
import tempfile
from collections import Counter, defaultdict
from dataclasses import dataclass
from datetime import date, datetime
from pathlib import Path
from typing import Any, Iterable
from urllib.parse import unquote

import yaml


SCHEMA = "guli-progress/v1"
CORE_DIRS = {
    "RequirementDocument": "requirement",
    "DevelopmentDocumentation": "development",
    "Archive": "archive",
    "Gameplay": "gameplay",
}
STATUS_VALUES = {
    "requirement": {"draft", "approved", "superseded", "cancelled"},
    "development": {"planned", "in_progress", "verification", "done", "abandoned"},
    "archive": {"recorded", "superseded"},
    "gameplay": {"current"},
    "backlog": {"current"},
    "reference": {"reference"},
}
VERIFICATION_VALUES = {"not_run", "partial", "passed", "failed", "not_applicable"}
CATEGORY_VALUES = {"art", "gameplay", "performance"}
CATEGORY_AREA_MAP = {
    "art": {"art", "assets", "vfx", "rendering", "presentation"},
    "gameplay": {
        "commander",
        "ship",
        "combat",
        "wingman",
        "building",
        "economy",
        "outpost",
        "movement",
        "ai",
        "navigation",
        "gameplay",
        "map",
        "map-authoring",
        "level",
        "resource",
        "resources",
    },
    "performance": {"performance"},
}
COMMON_FIELDS = (
    "schema",
    "id",
    "work_id",
    "kind",
    "role",
    "title",
    "areas",
    "status",
    "verification",
    "created",
    "updated",
    "summary",
    "next_action",
    "relations",
    "status_note",
)
AREA_RULES = (
    ("map-authoring", ("地图标注", "地图战略点", "markerauthoring", "gulimap")),
    ("building", ("建筑", "建造", "据点", "outpost")),
    ("wingman", ("僚机", "flightnav", "wingman")),
    ("commander", ("指挥官", "小兵", "soldier", "wm01", "mass")),
    ("ship", ("飞船", "ship", "combatavatarfly")),
    ("ui", (" ui", "ui ", "hud", "界面", "血条")),
    ("network", ("网络", "同步", "复制", "dedicated", "relay")),
    ("data-pipeline", ("数据管线", "excel", "datatable", "json")),
    ("vfx", ("特效", "vfx", "niagara", "爆炸", "法术场")),
    ("combat", ("战斗", "技能", "gas", "弹道", "扫射", "机枪", "导弹")),
    ("assets", ("资产", "模型", "骨骼", "动画", "socket", "rig")),
    ("learning", ("教材", "导读", "精读", "教程")),
    ("project", ("文档体系", "清理progress", "维护skill")),
)
GAMEPLAY_IDS = {
    "指挥官": "GAMEPLAY-COMMANDER",
    "飞船": "GAMEPLAY-SHIP",
    "战斗": "GAMEPLAY-COMBAT",
    "建筑": "GAMEPLAY-BUILDING",
}
DATE_IN_NAME = re.compile(r"^(\d{8})-")
MARKDOWN_LINK = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
CHECKBOX = re.compile(r"^\s*[-*]\s+\[([ xX])\]\s+", re.MULTILINE)
SPLIT_CONTENT_MARKER = "<!-- guli-progress:split-content -->\n"


@dataclass
class Document:
    path: Path
    relative_path: str
    metadata: dict[str, Any]
    body: str
    fallback: bool = False

    def public(self, include_body: bool = False) -> dict[str, Any]:
        result = dict(self.metadata)
        result["path"] = self.relative_path
        result["bytes"] = self.path.stat().st_size
        result["headings"] = extract_headings(self.body)
        checks = CHECKBOX.findall(self.body)
        result["tasks_total"] = len(checks)
        result["tasks_done"] = sum(1 for value in checks if value.lower() == "x")
        result["task_progress"] = round(result["tasks_done"] * 100 / len(checks)) if checks else None
        if include_body:
            result["body"] = self.body
        return result


@dataclass(frozen=True)
class SplitGroup:
    filename: str
    title: str
    start_heading: str | None


@dataclass(frozen=True)
class SplitSpec:
    relative_path: str
    groups: tuple[SplitGroup, ...]


SPLIT_SPECS = (
    SplitSpec(
        "Progress/DevelopmentDocumentation/20260902-僚机体系与空中三维导航.md",
        (
            SplitGroup("01-架构与配置.md", "架构与配置", None),
            SplitGroup("02-FlightNav与客户端模拟.md", "FlightNav 与客户端模拟", "## 3. 三维Flight Navigation"),
            SplitGroup("03-接纳租约与换主.md", "接纳、租约与换主", "## 5. Candidate、Acceptance与按包验证"),
            SplitGroup("04-战斗复制与表现.md", "战斗、复制与表现", "## 7. 战斗、死亡与表现"),
            SplitGroup("05-日志测试与Gate.md", "日志、测试与 Gate", "## 9. 日志、字段与开发命令"),
            SplitGroup("06-任务风险与结果.md", "任务、风险与结果", "## 13. 任务清单"),
        ),
    ),
    SplitSpec(
        "Progress/RequirementDocument/20260905-指挥官兵种技能与Roguelike升级归属.md",
        (
            SplitGroup("01-结论边界与实现基线.md", "结论、边界与实现基线", None),
            SplitGroup("02-技能身份与GAS归属.md", "技能身份与 GAS 归属", "## 4. 技能身份：定义、绑定和升级不要混成一个对象"),
            SplitGroup("03-多武器执行与冷却.md", "多武器执行与冷却", "## 6. 多武器执行与冷却范围"),
            SplitGroup("04-Roguelike升级与生命周期.md", "Roguelike 升级与生命周期", "## 7. Roguelike 修改规则"),
            SplitGroup("05-同步验收与关联.md", "同步、验收与关联", "## 10. 展示、同步与填表边界"),
        ),
    ),
    SplitSpec(
        "Progress/Gameplay/指挥官.md",
        (
            SplitGroup("01-战局选兵与移动.md", "战局选兵与移动", None),
            SplitGroup("02-UI表现与性能.md", "UI 表现与性能", "### 指挥官 UI 与战场反馈"),
            SplitGroup("03-数据技能与武器表现.md", "数据、技能与武器表现", "### Soldier 数据与运行时 GM"),
            SplitGroup("04-验证边界.md", "验证边界", "### 当前验证边界（相机稳定巡航）"),
            SplitGroup("05-数值与演进.md", "数值与演进", "## 数值速查"),
        ),
    ),
)


def project_root_from(start: str | None = None) -> Path:
    if start:
        candidate = Path(start).resolve()
        if (candidate / "Progress").is_dir():
            return candidate
        raise SystemExit(f"Project root has no Progress directory: {candidate}")
    for candidate in Path(__file__).resolve().parents:
        if (candidate / "Progress").is_dir():
            return candidate
    raise SystemExit("Could not locate the GuLiStrike project root")


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8-sig").replace("\r\n", "\n")


def atomic_write(path: Path, content: str) -> bool:
    content = content.replace("\r\n", "\n")
    if path.exists() and read_text(path) == content:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(content)
        os.replace(temp_name, path)
    finally:
        if os.path.exists(temp_name):
            os.unlink(temp_name)
    return True


def split_front_matter(text: str) -> tuple[dict[str, Any], str]:
    if not text.startswith("---\n"):
        return {}, text
    end = text.find("\n---\n", 4)
    if end < 0:
        return {}, text
    raw = text[4:end]
    parsed = yaml.safe_load(raw) or {}
    if not isinstance(parsed, dict):
        raise ValueError("front matter must be a mapping")
    return parsed, text[end + 5 :]


def render_document(metadata: dict[str, Any], body: str) -> str:
    ordered = {field: metadata.get(field, [] if field == "areas" else {} if field == "relations" else "") for field in COMMON_FIELDS}
    for key, value in metadata.items():
        if key not in ordered:
            ordered[key] = value
    dumped = yaml.safe_dump(
        ordered,
        allow_unicode=True,
        sort_keys=False,
        width=120,
        default_flow_style=False,
    ).rstrip()
    return f"---\n{dumped}\n---\n\n{body.lstrip()}"


def extract_title(body: str, fallback: str) -> str:
    match = re.search(r"^#\s+(.+?)\s*$", body, re.MULTILINE)
    return match.group(1).strip() if match else fallback


def clean_inline(text: str) -> str:
    text = re.sub(r"!?\[([^\]]+)\]\([^)]+\)", r"\1", text)
    text = re.sub(r"[`*_>#|]", "", text)
    text = re.sub(r"\s+", " ", text).strip(" -：:。")
    return html.unescape(text)


def extract_summary(body: str, title: str) -> str:
    lines = body.splitlines()
    paragraphs: list[str] = []
    current: list[str] = []
    in_fence = False
    seen_h2 = False
    for line in lines:
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        if line.startswith("## "):
            seen_h2 = True
            if current:
                paragraphs.append(" ".join(current))
                current = []
            continue
        stripped = line.strip()
        if not stripped:
            if current:
                paragraphs.append(" ".join(current))
                current = []
            continue
        if stripped.startswith("#") or stripped.startswith(("- ", "* ", ">", "|")):
            continue
        if re.match(r"^\d+[.)]\s", stripped):
            continue
        if seen_h2 or len(stripped) > 24:
            current.append(stripped)
    if current:
        paragraphs.append(" ".join(current))
    summary = clean_inline(next((value for value in paragraphs if len(clean_inline(value)) >= 12), title))
    return summary[:197] + "…" if len(summary) > 198 else summary


def extract_raw_header(body: str, label: str) -> str:
    match = re.search(rf"^- {re.escape(label)}：(.+?)\s*$", body, re.MULTILINE)
    return match.group(1).strip() if match else ""


def remove_legacy_header_fields(body: str) -> str:
    lines = body.splitlines()
    output: list[str] = []
    before_h2 = True
    for line in lines:
        if line.startswith("## "):
            before_h2 = False
        if before_h2 and re.match(r"^- (类型|日期|状态|最近更新)：", line):
            continue
        output.append(line)
    return re.sub(r"\n{3,}", "\n\n", "\n".join(output)).rstrip() + "\n"


def filename_date(path: Path) -> str:
    match = DATE_IN_NAME.match(path.name)
    if match:
        value = match.group(1)
        return f"{value[:4]}-{value[4:6]}-{value[6:]}"
    return ""


def extract_updated(body: str, created: str, path: Path) -> str:
    recent = re.search(r"最近更新：\s*(\d{4}-\d{2}-\d{2})", body)
    if recent:
        return recent.group(1)
    modified = datetime.fromtimestamp(path.stat().st_mtime).date().isoformat()
    return max(created or modified, modified)


def classify_areas(title: str, body: str, relative_path: str) -> list[str]:
    haystack = f" {title} {relative_path} {body[:2500]} ".lower()
    found = [area for area, terms in AREA_RULES if any(term.lower() in haystack for term in terms)]
    return found[:5] or ["cross-cutting"]


def suggest_categories(areas: Iterable[str]) -> list[str]:
    unique = {str(area) for area in areas}
    return [category for category, rule in CATEGORY_AREA_MAP.items() if unique & rule]


def normalize_status(kind: str, raw: str, body: str) -> tuple[str, str]:
    text = f"{raw}\n{body[:8000]}".lower()
    if kind == "requirement":
        if any(term in raw for term in ("草案", "讨论")):
            return "draft", "not_applicable"
        if any(term in raw for term in ("取消", "废弃", "回退")):
            return "cancelled", "not_applicable"
        if any(term in raw for term in ("被替代", "已取代")):
            return "superseded", "not_applicable"
        return "approved", "not_applicable"
    if kind == "development":
        if "回退" in raw or "废弃" in raw:
            status = "abandoned"
        elif "设计中" in raw or "尚未实施" in raw or "待实施" in raw:
            status = "planned"
        elif "实施中" in raw and not any(term in raw for term in ("代码完成", "已实施，待", "实现已完成")):
            status = "in_progress"
        elif any(term in raw for term in ("p4/p5 尚未完成", "尚未完成", "未完成交付")):
            status = "in_progress"
        elif any(term in raw for term in ("待验收", "待人工", "待用户授权", "仍在验收", "未通过项", "完整验收仍")):
            status = "verification"
        elif any(term in raw for term in ("已完成", "完成", "已收尾", "诊断完成", "已实现并验证")) or not raw:
            status = "done"
        else:
            status = "in_progress"
        verification = verification_from_text(text)
        if status in {"planned", "in_progress"} and verification == "passed":
            verification = "partial"
        return status, verification
    if kind == "archive":
        return ("superseded" if any(term in raw for term in ("被替代", "已取代")) else "recorded", verification_from_text(text))
    if kind == "gameplay":
        return "current", "not_applicable"
    if kind == "backlog":
        return "current", "not_applicable"
    return "reference", "not_applicable"


def verification_from_text(text: str) -> str:
    if any(term in text for term in ("失败", "未通过")) and not any(term in text for term in ("修复后通过", "最终通过")):
        return "partial"
    if any(term in text for term in ("未验证", "待验收", "未完成", "待人工", "待用户授权", "尚未执行", "仍有待办")):
        return "partial"
    if any(term in text for term in ("验证通过", "测试通过", "构建通过", "验收通过", "完成")):
        return "passed"
    return "not_run"


def extract_next_action(kind: str, status: str, body: str) -> str:
    if status not in {"draft", "planned", "in_progress", "verification"}:
        return ""
    unchecked = re.search(r"^\s*[-*]\s+\[ \]\s+(.+)$", body, re.MULTILINE)
    if unchecked:
        return clean_inline(unchecked.group(1))[:200]
    heading = re.search(r"^##+\s+(?:遗留问题|尚需产品确认的选择|待确认|风险与备忘).*?$(.*?)(?=^##|\Z)", body, re.MULTILINE | re.DOTALL)
    if heading:
        bullet = re.search(r"^\s*[-*]\s+(.+)$", heading.group(1), re.MULTILINE)
        if bullet:
            return clean_inline(bullet.group(1))[:200]
    if kind == "requirement":
        return "确认需求边界与验收标准"
    if status == "verification":
        return "完成正文所列待验收项"
    if status == "planned":
        return "按任务清单开始实施"
    return "继续完成正文中的未完成任务"


def extract_headings(body: str) -> list[dict[str, Any]]:
    result = []
    in_fence = False
    for line in body.splitlines():
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        match = re.match(r"^(#{2,4})\s+(.+?)\s*$", line)
        if match:
            result.append({"level": len(match.group(1)), "title": clean_inline(match.group(2))})
    return result


def core_root_files(progress_root: Path) -> list[Path]:
    files: list[Path] = []
    for directory in CORE_DIRS:
        files.extend(sorted((progress_root / directory).glob("*.md"), key=lambda value: value.name.casefold()))
    return files


def create_work_assignments(progress_root: Path) -> dict[str, tuple[str, str, str]]:
    paths = list((progress_root / "RequirementDocument").glob("*.md")) + list((progress_root / "DevelopmentDocumentation").glob("*.md"))
    by_name: dict[str, list[Path]] = defaultdict(list)
    used_by_date: dict[str, set[int]] = defaultdict(set)
    existing: dict[str, dict[str, str]] = defaultdict(dict)
    for path in paths:
        by_name[path.name].append(path)
        metadata, _ = split_front_matter(read_text(path))
        if metadata.get("schema") != SCHEMA:
            continue
        kind = CORE_DIRS[path.parent.name]
        existing[path.name][kind] = str(metadata.get("id", ""))
        work_id = str(metadata.get("work_id", ""))
        if existing[path.name].get("work_id") and existing[path.name]["work_id"] != work_id:
            raise ValueError(f"paired documents disagree on work_id: {path.name}")
        existing[path.name]["work_id"] = work_id
        match = re.fullmatch(r"WORK-(\d{8})-(\d{3})", work_id)
        if match:
            used_by_date[match.group(1)].add(int(match.group(2)))
    sequence: dict[str, tuple[str, str, str]] = {}
    for name in sorted(by_name, key=str.casefold):
        date_key = DATE_IN_NAME.match(name).group(1) if DATE_IN_NAME.match(name) else "00000000"
        known = existing.get(name, {})
        work_id = known.get("work_id", "")
        if not work_id:
            next_index = max(used_by_date[date_key], default=0) + 1
            used_by_date[date_key].add(next_index)
            work_id = f"WORK-{date_key}-{next_index:03d}"
        suffix = work_id.removeprefix("WORK-")
        sequence[name] = (
            work_id,
            known.get("requirement") or f"REQ-{suffix}",
            known.get("development") or f"DEV-{suffix}",
        )
    return sequence


def archive_assignments(progress_root: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    pending: dict[str, list[str]] = defaultdict(list)
    used_by_date: dict[str, set[int]] = defaultdict(set)
    for path in sorted((progress_root / "Archive").glob("*.md"), key=lambda value: value.name.casefold()):
        date_key = DATE_IN_NAME.match(path.name).group(1) if DATE_IN_NAME.match(path.name) else "00000000"
        metadata, _ = split_front_matter(read_text(path))
        archive_id = str(metadata.get("id", "")) if metadata.get("schema") == SCHEMA else ""
        match = re.fullmatch(r"ARC-(\d{8})-(\d{3})", archive_id)
        if match:
            result[path.name] = archive_id
            used_by_date[match.group(1)].add(int(match.group(2)))
        else:
            pending[date_key].append(path.name)
    for date_key, names in pending.items():
        next_index = max(used_by_date[date_key], default=0) + 1
        for name in names:
            result[name] = f"ARC-{date_key}-{next_index:03d}"
            used_by_date[date_key].add(next_index)
            next_index += 1
    return result


def path_key(path: Path, project_root: Path) -> str:
    return path.resolve().relative_to(project_root.resolve()).as_posix()


def metadata_for_core(
    path: Path,
    project_root: Path,
    work_map: dict[str, tuple[str, str, str]],
    archive_map: dict[str, str],
) -> tuple[dict[str, Any], str]:
    text = read_text(path)
    existing, body = split_front_matter(text)
    if existing.get("schema") == SCHEMA:
        return existing, body
    kind = CORE_DIRS[path.parent.name]
    title = extract_title(body, path.stem)
    raw_status = extract_raw_header(body, "状态")
    if kind == "gameplay":
        raw_status = extract_raw_header(body, "最近更新")
    created = filename_date(path) or extract_raw_header(body, "日期") or date.today().isoformat()
    status, verification = normalize_status(kind, raw_status, body)
    work_id = ""
    if kind in {"requirement", "development"}:
        work_id, req_id, dev_id = work_map[path.name]
        doc_id = req_id if kind == "requirement" else dev_id
    elif kind == "archive":
        doc_id = archive_map[path.name]
    else:
        doc_id = GAMEPLAY_IDS.get(path.stem, f"GAMEPLAY-{hashlib.sha1(path.stem.encode('utf-8')).hexdigest()[:10].upper()}")
    metadata = {
        "schema": SCHEMA,
        "id": doc_id,
        "work_id": work_id,
        "kind": kind,
        "role": "root",
        "title": title,
        "areas": classify_areas(title, body, path_key(path, project_root)),
        "status": status,
        "verification": verification,
        "created": created,
        "updated": extract_updated(body, created, path),
        "summary": extract_summary(body, title),
        "next_action": extract_next_action(kind, status, body),
        "relations": {},
        "status_note": raw_status,
    }
    return metadata, remove_legacy_header_fields(body)


def enrich_relations(items: list[tuple[Path, dict[str, Any], str]], project_root: Path) -> None:
    by_relative = {path_key(path, project_root): metadata for path, metadata, _ in items}
    by_name_kind = {(path.name, metadata["kind"]): metadata for path, metadata, _ in items}
    for path, metadata, body in items:
        relations: dict[str, Any] = dict(metadata.get("relations", {}))
        kind = metadata["kind"]
        if kind == "requirement":
            counterpart = by_name_kind.get((path.name, "development"))
            relations["development"] = counterpart["id"] if counterpart else None
        elif kind == "development":
            counterpart = by_name_kind.get((path.name, "requirement"))
            relations["requirement"] = counterpart["id"] if counterpart else None
            if counterpart is None:
                relations["standalone"] = True
            else:
                relations.pop("standalone", None)
        elif kind == "archive":
            related_ids: list[str] = []
            for target in markdown_targets(body):
                resolved = resolve_link(path, target)
                if not resolved:
                    continue
                try:
                    key = path_key(resolved, project_root)
                except ValueError:
                    continue
                linked = by_relative.get(key)
                if linked and linked.get("work_id") and linked["work_id"] not in related_ids:
                    related_ids.append(linked["work_id"])
            relations["work_items"] = related_ids
        metadata["relations"] = relations


def migrate(project_root: Path, apply: bool) -> dict[str, Any]:
    progress_root = project_root / "Progress"
    work_map = create_work_assignments(progress_root)
    archive_map = archive_assignments(progress_root)
    items: list[tuple[Path, dict[str, Any], str]] = []
    initial_hashes: dict[Path, str] = {}
    for path in core_root_files(progress_root):
        initial_hashes[path] = hashlib.sha256(path.read_bytes()).hexdigest()
        metadata, body = metadata_for_core(path, project_root, work_map, archive_map)
        items.append((path, metadata, body))
    enrich_relations(items, project_root)
    changed: list[str] = []
    for path, metadata, body in items:
        rendered = render_document(metadata, body)
        if read_text(path) == rendered:
            continue
        changed.append(path_key(path, project_root))
        if apply:
            current_hash = hashlib.sha256(path.read_bytes()).hexdigest()
            if current_hash != initial_hashes[path]:
                raise RuntimeError(f"Refusing to overwrite concurrently changed file: {path}")
            atomic_write(path, rendered)
    return {
        "mode": "apply" if apply else "dry-run",
        "core_documents": len(items),
        "work_items": len({metadata.get("work_id") for _, metadata, _ in items if metadata.get("work_id")}),
        "changed": len(changed),
        "paths": changed,
    }


def body_after_title(body: str) -> str:
    match = re.search(r"^#\s+.+?(?:\n|$)", body, re.MULTILINE)
    if not match or body[: match.start()].strip():
        raise ValueError("document body must begin with one H1 title")
    return body[match.end() :]


def split_destination(raw: str) -> tuple[str, str, bool]:
    value = raw.strip()
    if value.startswith("<") and ">" in value:
        end = value.index(">")
        return value[1:end], value[end + 1 :], True
    title_match = re.match(r"^(.*?)(\s+[\"'].+[\"'])$", value)
    if title_match:
        return title_match.group(1), title_match.group(2), False
    return value, "", False


def local_link_target(target: str, source: Path) -> tuple[Path, str] | None:
    if re.match(r"^(?:https?://|mailto:|codex:|#)", target, re.IGNORECASE):
        return None
    path_part, separator, anchor = target.partition("#")
    if not path_part:
        return None
    decoded = unquote(path_part)
    if re.match(r"^[A-Za-z]:[/\\]", decoded) or decoded.startswith(("/", "\\")):
        return None
    return (source.parent / Path(decoded)).resolve(), f"#{anchor}" if separator else ""


def transform_markdown_links(text: str, source: Path, destination: Path | None = None) -> str:
    link_pattern = re.compile(r"(!?\[[^\]]*\]\()([^)]+)(\))")

    def replace(match: re.Match[str]) -> str:
        target, suffix, wrapped = split_destination(match.group(2))
        resolved = local_link_target(target, source)
        if resolved is None:
            return match.group(0)
        absolute, anchor = resolved
        if destination is None:
            rewritten = absolute.as_posix() + anchor
        else:
            rewritten = os.path.relpath(absolute, destination.parent).replace("\\", "/") + anchor
        if wrapped or " " in rewritten:
            rewritten = f"<{rewritten}>"
        return f"{match.group(1)}{rewritten}{suffix}{match.group(3)}"

    output: list[str] = []
    in_fence = False
    for line in text.splitlines(keepends=True):
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            output.append(line)
        elif in_fence:
            output.append(line)
        else:
            output.append(link_pattern.sub(replace, line))
    return "".join(output)


def normalized_payload_hash(payload: str, source: Path) -> str:
    normalized = transform_markdown_links(payload, source)
    return hashlib.sha256(normalized.encode("utf-8")).hexdigest()


def segment_payload(payload: str, spec: SplitSpec) -> list[str]:
    starts = [0]
    for group in spec.groups[1:]:
        marker = group.start_heading
        match = re.search(rf"(?m)^{re.escape(marker or '')}\s*$", payload)
        if not match:
            raise ValueError(f"split heading not found in {spec.relative_path}: {marker}")
        starts.append(match.start())
    if starts != sorted(set(starts)):
        raise ValueError(f"split headings are out of order in {spec.relative_path}")
    starts.append(len(payload))
    return [payload[starts[index] : starts[index + 1]] for index in range(len(spec.groups))]


def child_metadata(parent: dict[str, Any], group: SplitGroup, index: int, segment: str, source: Path) -> dict[str, Any]:
    title = f"{parent['title']} · {group.title}"
    return {
        "schema": SCHEMA,
        "id": f"{parent['id']}-D{index:02d}",
        "work_id": parent.get("work_id", ""),
        "kind": parent["kind"],
        "role": "detail",
        "title": title,
        "areas": list(parent.get("areas", [])),
        "status": parent["status"],
        "verification": parent["verification"],
        "created": parent["created"],
        "updated": parent["updated"],
        "summary": extract_summary(segment, group.title),
        "next_action": parent.get("next_action", ""),
        "relations": {"parent": parent["id"]},
        "status_note": parent.get("status_note", ""),
        "split_order": index,
        "split_segment_sha256": normalized_payload_hash(segment, source),
    }


def split_parent_body(parent: dict[str, Any], spec: SplitSpec, payload_hash: str) -> str:
    lines = [
        f"# {parent['title']}",
        "",
        "> 本页保留稳定入口与整体状态；原始正文已按连续章节无损迁入下列子页。外部链接继续指向本页。",
        "",
        "## 文档导航",
        "",
    ]
    for index, group in enumerate(spec.groups, 1):
        lines.append(f"{index}. [{group.title}]({Path(spec.relative_path).stem}/{group.filename})")
    lines.extend(
        [
            "",
            "## 当前摘要",
            "",
            f"- 状态：`{parent['status']}`",
            f"- 验证：`{parent['verification']}`",
            f"- 摘要：{parent.get('summary') or '—'}",
            f"- 下一步：{parent.get('next_action') or '—'}",
            "",
            "## 内容完整性",
            "",
            f"- 拆分正文规范化 SHA-256：`{payload_hash}`",
            "- 使用 `progress_docs.py split --check` 可重新拼接子页并核对哈希。",
            "",
        ]
    )
    return "\n".join(lines)


def verify_split(project_root: Path, spec: SplitSpec) -> dict[str, Any]:
    source = project_root / spec.relative_path
    metadata, _ = split_front_matter(read_text(source))
    expected_hash = str(metadata.get("split_payload_sha256", ""))
    expected_ids = [f"{metadata.get('id')}-D{index:02d}" for index in range(1, len(spec.groups) + 1)]
    errors: list[str] = []
    normalized_segments: list[str] = []
    child_ids: list[str] = []
    child_root = source.with_suffix("")
    for index, group in enumerate(spec.groups, 1):
        child = child_root / group.filename
        if not child.exists():
            errors.append(f"missing child: {path_key(child, project_root)}")
            continue
        child_meta, child_body = split_front_matter(read_text(child))
        child_ids.append(str(child_meta.get("id", "")))
        if child_meta.get("role") != "detail" or child_meta.get("relations", {}).get("parent") != metadata.get("id"):
            errors.append(f"invalid parent relation: {path_key(child, project_root)}")
        if SPLIT_CONTENT_MARKER not in child_body:
            errors.append(f"missing split marker: {path_key(child, project_root)}")
            continue
        segment = child_body.split(SPLIT_CONTENT_MARKER, 1)[1]
        segment_hash = normalized_payload_hash(segment, child)
        if segment_hash != child_meta.get("split_segment_sha256"):
            errors.append(f"segment hash mismatch: {path_key(child, project_root)}")
        normalized_segments.append(transform_markdown_links(segment, child))
    reconstructed_hash = hashlib.sha256("".join(normalized_segments).encode("utf-8")).hexdigest()
    if child_ids != expected_ids:
        errors.append("child ids or order do not match the split contract")
    if metadata.get("relations", {}).get("children") != expected_ids:
        errors.append("parent children relation does not match the split contract")
    if not expected_hash or reconstructed_hash != expected_hash:
        errors.append("reconstructed payload hash does not match the pre-split payload")
    return {
        "path": spec.relative_path,
        "children": len(spec.groups),
        "expected_sha256": expected_hash,
        "reconstructed_sha256": reconstructed_hash,
        "errors": errors,
    }


def repair_split_boundaries(project_root: Path, spec: SplitSpec) -> list[str]:
    repaired: list[str] = []
    source = project_root / spec.relative_path
    child_root = source.with_suffix("")
    for group in spec.groups:
        child = child_root / group.filename
        if not child.exists():
            continue
        text = read_text(child)
        metadata, body = split_front_matter(text)
        if SPLIT_CONTENT_MARKER not in body:
            continue
        segment = body.split(SPLIT_CONTENT_MARKER, 1)[1]
        expected = str(metadata.get("split_segment_sha256", ""))
        if normalized_payload_hash(segment, child) == expected:
            continue
        if normalized_payload_hash(segment + "\n", child) == expected:
            atomic_write(child, text + "\n")
            repaired.append(path_key(child, project_root))
        elif segment.endswith("\n") and normalized_payload_hash(segment[:-1], child) == expected:
            atomic_write(child, text[:-1])
            repaired.append(path_key(child, project_root))
    return repaired


def split_long_documents(project_root: Path, mode: str) -> dict[str, Any]:
    results: list[dict[str, Any]] = []
    changed = 0
    for spec in SPLIT_SPECS:
        source = project_root / spec.relative_path
        source_bytes_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        metadata, body = split_front_matter(read_text(source))
        if metadata.get("split_payload_sha256"):
            repaired = repair_split_boundaries(project_root, spec) if mode == "apply" else []
            changed += 1 if repaired else 0
            result = verify_split(project_root, spec)
            result["repaired"] = repaired
            results.append(result)
            continue
        if mode == "check":
            results.append({"path": spec.relative_path, "children": 0, "errors": ["document has not been split"]})
            continue
        payload = body_after_title(body)
        segments = segment_payload(payload, spec)
        payload_hash = normalized_payload_hash(payload, source)
        child_root = source.with_suffix("")
        split_metadata = dict(metadata)
        split_metadata["updated"] = date.today().isoformat()
        planned: list[tuple[Path, str, str]] = []
        child_ids: list[str] = []
        for index, (group, segment) in enumerate(zip(spec.groups, segments), 1):
            child = child_root / group.filename
            child_meta = child_metadata(split_metadata, group, index, segment, source)
            child_ids.append(str(child_meta["id"]))
            rewritten = transform_markdown_links(segment, source, child)
            child_body = f"# {child_meta['title']}\n\n{SPLIT_CONTENT_MARKER}{rewritten}"
            planned.append((child, render_document(child_meta, child_body), child_meta["split_segment_sha256"]))
        parent_meta = dict(split_metadata)
        parent_relations = dict(parent_meta.get("relations", {}))
        parent_relations["children"] = child_ids
        parent_meta["relations"] = parent_relations
        parent_meta["split_payload_sha256"] = payload_hash
        parent_meta["split_children"] = len(planned)
        parent_rendered = render_document(parent_meta, split_parent_body(parent_meta, spec, payload_hash))
        changed += 1
        if mode == "apply":
            if hashlib.sha256(source.read_bytes()).hexdigest() != source_bytes_hash:
                raise RuntimeError(f"Refusing to overwrite concurrently changed file: {source}")
            for child, rendered, _ in planned:
                if child.exists() and read_text(child) != rendered:
                    raise RuntimeError(f"Refusing to overwrite existing split child: {child}")
                atomic_write(child, rendered)
            atomic_write(source, parent_rendered)
            results.append(verify_split(project_root, spec))
        else:
            reconstructed = "".join(transform_markdown_links(transform_markdown_links(segment, source, child), child) for (child, _, _), segment in zip(planned, segments))
            reconstructed_hash = hashlib.sha256(reconstructed.encode("utf-8")).hexdigest()
            results.append({
                "path": spec.relative_path,
                "children": len(planned),
                "expected_sha256": payload_hash,
                "reconstructed_sha256": reconstructed_hash,
                "errors": [] if reconstructed_hash == payload_hash else ["dry-run reconstruction mismatch"],
            })
    return {
        "mode": mode,
        "changed": changed if mode != "check" else 0,
        "documents": results,
        "errors": [error for result in results for error in result.get("errors", [])],
    }


def fallback_document(path: Path, project_root: Path) -> Document:
    body = read_text(path)
    relative = path_key(path, project_root)
    title = extract_title(body, path.stem)
    created = filename_date(path) or date.fromtimestamp(path.stat().st_mtime).isoformat()
    ref_id = "REF-" + hashlib.sha1(relative.encode("utf-8")).hexdigest()[:12].upper()
    metadata = {
        "schema": "legacy/reference",
        "id": ref_id,
        "work_id": "",
        "kind": "reference",
        "role": "root",
        "title": title,
        "areas": classify_areas(title, body, relative),
        "status": "reference",
        "verification": "not_applicable",
        "created": created,
        "updated": datetime.fromtimestamp(path.stat().st_mtime).date().isoformat(),
        "summary": extract_summary(body, title),
        "next_action": "",
        "relations": {},
        "status_note": "未迁移的嵌套参考资料",
    }
    return Document(path, relative, metadata, body, fallback=True)


def load_documents(project_root: Path, include_index: bool = False) -> list[Document]:
    progress_root = project_root / "Progress"
    documents: list[Document] = []
    for path in sorted(progress_root.rglob("*.md"), key=lambda value: value.as_posix().casefold()):
        relative = path.relative_to(progress_root).as_posix()
        if relative.startswith("_Index/") and not include_index:
            continue
        text = read_text(path)
        metadata, body = split_front_matter(text)
        if metadata.get("schema") == SCHEMA:
            documents.append(Document(path, path_key(path, project_root), metadata, body))
        else:
            documents.append(fallback_document(path, project_root))
    return documents


def markdown_targets(body: str) -> list[str]:
    without_fences: list[str] = []
    in_fence = False
    for line in body.splitlines():
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if not in_fence:
            without_fences.append(re.sub(r"`[^`]*`", "", line))
    targets = []
    for match in MARKDOWN_LINK.finditer("\n".join(without_fences)):
        raw = match.group(1).strip()
        if raw.startswith("<") and ">" in raw:
            raw = raw[1 : raw.index(">")]
        elif ' "' in raw:
            raw = raw.split(' "', 1)[0]
        target = unquote(raw.split("#", 1)[0].strip())
        if target:
            targets.append(target)
    return targets


def resolve_link(source: Path, target: str) -> Path | None:
    if re.match(r"^(?:https?://|mailto:|codex:|#)", target, re.IGNORECASE):
        return None
    candidate = Path(target)
    if re.match(r"^[A-Za-z]:[/\\]", target):
        return Path(target)
    return (source.parent / candidate).resolve()


def validate(project_root: Path, documents: list[Document] | None = None) -> dict[str, list[dict[str, str]]]:
    docs = documents or load_documents(project_root)
    errors: list[dict[str, str]] = []
    warnings: list[dict[str, str]] = []
    ids: dict[str, str] = {}
    roots = [doc for doc in docs if doc.metadata.get("role") == "root"]
    for doc in docs:
        meta = doc.metadata
        if doc.fallback:
            if any(part in CORE_DIRS for part in doc.path.relative_to(project_root / "Progress").parts[:1]):
                if doc.path.parent.name in CORE_DIRS:
                    errors.append(issue(doc, "missing_metadata", "顶层核心文档缺少 guli-progress/v1 元数据"))
            continue
        missing = [field for field in COMMON_FIELDS if field not in meta]
        if missing:
            errors.append(issue(doc, "missing_fields", ", ".join(missing)))
        doc_id = str(meta.get("id", ""))
        if doc_id in ids:
            errors.append(issue(doc, "duplicate_id", f"与 {ids[doc_id]} 重复：{doc_id}"))
        elif doc_id:
            ids[doc_id] = doc.relative_path
        kind = str(meta.get("kind", ""))
        status = str(meta.get("status", ""))
        if status not in STATUS_VALUES.get(kind, set()):
            errors.append(issue(doc, "invalid_status", f"{kind}: {status}"))
        verification = str(meta.get("verification", ""))
        if verification not in VERIFICATION_VALUES:
            errors.append(issue(doc, "invalid_verification", verification))
        categories = meta.get("categories")
        if categories is not None:
            if not isinstance(categories, list) or not set(categories) <= CATEGORY_VALUES:
                errors.append(issue(doc, "invalid_categories", f"允许值：{sorted(CATEGORY_VALUES)}"))
        elif kind in {"requirement", "development", "archive"} and meta.get("role") == "root":
            warnings.append(issue(doc, "missing_categories", "归档/需求/开发根文档建议填写 categories（可为空数组）"))
        if status in {"draft", "planned", "in_progress", "verification"} and not str(meta.get("next_action", "")).strip():
            errors.append(issue(doc, "missing_next_action", "活跃文档必须填写 next_action"))
        size_kb = doc.path.stat().st_size / 1024
        threshold = {"requirement": 20, "development": 30, "gameplay": 30, "archive": 80}.get(kind)
        if meta.get("role") == "root" and threshold and size_kb > threshold:
            warnings.append(issue(doc, "oversize", f"{size_kb:.1f}KB，建议阈值 {threshold}KB"))
        if kind == "development" and len(CHECKBOX.findall(doc.body)) > 25:
            warnings.append(issue(doc, "large_task_list", f"{len(CHECKBOX.findall(doc.body))} 个任务项"))
    by_work: dict[str, dict[str, list[Document]]] = defaultdict(lambda: defaultdict(list))
    for doc in roots:
        work_id = str(doc.metadata.get("work_id", ""))
        if work_id:
            by_work[work_id][str(doc.metadata.get("kind"))].append(doc)
    for work_id, kinds in by_work.items():
        if len(kinds.get("requirement", [])) > 1 or len(kinds.get("development", [])) > 1:
            errors.append({"path": work_id, "code": "duplicate_work_root", "message": "同一 work_id 存在重复根文档"})
        req = kinds.get("requirement", [])
        dev = kinds.get("development", [])
        if req and dev:
            if req[0].metadata.get("relations", {}).get("development") != dev[0].metadata.get("id"):
                errors.append(issue(req[0], "broken_pair", f"未关联 {dev[0].metadata.get('id')}"))
            if dev[0].metadata.get("relations", {}).get("requirement") != req[0].metadata.get("id"):
                errors.append(issue(dev[0], "broken_pair", f"未关联 {req[0].metadata.get('id')}"))
        elif dev and not dev[0].metadata.get("relations", {}).get("standalone"):
            errors.append(issue(dev[0], "orphan_development", "缺少需求且未标记 standalone"))
    for path in sorted((project_root / "Progress").rglob("*.md")):
        if "_Index" in path.parts:
            continue
        _, body = split_front_matter(read_text(path))
        for target in markdown_targets(body):
            if not target.lower().split("#", 1)[0].endswith(".md"):
                continue
            resolved = resolve_link(path, target)
            if resolved is not None and not resolved.exists():
                errors.append({
                    "path": path_key(path, project_root),
                    "code": "broken_markdown_link",
                    "message": target,
                })
    errors.sort(key=lambda value: (value["path"], value["code"], value["message"]))
    warnings.sort(key=lambda value: (value["path"], value["code"], value["message"]))
    return {"errors": errors, "warnings": warnings}


def issue(doc: Document, code: str, message: str) -> dict[str, str]:
    return {"path": doc.relative_path, "code": code, "message": message}


def derive_stage(requirement: Document | None, development: Document | None) -> str:
    if requirement and requirement.metadata.get("status") == "draft":
        return "draft"
    if development is None or development.metadata.get("status") == "planned":
        return "planned"
    status = str(development.metadata.get("status"))
    return {
        "in_progress": "in_progress",
        "verification": "verification",
        "done": "done",
        "abandoned": "done",
    }.get(status, "planned")


def parse_backlog(documents: Iterable[Document]) -> list[dict[str, Any]]:
    items: list[dict[str, Any]] = []
    for doc in documents:
        if doc.metadata.get("kind") != "backlog":
            continue
        matches = list(re.finditer(r"^##\s+(IDEA-\d{6}-\d{3})\s+(.+?)\s*$", doc.body, re.MULTILINE))
        for index, match in enumerate(matches):
            segment = doc.body[match.end() : matches[index + 1].start() if index + 1 < len(matches) else len(doc.body)]
            fields = dict(re.findall(r"^- ([^：]+)：\s*(.*?)\s*$", segment, re.MULTILINE))
            items.append({
                "id": match.group(1),
                "title": match.group(2),
                "status": fields.get("状态", "inbox"),
                "areas": [value.strip() for value in fields.get("模块", "cross-cutting").split(",") if value.strip()],
                "value": fields.get("价值", ""),
                "question": fields.get("待确认", ""),
                "requirement": fields.get("正式需求", ""),
                "path": doc.relative_path,
            })
    return items


def build_snapshot(project_root: Path, include_documents: bool = True) -> dict[str, Any]:
    documents = load_documents(project_root)
    diagnostics = validate(project_root, documents)
    by_work: dict[str, dict[str, Any]] = defaultdict(dict)
    archives_by_work: dict[str, list[Document]] = defaultdict(list)
    for doc in documents:
        meta = doc.metadata
        if meta.get("role") == "root" and meta.get("kind") in {"requirement", "development"} and meta.get("work_id"):
            by_work[str(meta["work_id"])][str(meta["kind"])] = doc
        if meta.get("kind") == "archive":
            for work_id in meta.get("relations", {}).get("work_items", []) or []:
                archives_by_work[str(work_id)].append(doc)
    work_items: list[dict[str, Any]] = []
    for work_id, linked in by_work.items():
        req = linked.get("requirement")
        dev = linked.get("development")
        primary = dev or req
        dev_public = dev.public() if dev else None
        work_items.append({
            "id": work_id,
            "title": (req or dev).metadata.get("title"),
            "stage": derive_stage(req, dev),
            "areas": sorted(set((req or dev).metadata.get("areas", []))),
            "updated": max(str(doc.metadata.get("updated", "")) for doc in (req, dev) if doc),
            "summary": (req or dev).metadata.get("summary", ""),
            "next_action": (dev or req).metadata.get("next_action", ""),
            "verification": dev.metadata.get("verification", "not_applicable") if dev else "not_applicable",
            "requirement_id": req.metadata.get("id") if req else None,
            "development_id": dev.metadata.get("id") if dev else None,
            "archive_ids": [doc.metadata.get("id") for doc in sorted(archives_by_work[work_id], key=lambda value: str(value.metadata.get("created", "")), reverse=True)],
            "tasks_total": dev_public["tasks_total"] if dev_public else 0,
            "tasks_done": dev_public["tasks_done"] if dev_public else 0,
            "task_progress": dev_public["task_progress"] if dev_public else None,
        })
    work_items.sort(key=lambda value: (value["updated"], value["id"]), reverse=True)
    public_docs = [doc.public() for doc in documents]
    public_docs.sort(key=lambda value: (str(value.get("updated", "")), str(value.get("id", ""))), reverse=True)
    stage_counts = Counter(item["stage"] for item in work_items)
    kind_counts = Counter(str(doc.metadata.get("kind")) for doc in documents)
    revision_source = "\n".join(f"{doc.relative_path}:{doc.path.stat().st_mtime_ns}:{doc.path.stat().st_size}" for doc in documents)
    result = {
        "schema": SCHEMA,
        "generated_at": datetime.now().astimezone().isoformat(timespec="seconds"),
        "revision": hashlib.sha256(revision_source.encode("utf-8")).hexdigest()[:16],
        "stats": {
            "documents": len(documents),
            "work_items": len(work_items),
            "by_kind": dict(sorted(kind_counts.items())),
            "by_stage": dict(sorted(stage_counts.items())),
            "errors": len(diagnostics["errors"]),
            "warnings": len(diagnostics["warnings"]),
        },
        "work_items": work_items,
        "backlog": parse_backlog(documents),
        "diagnostics": diagnostics,
    }
    if include_documents:
        result["documents"] = public_docs
    return result


def md_link(doc: dict[str, Any], project_root: Path) -> str:
    path = Path(doc["path"])
    relative = os.path.relpath(project_root / path, project_root / "Progress" / "_Index").replace("\\", "/")
    return f"[{doc.get('title', doc.get('id'))}]({relative})"


def table(headers: list[str], rows: list[list[Any]]) -> str:
    output = ["| " + " | ".join(headers) + " |", "|" + "|".join("---" for _ in headers) + "|"]
    for row in rows:
        output.append("| " + " | ".join(str(value).replace("\n", " ").replace("|", "\\|") for value in row) + " |")
    return "\n".join(output)


def generated_files(project_root: Path) -> dict[Path, str]:
    snapshot = build_snapshot(project_root)
    docs_by_id = {doc["id"]: doc for doc in snapshot["documents"]}
    index_root = project_root / "Progress" / "_Index"
    banner = "> 自动生成，请勿手改。运行 `progress_docs.py build` 刷新。\n\n"
    stages = [
        ("draft", "待确认需求"),
        ("planned", "规划中"),
        ("in_progress", "实施中"),
        ("verification", "待验收"),
        ("done", "已完成"),
    ]
    current_parts = ["# 当前工作", "", banner.rstrip(), ""]
    for stage, label in stages:
        rows = []
        for item in snapshot["work_items"]:
            if item["stage"] != stage:
                continue
            target_id = item["development_id"] or item["requirement_id"]
            target = docs_by_id[target_id]
            progress = "—" if item["task_progress"] is None else f"{item['tasks_done']}/{item['tasks_total']} ({item['task_progress']}%)"
            rows.append([md_link(target, project_root), ", ".join(item["areas"]), progress, item["next_action"] or "—", item["updated"]])
        current_parts.extend([f"## {label}", "", table(["工作项", "模块", "任务", "下一步", "更新"], rows) if rows else "暂无。", ""])
    verification_rows = []
    for item in snapshot["work_items"]:
        if item["stage"] == "verification" or item["verification"] in {"partial", "failed"}:
            target = docs_by_id[item["development_id"] or item["requirement_id"]]
            verification_rows.append([md_link(target, project_root), item["stage"], item["verification"], item["next_action"] or "—", item["updated"]])
    verification_md = "# 待验收与验证边界\n\n" + banner + table(["工作项", "阶段", "验证", "下一步", "更新"], verification_rows) + "\n"
    areas: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for doc in snapshot["documents"]:
        if doc.get("role") != "root":
            continue
        for area in doc.get("areas", []):
            areas[area].append(doc)
    area_parts = ["# 按模块查看", "", banner.rstrip(), ""]
    for area in sorted(areas):
        rows = [[md_link(doc, project_root), doc["kind"], doc["status"], doc["updated"]] for doc in sorted(areas[area], key=lambda value: value["updated"], reverse=True)]
        area_parts.extend([f"## {area}", "", table(["文档", "类型", "状态", "更新"], rows), ""])
    archives = [doc for doc in snapshot["documents"] if doc.get("kind") == "archive" and doc.get("role") == "root"]
    archive_rows = [[doc["created"], md_link(doc, project_root), ", ".join(doc["areas"]), doc["verification"], doc["summary"]] for doc in sorted(archives, key=lambda value: (value["created"], value["id"]), reverse=True)]
    archive_md = "# 归档时间线\n\n" + banner + table(["日期", "归档", "模块", "验证", "摘要"], archive_rows) + "\n"
    diagnostics = snapshot["diagnostics"]
    quality_parts = ["# 文档质量", "", banner.rstrip(), "", f"- 错误：{len(diagnostics['errors'])}", f"- 提示：{len(diagnostics['warnings'])}", ""]
    for label, key in (("错误", "errors"), ("提示", "warnings")):
        quality_parts.extend([f"## {label}", ""])
        rows = [[item["path"], item["code"], item["message"]] for item in diagnostics[key]]
        quality_parts.extend([table(["路径", "代码", "说明"], rows) if rows else "暂无。", ""])
    catalog_lines = []
    for doc in sorted(snapshot["documents"], key=lambda value: value["id"]):
        catalog = {key: doc.get(key) for key in ("id", "work_id", "kind", "role", "title", "areas", "categories", "status", "verification", "created", "updated", "summary", "next_action", "relations", "path", "headings", "tasks_total", "tasks_done", "task_progress")}
        catalog_lines.append(json.dumps(catalog, ensure_ascii=False, separators=(",", ":")))
    return {
        index_root / "Current.md": "\n".join(current_parts).rstrip() + "\n",
        index_root / "Verification.md": verification_md,
        index_root / "ByArea.md": "\n".join(area_parts).rstrip() + "\n",
        index_root / "Archive.md": archive_md,
        index_root / "Quality.md": "\n".join(quality_parts).rstrip() + "\n",
        index_root / "catalog.jsonl": "\n".join(catalog_lines) + "\n",
    }


def build_indexes(project_root: Path, check_only: bool) -> dict[str, Any]:
    expected = generated_files(project_root)
    changed = []
    for path, content in expected.items():
        if not path.exists() or read_text(path) != content:
            changed.append(path_key(path, project_root))
            if not check_only:
                atomic_write(path, content)
    return {"mode": "check" if check_only else "write", "changed": len(changed), "paths": changed}


def search_snapshot(snapshot: dict[str, Any], query: str, kind: str = "", status: str = "") -> list[dict[str, Any]]:
    needle = query.casefold().strip()
    results = []
    for doc in snapshot.get("documents", []):
        if kind and doc.get("kind") != kind:
            continue
        if status and doc.get("status") != status:
            continue
        haystack = " ".join(
            [
                str(doc.get("title", "")),
                str(doc.get("summary", "")),
                " ".join(doc.get("areas", [])),
                " ".join(heading.get("title", "") for heading in doc.get("headings", [])),
            ]
        ).casefold()
        if needle and needle not in haystack:
            continue
        score = 0
        if needle:
            score += 10 if needle in str(doc.get("title", "")).casefold() else 0
            score += 5 if needle in str(doc.get("summary", "")).casefold() else 0
            score += 2 if needle in " ".join(doc.get("areas", [])).casefold() else 0
        results.append({**doc, "score": score})
    results.sort(key=lambda value: (value["score"], value.get("updated", "")), reverse=True)
    return results


def print_json(value: Any) -> None:
    print(json.dumps(value, ensure_ascii=False, indent=2, default=str))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", help="GuLiStrike project root")
    subparsers = parser.add_subparsers(dest="command", required=True)
    migrate_parser = subparsers.add_parser("migrate", help="Add or normalize core document metadata")
    migrate_mode = migrate_parser.add_mutually_exclusive_group(required=True)
    migrate_mode.add_argument("--dry-run", action="store_true")
    migrate_mode.add_argument("--apply", action="store_true")
    build_parser = subparsers.add_parser("build", help="Generate tracked indexes")
    build_parser.add_argument("--check", action="store_true", help="Fail if generated files are stale")
    check_parser = subparsers.add_parser("check", help="Validate metadata, links, and document budgets")
    check_parser.add_argument("--json", action="store_true")
    snapshot_parser = subparsers.add_parser("snapshot", help="Print dashboard data")
    snapshot_parser.add_argument("--json", action="store_true", help="Compatibility flag; output is always JSON")
    split_parser = subparsers.add_parser("split", help="Split and verify the three maintained long documents")
    split_mode = split_parser.add_mutually_exclusive_group(required=True)
    split_mode.add_argument("--dry-run", action="store_true")
    split_mode.add_argument("--apply", action="store_true")
    split_mode.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    root = project_root_from(args.project_root)
    if args.command == "migrate":
        print_json(migrate(root, apply=args.apply))
        return 0
    if args.command == "build":
        result = build_indexes(root, check_only=args.check)
        print_json(result)
        return 1 if args.check and result["changed"] else 0
    if args.command == "check":
        result = validate(root)
        if args.json:
            print_json(result)
        else:
            print(f"errors={len(result['errors'])} warnings={len(result['warnings'])}")
            for level in ("errors", "warnings"):
                for item in result[level]:
                    print(f"{level[:-1].upper()} {item['code']} {item['path']}: {item['message']}")
        return 1 if result["errors"] else 0
    if args.command == "snapshot":
        print_json(build_snapshot(root))
        return 0
    if args.command == "split":
        mode = "apply" if args.apply else "check" if args.check else "dry-run"
        result = split_long_documents(root, mode)
        print_json(result)
        return 1 if result["errors"] else 0
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
