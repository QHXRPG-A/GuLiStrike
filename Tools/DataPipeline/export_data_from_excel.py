"""数据管线 stage 1（v2：Excel 元数据驱动，无 tables.json）。

用法:  python Tools/DataPipeline/export_data_from_excel.py

输入:  Data/Excel/*.xlsx（顶层；_ 开头的 sheet 跳过；_legacy/ 等子目录不扫）

表格式约定（每个 sheet）:
  第 1 行 = 列名（合法 C++ 标识符；武器表另支持“产生的法术场”数字ID引用）
  第 2 行 = 类型: int | float | bool | str | softclass | softobject
  第 3 行 = 必要性: Necessary | Optional
  第 4 行起 = 数据
  标准三列每表必有: id(int/Necessary)、name(str/Necessary)、Note(str/Optional)
  向量: PrefixX/PrefixY/PrefixZ 三个 float 列 -> FVector Prefix
  name 列 = DataTable 的 RowName（全表唯一），不生成 C++ 属性
  softclass = /Game/... 类路径 -> TSoftClassPtr<UObject>
  softobject = /Game/... 资产路径 -> TSoftObjectPtr<UObject>

产出:
  1. Data/Json/DT_{主干}_{sheet}.json    行数据（首键 "Name" = name 列值）
  2. Data/Json/manifest.json             表清单（DT 资产名 -> struct 路径）
  3. Source/GuLiStrike/Gameplay/Data/Generated/{主干}TableRows.h
     C++ 行结构，自动生成禁止手改；内容不变不写（改数值不触发重编译）

工作流:
  改数值       -> 跑本脚本 -> 跑 Scripts/import_data_to_engine.py（不碰 C++）
  加列/新表    -> 跑本脚本（自动重生成 .h）-> 重编译模块 -> 跑导入

生成结构命名: F{主干}{sheet}Row（如 FGuLiStrikeShipPartsRow）
"""
import json
import re
import sys
from pathlib import Path

import openpyxl

PROJECT = Path(__file__).resolve().parents[2]
EXCEL_DIR = PROJECT / "Data/Excel"
JSON_DIR = PROJECT / "Data/Json"
GEN_HEADER_DIR = PROJECT / "Source/GuLiStrike/Gameplay/Data/Generated"
MODULE = "GuLiStrike"

# Source ownership can change without renaming serialized UE assets/USTRUCTs.
# Values remain exclusively in Excel; these aliases preserve existing Blueprint,
# DataTableRowHandle, C++ and packaged-content identities during the migration.
SECONDARY_WORKBOOK = "GuLiStrikeSecondaryWeapons"
SECONDARY_TABLE_IDENTITIES = {
    "Skills": ("GuLiStrikeCommander", "Skills"),
    "UnitSkills": ("GuLiStrikeCommander", "UnitSkills"),
    "WeaponMounts": ("GuLiStrikeCommander", "WeaponMounts"),
    "WingmanWeapons": ("GuLiStrikeShip", "WingmanWeapons"),
    "WingmanTargeting": ("GuLiStrikeShip", "WingmanTargeting"),
}
SPELL_FIELD_TABLE = "DT_GuLiStrikeSpellFields_Fields"
FIELD_REFERENCE_COLUMN = "产生的法术场"
FIELD_REFERENCE_SHEETS = {"Skills", "WingmanWeapons"}

TYPES = {"int", "float", "bool", "str", "softclass", "softobject"}
MARKS = {"Necessary", "Optional"}
IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

# C++ 类型/默认值映射（向量与标准三列另行处理）
CPP_OF = {
    "int": ("int32", "0"),
    "float": ("float", "0.0f"),
    "bool": ("bool", "false"),
    "str": ("FString", None),
    "softclass": ("TSoftClassPtr<UObject>", None),
    "softobject": ("TSoftObjectPtr<UObject>", None),
}
VECTOR_CPP = ("FVector", "FVector::ZeroVector")


def cell_ref(sheet, row, col):
    return f"{sheet}!{openpyxl.utils.get_column_letter(col)}{row}"


class SheetError(Exception):
    pass


def load_schema(ws):
    """解析三行元数据 -> (列定义列表, 向量前缀集合)。"""
    if ws.max_row < 3 or ws.max_column < 3:
        raise SheetError(f"至少需要 3 行元数据 x 3 列（当前 {ws.max_row} 行 x {ws.max_column} 列）")

    names, types, marks = [], [], []
    for r_idx, bucket in ((1, names), (2, types), (3, marks)):
        for c_idx in range(1, ws.max_column + 1):
            v = ws.cell(row=r_idx, column=c_idx).value
            bucket.append(str(v).strip() if v is not None else "")

    # 截掉表头的空列（openpyxl 有时会撑出格式残留列）
    last = max((i for i, n in enumerate(names) if n), default=-1)
    if last < 0:
        raise SheetError("第 1 行（列名）为空")
    if any(n is not None and i > last for i, n in enumerate(names)):
        raise SheetError("列名行中间有空列")
    names, types, marks = names[: last + 1], types[: last + 1], marks[: last + 1]
    ncols = len(names)

    for i, n in enumerate(names):
        if not IDENT_RE.match(n) and not (n == FIELD_REFERENCE_COLUMN and ws.title in FIELD_REFERENCE_SHEETS):
            raise SheetError(f"{cell_ref(ws.title, 1, i + 1)}: 列名 '{n}' 不是合法标识符（字母/下划线开头）")
    dup = {n for n in names if names.count(n) > 1}
    if dup:
        raise SheetError(f"列名重复: {sorted(dup)}")
    if FIELD_REFERENCE_COLUMN in names:
        index = names.index(FIELD_REFERENCE_COLUMN)
        if types[index] != "int" or marks[index] != "Optional":
            raise SheetError("产生的法术场必须为 int / Optional，填写 GuLiStrikeSpellFields.xlsx / Fields.id")
        if "EffectConfigId" in names:
            raise SheetError("产生的法术场替代旧 EffectConfigId 列，不能同时维护两个引用")

    for i in range(ncols):
        if types[i] not in TYPES:
            raise SheetError(f"{cell_ref(ws.title, 2, i + 1)}: 未知类型 '{types[i]}'（可选: {sorted(TYPES)}）")
        if marks[i] not in MARKS:
            raise SheetError(f"{cell_ref(ws.title, 3, i + 1)}: 未知标记 '{marks[i]}'（可选: {sorted(MARKS)}）")

    # 标准三列
    for std_name, std_type in (("id", "int"), ("name", "str"), ("Note", "str")):
        if std_name not in names:
            raise SheetError(f"缺少标准列 '{std_name}'")
        i = names.index(std_name)
        if types[i] != std_type:
            raise SheetError(f"标准列 '{std_name}' 类型应为 {std_type}，实际 '{types[i]}'")
    if marks[names.index("id")] != "Necessary" or marks[names.index("name")] != "Necessary":
        raise SheetError("标准列 id/name 必须标记为 Necessary")
    if marks[names.index("Note")] != "Optional":
        raise SheetError("标准列 Note 必须标记为 Optional")

    # 向量三件套：同前缀的 X/Y/Z 三列，必须 float、缺一不可
    vec_members = {n for n in names for suffix in ("X", "Y", "Z") if n.endswith(suffix)}
    prefixes = {n[:-1] for n in vec_members}
    for p in prefixes:
        have = {a for a in "XYZ" if f"{p}{a}" in names}
        if have != set("XYZ"):
            raise SheetError(f"向量列不完整: {p}X/{p}Y/{p}Z 必须成组出现（当前 {sorted(have)}）")
        if p in names:
            raise SheetError(f"列 '{p}' 与向量列 {p}X/{p}Y/{p}Z 冲突")
        for a in "XYZ":
            if types[names.index(f"{p}{a}")] != "float":
                raise SheetError(f"向量列 {p}{a} 类型必须为 float")

    cols = [{"name": n, "type": types[i], "necessary": marks[i] == "Necessary", "col": i + 1}
            for i, n in enumerate(names)]
    return cols, prefixes


def check_value(sheet, col, row_idx, raw):
    """单元格值 vs 类型标记一致性。返回归一化值（空 = None）。"""
    where = cell_ref(sheet, row_idx, col["col"])
    if raw is None or (isinstance(raw, str) and not raw.strip()):
        if col["necessary"]:
            raise SheetError(f"{where}: 列 '{col['name']}' 标记 Necessary 但单元格为空")
        return {"int": 0, "float": 0.0, "bool": False,
                "str": "", "softclass": "", "softobject": ""}[col["type"]]
    t = col["type"]
    if t == "int":
        if isinstance(raw, bool) or not isinstance(raw, (int, float)) or float(raw) != int(raw):
            raise SheetError(f"{where}: int 列 '{col['name']}' 的值不是整数: {raw!r}")
        return int(raw)
    if t == "float":
        if isinstance(raw, bool) or not isinstance(raw, (int, float)):
            raise SheetError(f"{where}: float 列 '{col['name']}' 的值不是数字: {raw!r}")
        return float(raw)
    if t == "bool":
        if not isinstance(raw, bool):
            raise SheetError(f"{where}: bool 列 '{col['name']}' 的值不是布尔（Excel 里用 TRUE/FALSE）: {raw!r}")
        return raw
    # str / softclass / softobject：数字或布尔出现在字符串列 = 类型行笔误
    if isinstance(raw, (int, float, bool)):
        raise SheetError(f"{where}: 列 '{col['name']}' 标记 {t} 但单元格是 {type(raw).__name__} "
                         f"{raw!r}（检查第 2 行类型是否写错）")
    return str(raw).strip()


def export_sheet(ws):
    """一个 sheet -> (json 行列表, 结构属性列表)。校验失败抛 SheetError。"""
    cols, vec_prefixes = load_schema(ws)
    by_name = {c["name"]: c for c in cols}
    vec_axis_cols = {f"{p}{a}": p for p in vec_prefixes for a in "XYZ"}

    rows, seen_names, seen_ids = [], set(), set()
    for r_idx in range(4, ws.max_row + 1):
        if all(ws.cell(row=r_idx, column=c["col"]).value is None for c in cols):
            continue  # 全空行（表尾格式残留）
        row = {}
        for col in cols:
            v = check_value(ws.title, col, r_idx, ws.cell(row=r_idx, column=col["col"]).value)
            if v is not None:
                row[col["name"]] = v
        name_v, id_v = row.get("name"), row.get("id")
        if name_v in seen_names:
            raise SheetError(f"{cell_ref(ws.title, r_idx, by_name['name']['col'])}: "
                             f"name '{name_v}' 重复（name 是行名，必须全表唯一）")
        if id_v in seen_ids:
            raise SheetError(f"{cell_ref(ws.title, r_idx, by_name['id']['col'])}: id {id_v} 重复")
        seen_names.add(name_v)
        seen_ids.add(id_v)
        rows.append(row)

    # JSON 行：首键 Name = name 列值；其余键 = 生成的 C++ 属性名；向量合并为 {X,Y,Z}
    out_rows = []
    for src in rows:
        out = {"Name": src["name"]}
        for col in cols:
            n = col["name"]
            if n == "name":
                continue
            if n in vec_axis_cols:
                p = vec_axis_cols[n]
                if p not in out and any(f"{p}{a}" in src for a in "XYZ"):
                    out[p] = {a: src.get(f"{p}{a}", 0.0) for a in "XYZ"}
                continue
            key = "Id" if n == "id" else n
            if n in src:
                out[key] = src[n]
        out_rows.append(out)

    # 结构属性列表（供代码生成）
    props = []
    for col in cols:
        n = col["name"]
        if n == "name":
            continue  # 行名列，不生成属性
        if n == FIELD_REFERENCE_COLUMN:
            # Numeric authoring IDs resolve to the existing serialized row-name
            # property after every workbook has passed schema validation.
            props.append({"prop": "EffectConfigId", "cpp": "FString", "default": None,
                          "comment": "产生的法术场 (Fields.id) -> EffectConfigId (Fields.name；导出时解析)"})
            continue
        if n in vec_axis_cols:
            p = vec_axis_cols[n]
            if p not in {x["prop"] for x in props}:
                cpp, default = VECTOR_CPP
                props.append({"prop": p, "cpp": cpp, "default": default,
                              "comment": f"{n[:-1]}X/{n[:-1]}Y/{n[:-1]}Z (float)"})
            continue
        cpp, default = CPP_OF[col["type"]]
        props.append({"prop": "Id" if n == "id" else n, "cpp": cpp, "default": default,
                      "comment": f"{n} ({col['type']}, {col['necessary'] and 'Necessary' or 'Optional'})"})

    if not out_rows:
        print(f"note: [{ws.title}] 没有数据行，产出空表")
    return out_rows, props


def gen_header_text(stem, sheets_props):
    """Generate one stable native header from tables with explicit provenance."""
    sources = sorted({source["excel"] for entry in sheets_props for source in entry["sources"]})
    lines = [
        "// ====================================================================",
        f"// 自动生成自 Data/Excel/: {', '.join(sources)} —— 禁止手改。",
        "// 由 Tools/DataPipeline/export_data_from_excel.py 生成。",
        "// 表结构变更（加列/新表）后重跑导出并重编译 GuLiStrike 模块。",
        "// 约定: name 列是 DataTable 行名（不生成属性）；id -> Id；",
        "//       PrefixX/Y/Z 三列 -> FVector Prefix；softclass -> TSoftClassPtr<UObject>；",
        "//       softobject -> TSoftObjectPtr<UObject>。",
        "// ====================================================================",
        "",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        '#include "Engine/DataTable.h"',
        f'#include "{stem}TableRows.generated.h"',
        "",
    ]
    for entry in sheets_props:
        sheet, props = entry["identity_sheet"], entry["props"]
        struct = f"F{stem}{sheet}Row"
        provenance = "; ".join(f"{source['excel']} / {source['sheet']}" for source in entry["sources"])
        lines += [
            f"/** DataTable DT_{stem}_{sheet} 的行结构（源: {provenance}）。 */",
            "USTRUCT(BlueprintType)",
            f"struct {struct} : public FTableRowBase",
            "{",
            "\tGENERATED_BODY()",
            "",
        ]
        for p in props:
            init = f" = {p['default']}" if p["default"] else ""
            lines.append(f"\t/** {p['comment']} */")
            lines.append(f'\tUPROPERTY(EditAnywhere, BlueprintReadOnly, Category="{sheet}")')
            lines.append(f"\t{p['cpp']} {p['prop']}{init};")
            lines.append("")
        lines += ["};", ""]
    return "\n".join(lines).rstrip() + "\n"


def write_if_changed(path, text):
    """内容不变不写（保持 mtime，避免无谓重编译）。"""
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")
    return True


def resolve_spell_field_references(tables):
    """Resolve the sole authored field ID into the stable UE row-name contract."""
    fields = tables.get(SPELL_FIELD_TABLE)
    expected_source = {"excel": "GuLiStrikeSpellFields.xlsx", "sheet": "Fields"}
    if not fields or fields["sources"] != [expected_source]:
        raise SheetError("所有法术场必须统一维护在 GuLiStrikeSpellFields.xlsx / Fields")
    by_id = {row["Id"]: row for row in fields["rows"]}
    for table, entry in tables.items():
        if not any(source["excel"] == f"{SECONDARY_WORKBOOK}.xlsx"
                   and source["sheet"] in FIELD_REFERENCE_SHEETS for source in entry["sources"]):
            continue
        for row in entry["rows"]:
            field_id = row.pop(FIELD_REFERENCE_COLUMN, 0)
            if field_id == 0:
                if row.get("ExecutorId") == "LaunchProjectile" or row.get("AttackPattern") == "GroundDive":
                    raise SheetError(f"{table}/{row['Name']}: 此攻击必须填写“产生的法术场”引用")
                continue
            field = by_id.get(field_id)
            if field_id < 0 or field is None:
                raise SheetError(f"{table}/{row['Name']}: 产生的法术场 id={field_id} 不存在于 Fields.id")
            if field.get("FieldType", "").lower() != "combat":
                raise SheetError(f"{table}/{row['Name']}: 武器产生的法术场 id={field_id} 必须为 Combat")
            row["EffectConfigId"] = field["Name"]


def validate_building_references(tables):
    """Validate conditional building fields before any output is replaced."""
    entry = tables.get("DT_GuLiStrikeBuildings_Buildings")
    if entry is None:
        return
    buildings = {row["Id"]: row for row in entry["rows"]}
    soldiers = {row["Id"]: row for row in tables["DT_GuLiStrikeCommander_Soldiers"]["rows"]}
    fields = {row["Id"]: row for row in tables[SPELL_FIELD_TABLE]["rows"]}
    for row in buildings.values():
        label = f"Buildings/{row['Name']}"
        category = row["Category"]
        if row["Id"] <= 0 or category not in range(6) or row["PlacementType"] not in range(7):
            raise SheetError(f"{label}: invalid ID, Category or PlacementType enum")
        if row["MaxHealth"] <= 0 or any(row[key] < 0 for key in
                ("MaxShield", "BuildLevel", "BlueCost", "RedCost", "ConstructionWork")):
            raise SheetError(f"{label}: invalid health, cost or construction work")
        if any(row[key][axis] <= 0 for key in ("CollisionExtent", "MeshScale") for axis in "XYZ"):
            raise SheetError(f"{label}: footprint and mesh scale must be positive")
        if category != 5 and row["ConstructionWork"] <= 0:
            raise SheetError(f"{label}: constructible buildings require ConstructionWork")
        if category == 2:
            unit = soldiers.get(row["ProductionUnitId"])
            if not unit or unit.get("ActorClass") or not unit.get("ModelAsset") \
                    or row["ProductionSeconds"] <= 0 or row["ProductionCount"] <= 0:
                raise SheetError(f"{label}: barracks require a Mass unit, positive period and count")
        if category == 3 and (row["ShieldRadius"] <= 0 or row["ShieldRechargePerSecond"] <= 0):
            raise SheetError(f"{label}: shield supply parameters are required")
        gifts = row["FirstCaptureGiftIds"]
        if gifts and not re.fullmatch(r"[1-9][0-9]*(,[1-9][0-9]*)*", gifts):
            raise SheetError(f"{label}: gift IDs must be comma-separated positive integers")
        gift_ids = [int(value) for value in gifts.split(",")] if gifts else []
        if len(gift_ids) > 4:
            raise SheetError(f"{label}: strongholds have four fixed facility slots")
        if any(value not in buildings or buildings[value]["Category"] == 5 for value in gift_ids):
            raise SheetError(f"{label}: invalid gift building reference")
        gate = fields.get(row["GateFieldId"])
        if row["GateFieldId"] and (not gate or gate["FieldType"] != "StrongholdGate"):
            raise SheetError(f"{label}: GateFieldId must reference a StrongholdGate")
        if category == 5 and (not gift_ids or not gate):
            raise SheetError(f"{label}: strongholds require gift IDs and gate field")
    for row in fields.values():
        if row["FieldType"] != "StrongholdGate":
            continue
        required = ("RadiusCentimeters", "LaneHeightCentimeters", "AscentSeconds",
                    "AccelerationSeconds", "DecelerationSeconds", "ExitFlashSeconds",
                    "SpeedMultiplier", "ExitRadiusCentimeters")
        if not row["bPermanent"] or not row["bIndestructible"] or any(row[key] <= 0 for key in required):
            raise SheetError(f"Fields/{row['Name']}: invalid permanent gate configuration")


def main():
    workbooks = [w for w in sorted(EXCEL_DIR.glob("*.xlsx")) if not w.name.startswith("~$")]
    if not workbooks:
        print(f"error: {EXCEL_DIR} 下没有 xlsx", file=sys.stderr)
        sys.exit(1)

    manifest = {"tables": {}}
    tables = {}
    consolidated = any(w.stem == SECONDARY_WORKBOOK for w in workbooks)
    failed = False
    for wb_path in workbooks:
        stem = wb_path.stem
        if not IDENT_RE.match(stem):
            print(f"error: 文件名主干 '{stem}' 不是合法标识符（用作 C++ 结构名前缀）", file=sys.stderr)
            failed = True
            continue
        wb = openpyxl.load_workbook(wb_path, data_only=True)
        if stem == SECONDARY_WORKBOOK:
            missing = (set(SECONDARY_TABLE_IDENTITIES) | {"Projectiles"}) - set(wb.sheetnames)
            if missing:
                print(f"error: {wb_path.name} 缺少武器源工作表: {sorted(missing)}", file=sys.stderr)
                failed = True
        for ws in wb.worksheets:
            if ws.title.startswith("_"):
                print(f"note: 跳过 sheet '{ws.title}'（_ 前缀）")
                continue
            try:
                if stem == SECONDARY_WORKBOOK and ws.title == "WeaponFields":
                    raise SheetError("WeaponFields 已统一迁至 GuLiStrikeSpellFields.xlsx / Fields；禁止重复维护")
                if stem == SECONDARY_WORKBOOK and ws.title in FIELD_REFERENCE_SHEETS:
                    if FIELD_REFERENCE_COLUMN not in [cell.value for cell in ws[1]]:
                        raise SheetError("缺少“产生的法术场”列（int / Optional，引用 Fields.id）")
                rows, props = export_sheet(ws)
                if consolidated and stem != SECONDARY_WORKBOOK:
                    retired = {(s, t) for s, t in SECONDARY_TABLE_IDENTITIES.values()
                               if s != "GuLiStrikeSpellFields"}
                    if (stem, ws.title) in retired:
                        raise SheetError("此武器工作表已迁至 GuLiStrikeSecondaryWeapons.xlsx；禁止重复维护")
                identity_stem, identity_sheet = SECONDARY_TABLE_IDENTITIES.get(ws.title, (stem, ws.title)) \
                    if stem == SECONDARY_WORKBOOK else (stem, ws.title)
                table = f"DT_{identity_stem}_{identity_sheet}"
                source = {"excel": wb_path.name, "sheet": ws.title}
                if table in tables:
                    raise SheetError(f"重复的DataTable身份: {table}；每张表必须只有一个维护入口")
                else:
                    tables[table] = {"stem": identity_stem, "identity_sheet": identity_sheet,
                                     "props": props, "rows": rows, "sources": [source]}
            except SheetError as e:
                print(f"error: [{wb_path.name}::{ws.title}] {e}", file=sys.stderr)
                failed = True
                continue
        wb.close()

    if not failed and consolidated:
        try:
            resolve_spell_field_references(tables)
        except SheetError as e:
            print(f"error: {e}", file=sys.stderr)
            failed = True
    if not failed:
        try:
            validate_building_references(tables)
        except SheetError as e:
            print(f"error: {e}", file=sys.stderr)
            failed = True
    if failed:
        print("导出失败，JSON、头文件和manifest均未更新", file=sys.stderr)
        sys.exit(1)
    headers = {}
    for table, entry in tables.items():
        rows = entry["rows"]
        if len(entry["sources"]) > 1:
            rows.sort(key=lambda row: row["Id"])
        json_path = JSON_DIR / f"{table}.json"
        write_if_changed(json_path, json.dumps(rows, ensure_ascii=False, indent=2))
        manifest["tables"][table] = {
            **entry["sources"][0], "sources": entry["sources"],
            "struct": f"/Script/{MODULE}.{entry['stem']}{entry['identity_sheet']}Row",
            "row_key_column": "name",
        }
        headers.setdefault(entry["stem"], []).append(entry)
        print(f"OK: {json_path.relative_to(PROJECT)} ({len(rows)} 行)")
    for stem, entries in headers.items():
        header = GEN_HEADER_DIR / f"{stem}TableRows.h"
        changed = write_if_changed(header, gen_header_text(stem, entries))
        print(f"{'GEN' if changed else 'keep'}: {header.relative_to(PROJECT)}"
              + ("（有变化，需重编译）" if changed else "（无变化）"))
    write_if_changed(JSON_DIR / "manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2))
    print(f"manifest: {JSON_DIR / 'manifest.json'}（{len(manifest['tables'])} 表）")


if __name__ == "__main__":
    main()
