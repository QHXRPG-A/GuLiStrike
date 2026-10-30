"""Publish the explicit four-pack migration inventory without scanning assets."""

from __future__ import annotations

import json
from pathlib import Path
import shutil
import zipfile


PROJECT = Path(__file__).resolve().parents[2]
ROOT = PROJECT / "data/ExternalAssets"
REPORTS = ROOT / "migrations/20261026"
DESCRIPTIONS = {
    "drone-bot": "机械无人机、14段动画、骨架/物理资产、材质贴图、动画蓝图、展示台与Overview示例。",
    "advanced-dash-vfx": "冲刺/闪现/运动拖尾；18个Niagara系统，材质/函数/贴图、示例蓝图、人体模型和DemoRoom。",
    "muzzle-flashes": "枪口火焰/闪光；101个传统Cascade ParticleSystem，材质贴图、物理材质、展示武器/人物与演示房间。",
    "mech-constructor-spiders-tanks": "模块化蜘蛛机甲、履带/坦克/越野底盘、武器、静态/骨骼网格、91段动画、骨架/物理资产和组合蓝图。",
}
LIMITATIONS = {
    "drone-bot": [
        "源Overview引用HDRIBackdrop插件；本次编辑器未挂载该插件，迁移时背景相关对象无法完整加载。原始Overview包已另存，需启用插件并从原始包恢复后再验收演示背景。",
        "预检曾发现骨架对/Game/DroneBot/Meshes/DroneBot的外部引用；迁移保存后的Registry未再报告该引用。",
        "加载源动画蓝图时出现GetAllWidgetsOfClass结果为空却访问索引0的运行警告；未改写演示逻辑。",
    ],
    "advanced-dash-vfx": [
        "MI_TrailBeam仍引用原包未提供的/Game/CharacterVFXPack/Textures/T_Wave；补原贴图或在项目副本中适配后再验收对应效果。",
        "预检MF_Hilight引用缺失的/Game/AdvancedNiagaraVFX/Textures/Mask/T_Mask12；迁移保存后未再出现在Registry依赖中，原始材质函数包已保留。",
        "需要Niagara；18个系统的运行表现与独立编译诊断未执行。",
    ],
    "muzzle-flashes": [
        "101个特效为Cascade ParticleSystem，未转换为Niagara；运行及视觉效果由用户验收。",
    ],
    "mech-constructor-spiders-tanks": [
        "Wheel_Buggy_Tire依赖当前引擎缺失的/Script/PhysXVehicles.TireConfig，保持原文件并登记为不可加载的旧配置。",
        "车辆蓝图/动画蓝图涉及PhysXVehicles/PhysXVehiclesEditor；迁移时车辆类及示例车Actor出现加载警告，不能把模型入库理解为车辆玩法兼容UE5.7。相关原始包已另存，可在后续Chaos适配时恢复。",
        "MyCarGame.uasset（两处）及Maps/Demo_Scene.umap三个小型源包在UE5.7未注册资产，原样保留；实际已注册示例地图为Maps/MC_Spiders_Demo_Scene。",
        "Projectile_Grenade_3文件中的原对象名为Projectile_Grenade_1；通过UE批量重命名统一为Projectile_Grenade_3，并由编辑器更新引用。",
    ],
}


def load(name: str):
    return json.loads((REPORTS / name).read_text(encoding="utf-8"))


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def preserve_source_packages(packs: list[dict], before: dict) -> list[dict]:
    """Transfer original packages affected by known missing source dependencies."""
    records = []
    for pack in packs:
        baseline = next(p for p in before["packs"] if p["id"] == pack["id"])
        dependencies = baseline["dependencies_before"]
        selected = set(dependencies["missing_game_dependencies"])
        selected.update(baseline["unregistered_source_packages"])
        for package, refs in dependencies["by_package"].items():
            if any(ref.startswith(("/HDRIBackdrop/", "/Script/PhysXVehicles")) for ref in refs):
                selected.add(package)
        with zipfile.ZipFile(pack["source"]) as archive:
            for entry in pack["entries"]:
                if entry["source_package"] not in selected:
                    continue
                target = REPORTS / "compatibility-originals" / pack["id"] / entry["relative_file"]
                if not target.resolve().is_relative_to((REPORTS / "compatibility-originals").resolve()):
                    raise RuntimeError("Backup path escaped the compatibility directory")
                target.parent.mkdir(parents=True, exist_ok=True)
                if not target.exists():
                    with archive.open(entry["archive_entry"]) as source, target.open("xb") as output:
                        shutil.copyfileobj(source, output, 1024 * 1024)
                if target.stat().st_size != entry["bytes"]:
                    raise RuntimeError(f"Original package backup size mismatch: {target}")
                records.append({"pack_id": pack["id"], "source_package": entry["source_package"], "original_archive": pack["source"], "archive_entry": entry["archive_entry"], "bytes": entry["bytes"], "zip_crc32": entry["zip_crc32"], "preserved_file": target.relative_to(PROJECT).as_posix()})
    write_json(REPORTS / "compatibility_originals.json", records)
    return records


def main() -> None:
    packs = load("extraction_report.json")
    before = load("editor_preflight.json")
    final = load("final_files_and_editor_state.json")
    if final["missing_files"] or final["final_files"] != 807:
        raise RuntimeError("Final package-file inventory is incomplete")
    preserved = preserve_source_packages(packs, before)
    catalog_path = ROOT / "catalog.json"
    catalog = json.loads(catalog_path.read_text(encoding="utf-8")) if catalog_path.exists() else {
        "schema": "gulistrike/external-assets/v1",
        "scope": "已登记的外部资源；当前覆盖用户本次指定的四包，不代表全项目历史商城资产普查。",
        "root": "/Game/Assets",
        "packs": [],
    }
    current = {p["id"]: p for p in catalog["packs"]}
    for pack in packs:
        verification = load(pack["id"] + "_validation.json")
        move = load(pack["id"] + "_move.json")
        if not verification["passed"] or not move["passed"]:
            raise RuntimeError(f"Migration readback failed: {pack['id']}")
        entry = {
            "id": pack["id"], "name": pack["name"], "category": pack["category"],
            "source_archive": pack["source"], "source_sha256": pack["source_sha256"],
            "source_archive_prefix": pack["prefix"], "source_root": pack["source_root"],
            "target_root": pack["target_root"], "version_note": pack["version_note"],
            "description": DESCRIPTIONS[pack["id"]],
            "source_file_count": pack["file_count"],
            "registered_package_count": verification["target"]["package_count"],
            "currently_loadable_package_count": verification["target"]["package_count"] - len(verification["load_failures"]),
            "classes": verification["target"]["classes"], "maps": verification["target"]["maps"],
            "folder_file_counts": pack["folder_counts"],
            "unregistered_preserved_packages": [e["target_package"] for e in pack["entries"] if e["source_package"] in verification["unregistered_originals"]],
            "preserved_unloadable_packages": verification["expected_source_load_failures"],
            "external_actor_object_files": pack["external_actor_object_files"],
            "omitted_archive_entries": pack["omitted_archive_entries"],
            "normalized_object_names": move.get("normalized_object_names", []),
            "limitations": LIMITATIONS[pack["id"]],
            "validation": {
                "migration_saved_and_inventory_matched": True,
                "old_path_dependency_count": len(verification["dependencies"]["old_path_dependencies"]),
                "missing_game_dependencies": verification["dependencies"]["missing_game_dependencies"],
                "unexpected_load_failures": verification["unexpected_load_failures"],
                "source_compatibility_limits_retained": True,
                "visual": "not_run_user_review", "runtime": "not_run",
                "independent_cold_reload": "not_run",
            },
            "reports": f"data/ExternalAssets/migrations/20261026/{pack['id']}_validation.json",
            "migrated_on": "2026-10-27", "migration_batch": "20261026",
        }
        if pack.get("duplicate_is_identical"):
            entry["deduplicated_source"] = pack["duplicate"]
            entry["duplicate_sha256"] = pack["duplicate_sha256"]
        current[entry["id"]] = entry
    catalog["packs"] = sorted(current.values(), key=lambda p: (p["category"], p["name"]))
    catalog["updated"] = "2026-10-27"
    catalog["totals"] = {
        "packs": len(catalog["packs"]),
        "source_files": sum(p["source_file_count"] for p in catalog["packs"]),
        "registered_packages": sum(p["registered_package_count"] for p in catalog["packs"]),
        "currently_loadable_packages": sum(p["currently_loadable_package_count"] for p in catalog["packs"]),
    }
    write_json(catalog_path, catalog)
    print(json.dumps({"catalog": str(catalog_path), "totals": catalog["totals"], "preserved_compatibility_originals": len(preserved)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
