"""数据管线 stage 1（v2：Excel 元数据驱动，无 tables.json）。

用法:  python Tools/DataPipeline/export_data_from_excel.py

输入:  Data/Excel/*.xlsx（顶层；_ 开头的 sheet 跳过；_legacy/ 等子目录不扫）

表格式约定（每个 sheet）:
  第 1 行 = 列名（合法 C++ 标识符，全表唯一）
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
        if not IDENT_RE.match(n):
            raise SheetError(f"{cell_ref(ws.title, 1, i + 1)}: 列名 '{n}' 不是合法标识符（字母/下划线开头）")
    dup = {n for n in names if names.count(n) > 1}
    if dup:
        raise SheetError(f"列名重复: {sorted(dup)}")

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
        return None
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
    """sheets_props: [(sheet 名, props)]，按工作簿内 sheet 顺序。"""
    lines = [
        "// ====================================================================",
        f"// 自动生成自 Data/Excel/{stem}.xlsx —— 禁止手改。",
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
    for sheet, props in sheets_props:
        struct = f"F{stem}{sheet}Row"
        lines += [
            f"/** DataTable DT_{stem}_{sheet} 的行结构（源: {stem}.xlsx 的 {sheet} sheet）。 */",
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


def main():
    workbooks = [w for w in sorted(EXCEL_DIR.glob("*.xlsx")) if not w.name.startswith("~$")]
    if not workbooks:
        print(f"error: {EXCEL_DIR} 下没有 xlsx", file=sys.stderr)
        sys.exit(1)

    manifest = {"tables": {}}
    failed = False
    for wb_path in workbooks:
        stem = wb_path.stem
        if not IDENT_RE.match(stem):
            print(f"error: 文件名主干 '{stem}' 不是合法标识符（用作 C++ 结构名前缀）", file=sys.stderr)
            failed = True
            continue
        wb = openpyxl.load_workbook(wb_path, data_only=True)
        sheets_props = []
        for ws in wb.worksheets:
            if ws.title.startswith("_"):
                print(f"note: 跳过 sheet '{ws.title}'（_ 前缀）")
                continue
            try:
                rows, props = export_sheet(ws)
            except SheetError as e:
                print(f"error: [{wb_path.name}::{ws.title}] {e}", file=sys.stderr)
                failed = True
                continue
            table = f"DT_{stem}_{ws.title}"
            json_path = JSON_DIR / f"{table}.json"
            write_if_changed(json_path, json.dumps(rows, ensure_ascii=False, indent=2))
            manifest["tables"][table] = {
                "excel": wb_path.name,
                "sheet": ws.title,
                "struct": f"/Script/{MODULE}.{stem}{ws.title}Row",
                "row_key_column": "name",
            }
            sheets_props.append((ws.title, props))
            print(f"OK: {json_path.relative_to(PROJECT)} ({len(rows)} 行)")
        if sheets_props:
            header = GEN_HEADER_DIR / f"{stem}TableRows.h"
            changed = write_if_changed(header, gen_header_text(stem, sheets_props))
            state = "GEN" if changed else "keep"
            hint = "（有变化，需重编译）" if changed else "（无变化）"
            print(f"{state}: {header.relative_to(PROJECT)}{hint}")

    if failed:
        print("导出失败，manifest 未更新", file=sys.stderr)
        sys.exit(1)
    write_if_changed(JSON_DIR / "manifest.json", json.dumps(manifest, ensure_ascii=False, indent=2))
    print(f"manifest: {JSON_DIR / 'manifest.json'}（{len(manifest['tables'])} 表）")


if __name__ == "__main__":
    main()
