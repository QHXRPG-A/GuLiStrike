"""Export every sheet of the pipeline workbook to UE DataTable JSON (validated).

Pipeline stage 1 (system Python + openpyxl), 通用脚本——不含任何具体表的字段知识：
    Data/Excel/<stem>.xlsx  ->  Data/Json/DT_<stem>_<Sheet>.json   （每个 sheet 一张表）

通用性约定：
    - 脚本只实现【通用规则】：行名唯一非空、列类型（num/str/bool/enum/vector/aux/any）、
      数值范围（min/max）、必填列、按列值条件必填（required_by）、跨字段比较（compare）。
    - 每个 sheet 的具体规则全部是数据，放在 Tools/DataPipeline/tables.json：
        schema:   { C++属性名: [类型, 参数1, 参数2, Excel列名?] }
                  - JSON 输出键 = C++属性名；Excel 列名缺省同属性名；
                  - vector 类型在 Excel 里是 <Excel列名>X/Y/Z 三列（第 4 元素即前缀）；
                  - aux 类型只参与校验（enum 合法性、required_by 判定），不进 JSON
                    （如 Parts 的 Type 列只是 Excel 里的分类辅助列）；
                  - 类型: num(数值,参数为min/max) / str / bool / enum(参数为取值列表)
                         / vector / aux(同enum但不导出) / any(原样透传)。
        required:     所有行都必填的属性名列表
        required_by:  { 判定列: { 列值: [必填属性...] } }
        compare:      [ {col, op(>=/>/<=/<), other} ]
        row_name:     行名列的属性名（缺省 "Name"）
    - 换一张表、加一张表都不需要改本脚本，只改配置。
    - 没登记 schema 的 sheet 走推断兜底：首列 Name 唯一非空、单元格类型自动推断、
      <前缀>X/Y/Z 三列自动合并为向量（产出 JSON，等登记后再严格化）。
    - 表名 = DT_{Excel文件名主干}_{sheet名}。

Run: python Tools/DataPipeline/export_data_from_excel.py [xlsx路径]
Exit: 0 ok (json written), 1 validation failure.
"""
import json
import sys
from pathlib import Path

from openpyxl import load_workbook
from openpyxl.utils import get_column_letter

PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CONFIG = PROJECT_ROOT / "Tools" / "DataPipeline" / "tables.json"
JSON_DIR = PROJECT_ROOT / "Data" / "Json"

COMPARE_OPS = {
    ">=": lambda a, b: a >= b,
    ">": lambda a, b: a > b,
    "<=": lambda a, b: a <= b,
    "<": lambda a, b: a < b,
}


# ---------------------------------------------------------------------------
# 通用小工具
# ---------------------------------------------------------------------------

def cell_ref(ws_title, row, col):
    return f"{ws_title}!{get_column_letter(col)}{row}"


def excel_name_of(schema, key):
    """schema 属性对应的 Excel 列名（vector 为前缀）。第 4 元素可覆盖，缺省 = 属性名。"""
    spec = schema.get(key)
    if spec is None:
        return key
    if spec[0] == "vector":
        return spec[3] if len(spec) > 3 else key
    return spec[3] if len(spec) > 3 else key


def excel_columns_of(schema):
    """schema -> {Excel 列名: 属性名}（vector 展开为 <前缀>X/Y/Z 三列）。"""
    out = {}
    for key in schema:
        base = excel_name_of(schema, key)
        if schema[key][0] == "vector":
            for axis in "XYZ":
                out[f"{base}{axis}"] = key
        else:
            out[base] = key
    return out


def check_value(kind, value, p1, p2):
    """按 schema 规则校验单值：返回 (status, payload)。status ∈ ok / empty / bad。"""
    if value is None or value == "":
        return "empty", None
    if kind == "num":
        if isinstance(value, str):
            return "bad", f"应为数字，得到文本 '{value}'"
        v = float(value)
        if p1 is not None and v < p1:
            return "bad", f"{v} 低于下限 {p1}"
        if p2 is not None and v > p2:
            return "bad", f"{v} 超过上限 {p2}"
        return "ok", v
    if kind == "bool":
        if isinstance(value, bool):
            return "ok", value
        if isinstance(value, str) and value.strip().lower() in ("true", "false"):
            return "ok", value.strip().lower() == "true"
        return "bad", f"应为 TRUE/FALSE，得到 {value!r}"
    if kind in ("enum", "aux"):
        s = str(value).strip()
        if s not in p1:
            return "bad", f"必须是 {'/'.join(p1)}，得到 {s!r}"
        return "ok", s
    if kind == "str":
        return "ok", str(value).strip()
    return "ok", value  # any


def normalize_num(value):
    """数值统一成 int/float（整数值转 int，便于 JSON 干净）。"""
    v = float(value)
    return int(v) if v == int(v) else v


# ---------------------------------------------------------------------------
# 配置驱动导出（登记过 schema 的 sheet）
# ---------------------------------------------------------------------------

def export_configured(ws, cfg):
    errors = []
    schema = cfg["schema"]
    row_name_key = cfg.get("row_name", "Name")
    excel_of = excel_columns_of(schema)  # Excel 列名 -> 属性名

    # 表头匹配（未知列报错，防拼错）
    header = {}  # excel 列名 -> 工作表列号
    for col in range(1, ws.max_column + 1):
        name = ws.cell(row=1, column=col).value
        if name is None:
            continue
        name = str(name).strip()
        if name in excel_of:
            header[name] = col
        else:
            errors.append(f"{ws.title}!{get_column_letter(col)}1: 未知列 '{name}'（拼写错误？已知列: {', '.join(excel_of)}）")

    # 必需列存在性（row_name + required + required_by 涉及列 + compare 涉及列）
    need_cols = {row_name_key} | set(cfg.get("required", []))
    for by_col, mapping in (cfg.get("required_by") or {}).items():
        need_cols.add(by_col)
        for cols in mapping.values():
            need_cols.update(cols)
    for rule in (cfg.get("compare") or []):
        need_cols.update((rule["col"], rule["other"]))
    for col_name in need_cols:
        base = excel_name_of(schema, col_name)
        excel_names = [f"{base}{a}" for a in "XYZ"] if schema.get(col_name, [None])[0] == "vector" else [base]
        for en in excel_names:
            if en not in header:
                errors.append(f"{ws.title}: 缺少必需列 '{en}'")

    out_rows = []
    seen = set()
    for row in range(2, ws.max_row + 1):
        if all(ws.cell(row=row, column=c).value in (None, "") for c in range(1, ws.max_column + 1)):
            continue

        def cell(col_key):
            en = excel_name_of(schema, col_key)
            return ws.cell(row=row, column=header[en]).value if en in header else None

        def err(col_key, msg):
            en = excel_name_of(schema, col_key)
            errors.append(f"{cell_ref(ws.title, row, header.get(en, 1))}: {msg}")

        # 行名（唯一非空）
        status, row_name = check_value("str", cell(row_name_key), None, None)
        if status != "ok" or not row_name:
            errors.append(f"{cell_ref(ws.title, row, header.get(excel_name_of(schema, row_name_key), 1))}: 行名（{row_name_key}）不能为空")
            continue
        if row_name in seen:
            err(row_name_key, f"行名重复: {row_name}")
            continue
        seen.add(row_name)

        # 必填（vector 类型要求三个分量都非空）
        for col_name in cfg.get("required", []):
            if schema.get(col_name, [None])[0] == "vector":
                for axis in "XYZ":
                    en = f"{excel_name_of(schema, col_name)}{axis}"
                    v = ws.cell(row=row, column=header[en]).value if en in header else None
                    if v in (None, ""):
                        errors.append(f"{cell_ref(ws.title, row, header.get(en, 1))}: {en} 为必填项")
            elif cell(col_name) in (None, ""):
                err(col_name, f"{col_name} 为必填项")

        # 按列值条件必填（如 Type=Engine 的行必须填 Thrust）
        for by_col, mapping in (cfg.get("required_by") or {}).items():
            status, by_val = check_value("str", cell(by_col), None, None)
            if status == "ok" and by_val in mapping:
                for col_name in mapping[by_val]:
                    if cell(col_name) in (None, ""):
                        err(col_name, f"{col_name} 为必填项（{by_col}={by_val} 行）")

        # 逐列转值
        entry = {"Name": row_name}
        values = {}  # 属性名 -> 已校验的值（compare 用）
        for key, spec in schema.items():
            kind, p1, p2 = spec[0], spec[1], spec[2]  # 第 4 元素是 Excel 列名覆盖
            if key == row_name_key or kind == "aux":
                # aux 参与上面的校验/条件判定，但不导出
                if kind == "aux":
                    status, pv = check_value(kind, cell(key), p1, p2)
                    if status == "bad":
                        err(key, pv)
                continue
            if kind == "vector":
                base = excel_name_of(schema, key)
                axes = {}
                bad = False
                for axis in "XYZ":
                    en = f"{base}{axis}"
                    v = ws.cell(row=row, column=header[en]).value if en in header else None
                    if v in (None, ""):
                        continue
                    st, pv = check_value("num", v, p1, p2)
                    if st == "bad":
                        errors.append(f"{cell_ref(ws.title, row, header.get(en, 1))}: {pv}")
                        bad = True
                    else:
                        axes[axis] = normalize_num(pv)
                if not bad and axes:
                    entry[key] = {a: axes.get(a, 0) for a in "XYZ"}  # 缺失分量补 0
            else:
                st, pv = check_value(kind, cell(key), p1, p2)
                if st == "ok":
                    entry[key] = normalize_num(pv) if kind == "num" else pv
                    values[key] = pv
                elif st == "bad":
                    err(key, pv)

        # 跨字段比较（如 SpeedMultiplierMax >= SpeedMultiplierMin）
        for rule in (cfg.get("compare") or []):
            a, b = values.get(rule["col"]), values.get(rule["other"])
            if isinstance(a, (int, float)) and isinstance(b, (int, float)):
                if not COMPARE_OPS[rule["op"]](float(a), float(b)):
                    err(rule["col"], f"{rule['col']}（{a:g}）应 {rule['op']} {rule['other']}（{b:g}）")

        out_rows.append(entry)
    return out_rows, errors


# ---------------------------------------------------------------------------
# 推断兜底导出（未登记 schema 的 sheet）
# ---------------------------------------------------------------------------

def guess_cell(value):
    """单元格 -> JSON 值（通用类型推断）。返回 (status, value)；status ∈ ok/empty。"""
    if value is None or value == "":
        return "empty", None
    if isinstance(value, bool):
        return "ok", value
    if isinstance(value, (int, float)):
        return "ok", normalize_num(value)
    s = str(value).strip()
    if s.lower() in ("true", "false"):
        return "ok", s.lower() == "true"
    return "ok", s


def export_generic(ws):
    errors = []
    headers = []
    for col in range(1, ws.max_column + 1):
        name = ws.cell(row=1, column=col).value
        if name is not None and str(name).strip():
            headers.append((str(name).strip(), col))

    # <前缀>X/Y/Z 三列合并为向量
    vec_prefixes = []
    for name, _ in headers:
        if name.endswith(("X", "Y", "Z")) and len(name) > 1:
            pfx = name[:-1]
            if len({n[-1] for n, _ in headers if n[:-1] == pfx}) == 3 and pfx not in vec_prefixes:
                vec_prefixes.append(pfx)

    out_rows = []
    seen = set()
    for row in range(2, ws.max_row + 1):
        if all(ws.cell(row=row, column=c).value in (None, "") for c in range(1, ws.max_column + 1)):
            continue
        vals = {name: ws.cell(row=row, column=col).value for name, col in headers}
        name_val = str(vals.get("Name", "") or "").strip()
        if not name_val:
            errors.append(f"{ws.title}!A{row}: 首列 Name 为空（通用导出要求首列为行名）")
            continue
        if name_val in seen:
            errors.append(f"{ws.title}!A{row}: 行名重复: {name_val}")
            continue
        seen.add(name_val)

        entry = {"Name": name_val}
        vec_acc = {}
        for name, _ in headers:
            if name == "Name":
                continue
            status, v = guess_cell(vals.get(name))
            if status == "empty":
                continue
            pfx = name[:-1] if name[-1] in "XYZ" and name[:-1] in vec_prefixes else None
            if pfx is not None:
                vec_acc.setdefault(pfx, {})[name[-1]] = v
            else:
                entry[name] = v
        for pfx, axes in vec_acc.items():
            if len(axes) == 3:
                entry[pfx] = axes
        out_rows.append(entry)
    return out_rows, errors


# ---------------------------------------------------------------------------
# 入口
# ---------------------------------------------------------------------------

def main() -> int:
    config = json.loads(DEFAULT_CONFIG.read_text(encoding="utf-8"))
    xlsx_arg = sys.argv[1] if len(sys.argv) > 1 else None
    xlsx_path = (PROJECT_ROOT / xlsx_arg).resolve() if xlsx_arg else (PROJECT_ROOT / config["excel"]).resolve()
    stem = xlsx_path.stem  # 例：GuLiStrikeShip
    if not xlsx_path.exists():
        print(f"missing {xlsx_path}")
        return 1
    wb = load_workbook(xlsx_path, data_only=True)  # data_only: 读公式的缓存值

    all_errors = []
    written = []
    unconfigured = []
    for sheet in wb.sheetnames:
        cfg = config["sheets"].get(sheet)
        if cfg and cfg.get("schema"):
            out_rows, errors = export_configured(wb[sheet], cfg)
            strict = True
        else:
            out_rows, errors = export_generic(wb[sheet])
            strict = False
            unconfigured.append(sheet)
        all_errors += errors
        written.append((f"DT_{stem}_{sheet}", out_rows, strict))

    if all_errors:
        print(f"校验失败（{len(all_errors)} 处），未生成 JSON：")
        for e in all_errors:
            print("  -", e)
        return 1

    JSON_DIR.mkdir(parents=True, exist_ok=True)
    for table_name, out_rows, strict in written:
        out_path = JSON_DIR / f"{table_name}.json"
        out_path.write_text(json.dumps(out_rows, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"OK: {out_path} ({len(out_rows)} 行{'，配置校验' if strict else '，推断导出（未登记 schema）'})")
    if unconfigured:
        print(f"note: 以下 sheet 未登记 schema，走推断导出（建议在 tables.json 登记）: {', '.join(unconfigured)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
