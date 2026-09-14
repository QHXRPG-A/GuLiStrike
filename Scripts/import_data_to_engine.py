"""Create/update DataTable assets from Data/Json/*.json in the live editor (pipeline stage 2).

Run via:  python Scripts/ue_exec.py Scripts/import_data_to_engine.py
Note:    MCPython TCP 服务是排队异步执行——连接立刻关闭、脚本稍后在游戏线程跑。
         发送后轮询 Data/tmp_import_report.json 的 mtime，再读结果。
         Data/tmp_import_progress.log 记录逐步进度，用于定位挂死点。

输入（stage 1 产出，见 Tools/DataPipeline/export_data_from_excel.py）:
    - Data/Json/DT_*.json        行数据（首键 "Name" = Excel 的 name 列）
    - Data/Json/manifest.json    表清单（DT 资产名 -> struct 路径，导出脚本自动生成）

游戏侧接线（新增游戏系统时在此扩展）:
    - WIRING: DT 资产名 -> 要赋值的 CDO 属性名（目前统一赋到 BP_GuLiStrikeShip）；
    - 文件末尾的"部件蓝图 PartId"步骤是飞船系统专属逻辑。

路线（踩坑记录见 Progress/Archive/20260822-数据管线开发总归档-0821至0822.md）：
  - 资产：create_asset(DataTableFactory+struct) 首次建 DT —— 安全，已验证。
  - 行数据：CSVImportFactory + AutomatedImportSettings(ImportRowStruct/ImportType)，
    经 AssetImportTask 导入 CSV 侧车（JSON 数据先在本脚本内转 CSV；向量列用
    "(X=..,Y=..,Z=..)" ImportText 格式）。
  - 禁用：delete_asset（弹模态框卡死）、fill_data_table_from_csv_string /
    fill_data_table_from_json_string（导入完成后收尾路径必挂，复现两次）。

Report:  Data/tmp_import_report.json
"""
import csv
import io
import json
import math
import re
import traceback

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()).rstrip("/\\")
DEST_PATH = "/Game/GuLiStrike/Data"
SHIP_BP_PATH = "/Game/GuLiStrike/Ship/BP_GuLiStrikeShip.BP_GuLiStrikeShip"
MANIFEST_PATH = f"{PROJECT}/Data/Json/manifest.json"
REPORT = f"{PROJECT}/Data/tmp_import_report.json"
PROGRESS = f"{PROJECT}/Data/tmp_import_progress.log"

# DT 资产名 -> BP_GuLiStrikeShip CDO 上的属性名（飞船游戏侧接线）
WIRING = {
    "DT_GuLiStrikeShip_Parts": "part_data_table",
    "DT_GuLiStrikeShip_Tuning": "tuning_data_table",
    "DT_GuLiStrikeShip_Camera": "camera_data_table",
}

# 由 C++ Config settings 通过软引用接线；成功导入后不应被误报为未接线。
CONFIG_WIRED_TABLES = {
    # Ship V3 assets reference these rows; deployed by deploy_wingman_attack_assets.py.
    "DT_GuLiStrikeShip_WingmanWeapons",
    "DT_GuLiStrikeShip_WingmanTargeting",
    "DT_GuLiStrikeCommander_Soldiers",
    "DT_GuLiStrikeCommander_Skills",
    "DT_GuLiStrikeCommander_UnitSkills",
    "DT_GuLiStrikeSpellFields_Fields",
    "DT_GuLiStrikeCommander_WeaponMounts",
    "DT_GuLiStrikeSecondaryWeapons_Projectiles",
}


def mark(msg):
    with open(PROGRESS, "a", encoding="utf-8") as f:
        f.write(str(msg) + "\n")


def to_cell(value):
    """JSON 值 -> DataTable CSV 单元格（ImportText 格式）。"""
    if value is None:
        return ""
    if isinstance(value, bool):
        return "True" if value else "False"
    if isinstance(value, dict):  # 向量：结构体 ImportText 需要带括号格式
        return "(X={:.6f},Y={:.6f},Z={:.6f})".format(
            float(value["X"]), float(value["Y"]), float(value["Z"]))
    if isinstance(value, float) and value == int(value):
        return str(int(value))
    return str(value)


def write_csv_sidecar(rows, path):
    """列名从 JSON 行键自派生（按首次出现顺序），不含任何表结构知识。"""
    columns = []
    for r in rows:
        for k in r:
            if k != "Name" and k not in columns:
                columns.append(k)
    buf = io.StringIO()
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(["Name"] + columns)
    for r in rows:
        w.writerow([r["Name"]] + [to_cell(r.get(c)) for c in columns])
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(buf.getvalue())


def import_table(table_name, table_cfg):
    entry = {"asset": table_name}
    try:
        json_path = f"{PROJECT}/Data/Json/{table_name}.json"
        mark(f"begin {table_name}")
        with open(json_path, encoding="utf-8") as f:
            src_rows = json.load(f)
        entry["source_rows"] = [r["Name"] for r in src_rows]
        csv_path = json_path.replace(".json", ".csv")
        write_csv_sidecar(src_rows, csv_path)
        mark("csv-sidecar-written")

        row_struct = unreal.find_object(None, table_cfg["struct"])
        if not row_struct:
            raise RuntimeError(f"struct not found: {table_cfg['struct']}")
        mark("find-struct")

        # 首次建资产（已存在则复用；重导入整表替换）
        asset_path = f"{DEST_PATH}/{table_name}"
        dt = unreal.load_object(None, f"{asset_path}.{table_name}")
        entry["created"] = dt is None
        if dt is None:
            factory = unreal.DataTableFactory()
            factory.set_editor_property("struct", row_struct)
            dt = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
                asset_name=table_name,
                package_path=DEST_PATH,
                asset_class=unreal.DataTable,
                factory=factory,
            )
            if not dt:
                raise RuntimeError("create_asset returned None")
        mark(f"dt-ready created={entry['created']}")

        # CSVImportFactory + AutomatedImportSettings：官方自动导入通道
        try:
            import_type = unreal.CSVImportType.ECSV_DATA_TABLE
        except AttributeError:
            import_type = 0
        settings = unreal.CSVImportSettings()
        settings.set_editor_property("import_row_struct", row_struct)
        settings.set_editor_property("import_type", import_type)
        csv_factory = unreal.CSVImportFactory()
        csv_factory.set_editor_property("automated_import_settings", settings)

        task = unreal.AssetImportTask()
        task.set_editor_property("factory", csv_factory)
        task.set_editor_property("filename", csv_path)
        task.set_editor_property("destination_path", DEST_PATH)
        task.set_editor_property("destination_name", table_name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        mark("import-task-done")

        lib = unreal.DataTableFunctionLibrary
        row_names = [str(n) for n in lib.get_data_table_row_names(dt)]
        mark(f"rows={row_names}")
        entry["row_names"] = row_names
        if set(row_names) != set(entry["source_rows"]):
            raise RuntimeError(f"row mismatch: json={entry['source_rows']} dt={row_names}")

        # 回读校验：导出 JSON 与源数据全量比对（比抽查更严）
        # 注意导出格式：向量是 "(X=..,Y=..,Z=..)" 字符串、软引用带路径，统一用包含判断
        exported = lib.export_data_table_to_json_string(dt)
        mark("exported")
        ex_rows = {r["Name"]: r for r in json.loads(exported)}

        def parse_vec(v):
            if isinstance(v, dict):
                return [float(v.get("X", 0)), float(v.get("Y", 0)), float(v.get("Z", 0))]
            nums = [float(x) for x in re.findall(r"[-+0-9.eE]+", str(v))]
            return nums[:3] if len(nums) >= 3 else None

        checks = {}
        for src in src_rows:
            name = src["Name"]
            ex = ex_rows.get(name)
            if ex is None:
                checks[name] = False
                continue
            ok_row = True
            for key, sv in src.items():
                if key == "Name":
                    continue
                ev = ex.get(key)
                if isinstance(sv, dict):  # 向量
                    ev_vec = parse_vec(ev) if ev is not None else None
                    ok_row = ok_row and ev_vec is not None and all(
                        round(a, 3) == round(float(b), 3) for a, b in zip(ev_vec, [sv["X"], sv["Y"], sv["Z"]]))
                elif isinstance(sv, bool):
                    ok_row = ok_row and bool(ev) == sv
                elif isinstance(sv, (int, float)):
                    # DataTable JSON export formats large floats with limited significant digits
                    # (for example 13333.333 -> 13333.3). Compare with a tight relative
                    # tolerance instead of fixed three-decimal equality.
                    ok_row = ok_row and ev is not None and math.isclose(
                        float(ev), float(sv), rel_tol=5e-6, abs_tol=1e-3)
                else:  # 文本/路径：软引用带路径后缀，统一用包含判断
                    ok_row = ok_row and ev is not None and str(sv) in str(ev)
            checks[name] = ok_row
        entry["row_checks"] = checks
        if not all(checks.values()):
            raise RuntimeError(f"row value mismatch: {checks}")

        unreal.EditorAssetLibrary.save_loaded_asset(dt, False)
        entry["imported"] = True
        mark("saved")
    except Exception:  # noqa: BLE001
        entry["imported"] = False
        entry["err"] = traceback.format_exc()[-600:]
        mark("error: " + entry["err"].splitlines()[-1])
    return entry


def wire_secondary_projectile_profiles():
    """Bind table-authored motion and verify the native resolver before saving."""
    name = 'DT_GuLiStrikeSecondaryWeapons_Projectiles'
    table = unreal.load_asset(f'{DEST_PATH}/{name}')
    if not table:
        raise RuntimeError('Import SecondaryWeapons/Projectiles before wiring projectile assets')
    with open(f'{PROJECT}/Data/Json/{name}.json', encoding='utf-8') as source:
        rows = json.load(source)
    fields = {
        'speed': 'SpeedCentimetersPerSecond', 'lift_seconds': 'LiftSeconds',
        'minimum_lift_height': 'MinimumLiftHeightCentimeters',
        'maximum_lift_height': 'MaximumLiftHeightCentimeters',
        'lateral_offset': 'LateralOffsetCentimeters', 'convergence_distance': 'ConvergenceDistanceCentimeters',
        'turn_rate': 'TurnRateDegreesPerSecond', 'sweep_radius': 'SweepRadiusCentimeters',
        'maximum_lifetime': 'MaximumLifetimeSeconds',
    }
    assets = []
    seen = set()
    slots = set()
    for row in rows:
        asset = unreal.load_asset(row['ProjectileAsset'])
        slot = (row['UnitTypeId'], row['SlotId'])
        if not isinstance(asset, unreal.GuLiProjectileEffectDefinition) or asset.get_path_name() in seen or slot in slots:
            raise RuntimeError(f"Invalid or duplicate ProjectileAsset in {row['Name']}")
        seen.add(asset.get_path_name())
        slots.add(slot)
        assets.append((row, asset))
    result = []
    catalog = unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_CommanderCombatEffects')
    if not catalog:
        raise RuntimeError('Commander combat effect catalog is missing')
    mounts = list(catalog.get_editor_property('mounts'))
    catalog_changed = False
    for row, asset in assets:
        matching = [m for m in mounts if int(m.get_editor_property('unit_type_id')) == row['UnitTypeId']
                    and str(m.get_editor_property('slot_id')) == row['SlotId']]
        if len(matching) != 1 or str(matching[0].get_editor_property('skill_id')) != row['SkillId']:
            raise RuntimeError(f"Projectiles/{row['Name']} has no matching weapon type/slot/skill binding")
        mount = matching[0]
        if mount.get_editor_property('projectile') != asset:
            mount.set_editor_property('projectile', asset)
            catalog_changed = True
        handle = unreal.DataTableRowHandle(data_table=table, row_name=row['Name'])
        previous = asset.get_editor_property('motion_profile_row')
        changed = previous.get_editor_property('data_table') != table or str(previous.get_editor_property('row_name')) != row['Name']
        if changed:
            asset.modify()
            asset.set_editor_property('motion_profile_row', handle)
        resolved = asset.resolve_motion_settings()
        if resolved is None or any(not math.isclose(float(resolved.get_editor_property(prop)), float(row[column]),
                                                     rel_tol=5e-6, abs_tol=1e-3) for prop, column in fields.items()):
            if changed:
                asset.set_editor_property('motion_profile_row', previous)
            raise RuntimeError(f"Native motion resolver disagrees with Projectiles/{row['Name']}")
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
            raise RuntimeError(f'Could not save {asset.get_path_name()}')
        result.append({'asset': asset.get_path_name(), 'row': row['Name'], 'resolved': {
            prop: float(resolved.get_editor_property(prop)) for prop in fields}})
    if catalog_changed:
        catalog.modify()
        catalog.set_editor_property('mounts', mounts)
        if not unreal.EditorAssetLibrary.save_loaded_asset(catalog, False):
            raise RuntimeError('Could not save Commander projectile bindings')
    return result


def wire_secondary_wingman_profiles():
    name = 'DT_GuLiStrikeShip_WingmanWeapons'
    table = unreal.load_asset(f'{DEST_PATH}/{name}')
    with open(f'{PROJECT}/Data/Json/{name}.json', encoding='utf-8') as source:
        rows = json.load(source)
    result = []
    fields = {'damage': 'Damage', 'cooldown_seconds': 'CooldownSeconds', 'range_centimeters': 'RangeCentimeters',
              'projectile_speed_centimeters_per_second': 'ProjectileSpeedCentimetersPerSecond',
              'projectile_lifetime_seconds': 'ProjectileLifetimeSeconds', 'sweep_radius_centimeters': 'SweepRadiusCentimeters',
              'maximum_homing_turn_rate_degrees_per_second': 'MaximumHomingTurnRateDegreesPerSecond'}
    with open(f'{PROJECT}/Data/Json/DT_GuLiStrikeSpellFields_Fields.json', encoding='utf-8') as source:
        field_rows = {row['Name']: row for row in json.load(source)}
    seen = set()
    for row in rows:
        asset = unreal.load_asset(row['WeaponAsset'])
        if not isinstance(asset, unreal.GuLiWingmanWeaponDefinition) or asset.get_path_name() in seen:
            raise RuntimeError(f"WingmanWeapons/{row['Name']} has an invalid or duplicate weapon asset")
        seen.add(asset.get_path_name())
        if row.get('AttackPattern') == 'GroundDive':
            projectile = unreal.load_asset(row.get('AttackProjectile', ''))
            field = projectile.get_editor_property('impact_field') if isinstance(projectile, unreal.GuLiProjectileEffectDefinition) else None
            if not field or row.get('EffectConfigId') not in field_rows:
                raise RuntimeError(f"WingmanWeapons/{row['Name']} requires impact visuals and a referenced global spell field")
        handle = unreal.DataTableRowHandle(data_table=table, row_name=row['Name'])
        previous = asset.get_editor_property('attack_profile_row')
        changed = previous.get_editor_property('data_table') != table or str(previous.get_editor_property('row_name')) != row['Name']
        if changed:
            asset.modify()
            asset.set_editor_property('attack_profile_row', handle)
        resolved = asset.get_resolved_weapon_config()
        expected = dict(row)
        if row.get('EffectConfigId'):
            expected['Damage'] = field_rows[row['EffectConfigId']]['Damage']
        if resolved is None or any(not math.isclose(float(resolved.get_editor_property(prop)), float(expected.get(column, 0)),
                                                     rel_tol=5e-6, abs_tol=1e-3) for prop, column in fields.items()):
            if changed:
                asset.set_editor_property('attack_profile_row', previous)
            raise RuntimeError(f"Native weapon resolver disagrees with WingmanWeapons/{row['Name']}")
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
            raise RuntimeError(f'Could not save {asset.get_path_name()}')
        result.append({'asset': asset.get_path_name(), 'row': row['Name'], 'resolved': {
            prop: float(resolved.get_editor_property(prop)) for prop in fields}})
    return result


report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}
try:
    manifest = json.loads(open(MANIFEST_PATH, encoding="utf-8").read())
    imported_dts = {}
    for table_name, table_cfg in manifest["tables"].items():
        entry = import_table(table_name, table_cfg)
        report["tables"].append(entry)
        prop = WIRING.get(table_name)
        if entry.get("imported") and prop:
            imported_dts[prop] = unreal.load_object(
                None, f"{DEST_PATH}/{table_name}.{table_name}")
        elif entry.get("imported") and table_name in CONFIG_WIRED_TABLES:
            report["config_wired"].append(table_name)
            mark(f"note: {table_name} is wired through C++ config settings")
        elif prop is None:
            report["unwired"].append(f"{table_name}（WIRING 未登记，已导入但未接线）")
            mark(f"note: {table_name} not in WIRING")

    if any(e.get('asset') == 'DT_GuLiStrikeSecondaryWeapons_Projectiles' and e.get('imported') for e in report['tables']):
        report['projectile_profiles'] = wire_secondary_projectile_profiles()
    if any(e.get('asset') == 'DT_GuLiStrikeShip_WingmanWeapons' and e.get('imported') for e in report['tables']):
        report['wingman_profiles'] = wire_secondary_wingman_profiles()

    # --- 游戏侧接线：赋值到飞船蓝图 CDO（属性名来自 WIRING） ---
    if imported_dts:
        mark("ship-assign-begin")
        ship_bp = unreal.load_object(None, SHIP_BP_PATH)
        scdo = unreal.get_default_object(ship_bp.generated_class())
        for prop_name, dt in imported_dts.items():
            scdo.set_editor_property(prop_name, dt)
        report["ship_assigned"] = sorted(imported_dts)
        ship_bp.modify()
        report["ship_saved"] = bool(unreal.EditorAssetLibrary.save_loaded_asset(ship_bp, False))
        mark("ship-assigned")

    # --- 游戏侧接线（飞船专属）：部件蓝图 PartId（= 资产名去 BP_ 前缀） ---
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    part_ids = {}
    for ad in ar.get_assets_by_path("/Game/GuLiStrike", recursive=True):
        asset = ad.get_asset()
        if not isinstance(asset, unreal.Blueprint):
            continue
        generated = asset.generated_class()
        if not generated:
            continue
        cdo = unreal.get_default_object(generated)
        if not isinstance(cdo, unreal.GuLiStrikeShipPartComponent):
            continue
        name = str(ad.asset_name)
        part_id = name[3:] if name.startswith("BP_") else name
        cdo.set_editor_property("part_id", part_id)
        asset.modify()
        unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
        part_ids[name] = part_id
    report["part_ids"] = part_ids
    mark("part-ids-done")

    # --- 孤儿资产警告：目录里不在 manifest 之列的 DataTable ---
    expected = set(manifest["tables"].keys())
    orphans = []
    for ad in ar.get_assets_by_path(DEST_PATH, recursive=False):
        an = str(ad.asset_name)
        if an not in expected and "DataTable" in str(ad.asset_class_path):
            orphans.append(f"{DEST_PATH}/{an}")
    report["orphan_tables"] = orphans
    if orphans:
        mark(f"WARNING orphan tables: {orphans}")
except Exception:  # noqa: BLE001
    report["errors"].append(traceback.format_exc()[-600:])
    mark("fatal-error")

mark("report-write")
with open(REPORT, "w", encoding="utf-8") as f:
    json.dump(report, f, ensure_ascii=False, indent=2, default=str)

print("import done, report at", REPORT)
