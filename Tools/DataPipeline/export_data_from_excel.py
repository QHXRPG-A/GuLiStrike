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
import csv
import io
import math
import colorsys
import re
import sys
import tempfile
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

TYPES = {"int", "float", "bool", "str", "str[]", "softclass", "softobject"}
MARKS = {"Necessary", "Optional"}
IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

# C++ 类型/默认值映射（向量与标准三列另行处理）
CPP_OF = {
    "int": ("int32", "0"),
    "float": ("float", "0.0f"),
    "bool": ("bool", "false"),
    "str": ("FString", None),
    "str[]": ("TArray<FString>", None),
    "softclass": ("TSoftClassPtr<UObject>", None),
    "softobject": ("TSoftObjectPtr<UObject>", None),
}
VECTOR_CPP = ("FVector", "FVector::ZeroVector")


def cell_ref(sheet, row, col):
    return f"{sheet}!{openpyxl.utils.get_column_letter(col)}{row}"


class SheetError(Exception):
    pass


def load_schema(ws, allow_text_id=False):
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
    for std_name, std_type in (("id", "str" if allow_text_id else "int"), ("name", "str"), ("Note", "str")):
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
                "str": "", "str[]": [], "softclass": "", "softobject": ""}[col["type"]]
    t = col["type"]
    if t == "str[]":
        try:
            value = json.loads(raw) if isinstance(raw, str) else None
        except (ValueError, TypeError):
            value = None
        if not isinstance(value, list) or not all(isinstance(v, str) and v.strip() for v in value):
            raise SheetError(f"{where}: str[] 必须为 JSON 字符串列表")
        return value
    if t == "int":
        if isinstance(raw, bool) or not isinstance(raw, (int, float)) or float(raw) != int(raw):
            raise SheetError(f"{where}: int 列 '{col['name']}' 的值不是整数: {raw!r}")
        if col['name'].endswith('VfxId') and col['necessary'] and raw <= 0:
            raise SheetError(f"{where}: 必需特效 {col['name']} 必须引用正整数 ID")
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


def export_sheet(ws, allow_text_id=False):
    """一个 sheet -> (json 行列表, 结构属性列表)。校验失败抛 SheetError。"""
    cols, vec_prefixes = load_schema(ws, allow_text_id)
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


def export_game_texts(ws):
    """Game copy has three author-facing columns; TextId is also the stable UE row name."""
    if ws.title != "Texts":
        raise SheetError("游戏文本表仅接受 Texts 工作表")
    expected = [("文本id", "str", "Necessary"), ("介绍", "str", "Necessary"),
                ("内容", "str", "Necessary")]
    if ws.max_column != 3:
        raise SheetError("游戏文本表必须为三列：文本id、介绍、内容")
    for column, metadata in enumerate(expected, 1):
        if tuple(ws.cell(row, column).value for row in range(1, 4)) != metadata:
            raise SheetError(f"{cell_ref(ws.title, 1, column)}: 元数据必须为 {metadata}")
    rows, seen = [], set()
    for index in range(4, ws.max_row + 1):
        if all(ws.cell(index, c).value is None for c in range(1, 4)):
            continue
        values = [check_value(ws.title, {"col": c, "name": expected[c-1][0],
                  "type": "str", "necessary": True}, index, ws.cell(index, c).value) for c in range(1, 4)]
        ident, introduction, content = values
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_.]*", ident):
            raise SheetError(f"{cell_ref(ws.title,index,1)}: 文本id必须为稳定英文标识，可含点和下划线")
        if ident.casefold() in seen:
            raise SheetError(f"{cell_ref(ws.title,index,1)}: 文本id重复（UE行名不区分大小写）: {ident}")
        seen.add(ident.casefold())
        # Preserve intentional whitespace/newlines in copy; only IDs/notes are normalized.
        content = str(ws.cell(index, 3).value)
        rows.append({"Name": ident, "TextId": ident, "Introduction": introduction, "Content": content})
    props = [{"prop": p, "cpp": "FString", "default": None, "comment": f"{label} (str, Necessary)"}
             for p, label in zip(("TextId", "Introduction", "Content"), ("文本id", "介绍", "内容"))]
    return rows, props


def validate_game_text_references(tables):
    table = tables.get("DT_GuLiStrikeGameTexts_Texts")
    if not table:
        raise SheetError("缺少 GuLiStrikeGameTexts.xlsx / Texts")
    available = {row["TextId"] for row in table["rows"]}
    used = set()
    # Source-only traversal: never inspect binary assets, caches or build products.
    for source in (PROJECT / "Source/GuLiStrike").rglob("*.cpp"):
        for ident in re.findall(r'GuLiGameText::(?:Text|Get|Format)\(TEXT\("([A-Za-z0-9_.]+)"\)',
                                source.read_text(encoding="utf-8-sig")):
            used.add(ident)
    missing = used - available
    if missing:
        raise SheetError(f"原生 UI 引用缺少游戏文本: {sorted(missing)}")


def validate_rogue_card_text_styles(tables):
    table_name = 'DT_GuLiStrikeRogueCardUI_TextStyles'
    if table_name not in tables:
        return
    entry = tables[table_name]
    styles = {row['Name']: row for row in entry['rows']}
    if 'Default' not in styles:
        raise SheetError('GuLiStrikeRogueCardUI.xlsx::TextStyles 缺少Default样式')
    for row in styles.values():
        where = f"GuLiStrikeRogueCardUI.xlsx::TextStyles 行{entry['row_locations'][str(row['Id'])]} 样式{row['Name']}"
        if row['FontSize'] <= 0 or row['OutlineSize'] < 0:
            raise SheetError(f'{where}: FontSize必须为正整数，OutlineSize必须非负')
        for field in ('ColorSRGB', 'OutlineColorSRGB'):
            if not re.fullmatch(r'#[0-9A-Fa-f]{6}', row[field]):
                raise SheetError(f'{where}: {field}必须为#RRGGBB')
        if not row['FontAsset'].startswith(('/Engine/', '/Game/')) or '.' not in row['FontAsset'].rsplit('/', 1)[-1]:
            raise SheetError(f'{where}: FontAsset必须使用完整字体资产路径')
    text_rows = tables.get('DT_GuLiStrikeGameTexts_Texts', {}).get('rows', [])
    texts = {row['TextId']: row['Content'] for row in text_rows}
    for card in tables.get('DT_GuLiStrikeRogueCards_Cards', {}).get('rows', []):
        key = card['TextIds'][1]
        pattern = texts.get(key, '')
        if any(character in pattern for character in '\r\n'):
            raise SheetError(f'卡牌{card["Id"]}的{key}必须为单行说明')
        tags = re.findall(r'<([^/>]+)>', pattern)
        missing = set(tags) - set(styles)
        if missing or pattern.count('</>') != len(tags):
            raise SheetError(f'卡牌{card["Id"]}的{key}引用未知或未闭合富文本样式: {sorted(missing)}')


def validate_missile_guidance_capacity(tables):
    """Keep the existing integer upgrade source within the whole-salvo guidance limit."""
    skills = tables.get('DT_GuLiStrikeSecondaryUnitSkills_Skills', {}).get('rows', [])
    cards = tables.get('DT_GuLiStrikeRogueCards_Cards', {}).get('rows', [])
    for row in skills:
        limit = row.get('MaxProjectilesPerActivation', 0)
        where = f"GuLiStrikeSecondaryUnitSkills.xlsx / Skills {row['Name']}"
        if not 0 <= limit <= 2147483647:
            raise SheetError(f'{where}: MaxProjectilesPerActivation必须为非负int32，0表示不限')
        if row['ExecutorClass'].rsplit('.', 1)[-1] != 'GuLiWarMachineMissileSkillExecutor':
            continue
        if limit < 1 or row.get('SourceWeaponSlot') != 'MissileLauncher' \
                or row.get('TargetAreaDiameterCentimeters', 0) <= 0 or not row.get('GroundWarningStyle'):
            raise SheetError(f'{where}: 重防号导弹必须配置正数容量、MissileLauncher及共享预警范围/样式')
        maximum_salvo = 1  # Existing WM01 baseline; integer sources add to it.
        for card in cards:
            if card['UnitTypeId'] == 2 and card['ImplementationClass'].endswith('.GuLiRogueCardMissileCountEffect'):
                if card['MaxAcquisitions'] < 1:
                    raise SheetError(f"GuLiStrikeRogueCards.xlsx / Cards {card['Id']}: 弹量卡必须有限次，避免整台齐射超过引导容量")
                maximum_salvo += card['MaxAcquisitions'] * card['BonusCount']
        if maximum_salvo > limit:
            raise SheetError(f'{where}: 基础1发与弹量卡累计上限{maximum_salvo}超过引导容量{limit}')


def validate_rogue_cards(tables):
    entry = tables.get("DT_GuLiStrikeRogueCards_Cards")
    if entry is None:
        return
    texts = {r["TextId"] for r in tables["DT_GuLiStrikeGameTexts_Texts"]["rows"]}
    units = {r["Id"] for r in tables["DT_GuLiStrikeCommander_Soldiers"]["rows"]}
    cards = {row["Id"]: row for row in entry["rows"]}
    def fail(row, message):
        line = entry.get("row_locations", {}).get(row["Id"], "?")
        raise SheetError(f"GuLiStrikeRogueCards.xlsx / Cards 行{line} 卡ID={row['Id']} ({row['Name']}): {message}")

    percent_effects = {"GuLiRogueCardFireRateEffect", "GuLiRogueCardMoveSpeedEffect", "GuLiRogueCardMissileDamageEffect"}
    for row in entry["rows"]:
        if not re.fullmatch(r"[0-9]{2}\.[0-9]{2}", row["Id"]) or row["Id"].endswith(".00"):
            raise SheetError(f"卡牌 {row['Name']} 的 id 必须为文本 卡族.两位等级，例如 01.01")
        if row["Type"] not in range(1, 5) or row["UnitTypeId"] not in units:
            raise SheetError(f"卡牌 {row['Name']} 的类型或目标兵种无效")
        if len(row["TextIds"]) != 2 or any(t not in texts for t in row["TextIds"]):
            raise SheetError(f"卡牌 {row['Name']} 必须引用两项已存在的文本，顺序为标题、说明")
        effect = row["ImplementationClass"].rsplit(".", 1)[-1]
        percent, count = row["BonusPercent"], row["BonusCount"]
        if not math.isfinite(percent) or row["MaxAcquisitions"] < 0:
            fail(row, "BonusPercent必须有限；MaxAcquisitions必须为非负整数，0表示无限")
        if effect in percent_effects:
            if percent <= 0 or count != 0:
                fail(row, "百分比效果要求BonusPercent>0且BonusCount=0")
        elif effect == "GuLiRogueCardMissileCountEffect":
            if count <= 0 or percent != 0:
                fail(row, "弹量效果要求BonusCount为正整数且BonusPercent=0")
        elif effect == "GuLiRogueCardMissilePodEffect":
            if percent != 0 or count != 0 or row["MaxAcquisitions"] != 1:
                fail(row, "解锁效果要求两项增量均为0且MaxAcquisitions=1")
        else:
            fail(row, f"效果类 {effect} 尚未登记数值契约")
        for key in ("RequiredCardIds", "ExcludedCardIds"):
            refs = row[key]
            if len(set(refs)) != len(refs):
                fail(row, f"{key}含重复引用")
            if row["Id"] in refs:
                fail(row, f"{key}不能引用自身")
            missing = set(refs) - cards.keys()
            if missing:
                fail(row, f"{key}引用未知卡: {sorted(missing)}")
        for key in ("ImplementationClass", "FrontMaterial", "UpgradeVfx"):
            if not row[key].startswith(("/Script/", "/Game/")) or "." not in row[key].rsplit("/", 1)[-1]:
                raise SheetError(f"卡牌 {row['Name']}.{key} 必须为完整对象路径")
        if not row['UpgradeVfx'].startswith('/Game/'):
            raise SheetError(f"卡牌 {row['Name']} 的升级特效必须为项目Niagara资产")
        if not math.isfinite(row['UpgradeVfxScale']) or row['UpgradeVfxScale'] <= 0:
            raise SheetError(f"卡牌 {row['Name']} 的升级特效缩放必须为有限正数")
        number = r'[+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?'
        color = re.fullmatch(r'\(\s*R=(' + number + r')\s*,\s*G=(' + number +
                             r')\s*,\s*B=(' + number + r')\s*,\s*A=(' + number + r')\s*\)', row['UpgradeVfxColor'])
        values = [float(v) for v in color.groups()] if color else []
        if not values or not all(math.isfinite(v) for v in values) or not 0 <= values[3] <= 1:
            raise SheetError(f"卡牌 {row['Name']} 的升级特效颜色须为线性HDR (R=...,G=...,B=...,A=...)，alpha在0~1")

    # An exclusion authored on either endpoint applies symmetrically. Every
    # prerequisite closure, including its owner, must be simultaneously obtainable.
    excluded = {ident: set(row["ExcludedCardIds"]) for ident, row in cards.items()}
    for ident, row in cards.items():
        for other in row["ExcludedCardIds"]:
            excluded[other].add(ident)
    closures = {}
    visiting = []
    def closure(ident):
        if ident in visiting:
            fail(cards[ident], "依赖环: " + " -> ".join(visiting[visiting.index(ident):] + [ident]))
        if ident in closures:
            return closures[ident]
        visiting.append(ident)
        result = {ident}
        for required in cards[ident]["RequiredCardIds"]:
            result.update(closure(required))
        visiting.pop()
        for member in sorted(result):
            conflict = result & excluded[member]
            if conflict:
                fail(cards[ident], f"依赖链内部互斥（含卡牌自身）: {member} 与 {sorted(conflict)}")
        closures[ident] = result
        return result
    for ident in cards:
        closure(ident)


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
            display = ', meta=(DisplayName="描述", ToolTip="模型的用途及所属玩法或装配")' if stem == 'GuLiStrikeModels' and sheet == 'Models' and p['prop'] == 'Description' else ''
            lines.append(f'\tUPROPERTY(EditAnywhere, BlueprintReadOnly, Category="{sheet}"{display})')
            lines.append(f"\t{p['cpp']} {p['prop']}{init};")
            lines.append("")
        lines += ["};", ""]
    return "\n".join(lines).rstrip() + "\n"


def write_if_changed(path, text):
    """内容不变不写（保持 mtime，避免无谓重编译）。"""
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    # The running editor/build tools can memory-map generated headers on Windows.
    # Replace a complete file instead of truncating that mapped file in place.
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n",
                                     dir=path.parent, suffix=".tmp", delete=False) as output:
        temporary = Path(output.name)
        output.write(text)
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)
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
    models = {row['Id']: row for row in tables.get('DT_GuLiStrikeModels_Models', {}).get('rows', [])}
    fields = {row["Id"]: row for row in tables[SPELL_FIELD_TABLE]["rows"]}
    for row in buildings.values():
        label = f"Buildings/{row['Name']}"
        category = row["Category"]
        if row["Id"] <= 0 or category not in range(7) or row["PlacementType"] not in range(8):
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
            if not unit or unit.get("ActorClass") or models.get(unit.get('ModelId'), {}).get('ResourceType') != 'StaticMesh' \
                    or unit.get("bConstructionOnly") \
                    or row["ProductionSeconds"] <= 0 or row["ProductionCount"] <= 0:
                raise SheetError(f"{label}: barracks require a Mass unit, positive period and count")
        if category == 6:
            unit = soldiers.get(row.get("CompletionUnitTypeId", 0))
            if not unit or not unit.get("bConstructionOnly") or unit.get("bSummonOnly") or unit.get("ActorClass"):
                raise SheetError(f"{label}: constructed unit requires a construction-only Mass definition")
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
        transit = fields.get(row["TransitFieldId"])
        if row["TransitFieldId"] and (not transit or transit["FieldType"] != "StrongholdTransit"):
            raise SheetError(f"{label}: TransitFieldId must reference a StrongholdTransit")
        if category == 5 and (not gift_ids or not transit):
            raise SheetError(f"{label}: strongholds require gift IDs and transit configuration")
    for row in fields.values():
        if row["FieldType"] in ("Combat", "Teleport") and row["RadiusCentimeters"] <= 0:
            raise SheetError(f"Fields/{row['Name']}: combat and teleport fields require a positive radius")
        if row["FieldType"] != "StrongholdTransit":
            continue
        required = ("LaneHeightCentimeters", "AscentSeconds",
                    "AccelerationSeconds", "DecelerationSeconds", "ExitFlashSeconds",
                    "SpeedMultiplier", "ExitRadiusCentimeters")
        if any(row[key] <= 0 for key in required):
            raise SheetError(f"Fields/{row['Name']}: invalid airborne transit configuration")


def validate_commander_state_trees(tables):
    entry = tables.get("DT_GuLiStrikeCommander_Soldiers")
    if entry is None:
        return
    for row in entry["rows"]:
        asset = row.get("StateTreeAsset", "")
        if not re.fullmatch(r"/Game/(?:[A-Za-z0-9_]+/)*([A-Za-z0-9_]+)\.\1", asset):
            raise SheetError(f"Soldiers/{row['Name']}: StateTreeAsset requires a /Game/Path/Asset.Asset object path")


def validate_vfx_references(tables):
    """Run before writing any output, including unrelated tables and headers."""
    effects = tables.get('DT_GuLiStrikeVfx_Effects')
    if not effects:
        raise SheetError('缺少 GuLiStrikeVfx.xlsx / Effects')
    ids, definitions = set(), set()
    for row in effects['rows']:
        label = 'Effects/' + row['Name']
        id = row['Id']
        path = row.get('ResourcePath', '')
        scale = row.get('Scale', {})
        if type(id) is not int or not 0 < id <= 2147483647 or id in ids:
            raise SheetError(f'{label}: 特效 ID 必须是唯一正整数')
        if not re.fullmatch(r'/(?:Game|Engine|[A-Za-z][A-Za-z0-9_]*)/[A-Za-z0-9_/]+\.[A-Za-z0-9_]+', path):
            raise SheetError(f'{label}: ResourcePath 必须是完整资源对象路径')
        if not IDENT_RE.fullmatch(row['Name']):
            raise SheetError(f'{label}: 特效行名必须是稳定的 C++ 标识符，以生成符号 ID')
        if set(scale) != {'X', 'Y', 'Z'} or not all(math.isfinite(v) and v > 0 for v in scale.values()):
            raise SheetError(f'{label}: 基础缩放必须为三个有限正数')
        key = (path.casefold(), *(scale[k] for k in 'XYZ'))
        if key in definitions:
            raise SheetError(f'{label}: 重复的资源路径与基础缩放组合')
        ids.add(id)
        definitions.add(key)
    for line in (PROJECT/'Config/DefaultGame.ini').read_text(encoding='utf-8-sig').splitlines():
        field, _, value = line.partition('=')
        if field.lstrip('+').endswith(('VfxId', 'VfxIds')):
            try:
                reference = int(value.strip())
            except ValueError:
                raise SheetError(f'DefaultGame.ini {field}: 非整数特效 ID')
            if reference < 0 or reference and reference not in ids:
                raise SheetError(f'DefaultGame.ini {field}: 悬空特效 ID {reference}')
    for table, entry in tables.items():
        for row in entry['rows']:
            for field, value in row.items():
                if field.endswith('VfxId') and (type(value) is not int or value < 0 or value and value not in ids):
                    raise SheetError(f'{table}/{row["Name"]}.{field}: 悬空或非法特效 ID {value}')
            required = []
            if table == 'DT_GuLiStrikeMech_Skills':
                required = ['JetVfxId', 'FuelBarVfxId'] if row.get('ExecutionType') == 'GAS' else ['BulletVfxId', 'MuzzleVfxId']
            elif table == 'DT_GuLiStrikeSpellFields_Fields' and row.get('FieldType') == 'StrongholdTransit':
                required = ['EnergyVfxId', 'TrailVfxId', 'FlashVfxId']
            for field in required:
                if row.get(field, 0) not in ids:
                    raise SheetError(f'{table}/{row["Name"]}.{field}: 必需特效未配置')


def validate_projectile_visual_profiles(tables):
    """Zero/empty is allowed for legacy projectiles; a configured profile must be complete."""
    fields = ('SmokeInitialWidthCentimeters', 'SmokeMaximumWidthCentimeters',
              'FlameWidthCentimeters', 'FlameLengthCentimeters')
    for row in tables.get('DT_GuLiStrikeSecondaryWeapons_Projectiles', {}).get('rows', []):
        values = [row.get(field, 0) for field in fields]
        if row['Name'] != 'WM01_Missile' and not any(values):
            continue
        if not all(isinstance(v, (int, float)) and math.isfinite(v) and v > 0 for v in values) \
                or values[1] < values[0]:
            raise SheetError(f"Projectiles/{row['Name']}: 烟宽/尾焰尺寸必须为有限正数，烟宽上限不得小于初始宽")


def validate_model_references(tables):
    """Global model foreign keys and material contracts, before touching any output."""
    catalog = tables.get('DT_GuLiStrikeModels_Models', {}).get('rows', [])
    ids = {r['Id'] for r in catalog}
    if not catalog or len(ids) != len(catalog) or any(i <= 0 for i in ids):
        raise SheetError('Models 模型 ID 必须为唯一正整数')
    for row in catalog:
        if not str(row.get('Description', '')).strip():
            raise SheetError(f"Models/{row['Name']}: 描述不能为空，需说明模型用途")
        if not IDENT_RE.fullmatch(row['Name']):
            raise SheetError(f"Models/{row['Name']}: name 用于稳定 C++ 常量，必须为合法标识符")
        if row['ResourceType'] not in {'StaticMesh', 'SkeletalMesh', 'PresentationClass'}:
            raise SheetError(f"Models/{row['Name']}: 无效资源类型")
        for key in ('ResourcePath', 'CandidateResourcePath'):
            path = row.get(key, '')
            if not path and key.startswith('Candidate'): continue
            if not path.startswith(('/Game/', '/Engine/')) or '.' not in path.rsplit('/', 1)[-1]:
                raise SheetError(f"Models/{row['Name']}.{key}: 必须为完整 UE 软引用")
            if (row['ResourceType'] == 'PresentationClass') != path.endswith('_C'):
                raise SheetError(f"Models/{row['Name']}.{key}: 视觉类必须用 _C，网格不得使用 _C")
        for key in ('BluePrimaryHex', 'BlueSecondaryHex', 'EnemyPrimaryHex', 'EnemySecondaryHex'):
            if not re.fullmatch(r'#[0-9A-Fa-f]{6}', row[key]):
                raise SheetError(f"Models/{row['Name']}.{key}: 必须为 #RRGGBB")
        if not IDENT_RE.fullmatch(row['Name']) or row['Name'] in {'class','struct','auto','int','float','bool','return','default','namespace','new','delete','const','static'}:
            raise SheetError(f"Models/{row['Name']}: name 必须可用作稳定 C++ 模型常量，显示名单独填写")
        if row['bTeamColorEnabled'] and (row['EnemyPrimaryHex'].lower() == row['BluePrimaryHex'].lower()
                                      or row['EnemySecondaryHex'].lower() == row['BlueSecondaryHex'].lower()):
            raise SheetError(f"Models/{row['Name']}: 敌方队色不得与蓝方相同")
        if row['bTeamColorEnabled']:
            for key in ('EnemyPrimaryHex','EnemySecondaryHex'):
                value=row[key][1:]
                rgb=[int(value[i:i+2],16)/255.0 for i in (0,2,4)]
                hue,saturation,_=colorsys.rgb_to_hsv(*rgb)
                if 190<=hue*360<=255 and saturation>.2:
                    raise SheetError(f"Models/{row['Name']}.{key}: 敌方使用非蓝配色，不能配置蓝色系")
    ledger = PROJECT / 'Data/Models/published-model-ids.json'
    if ledger.exists():
        published = json.loads(ledger.read_text(encoding='utf-8'))
        current = {r['Name']: r['Id'] for r in catalog}
        for name, identity in published.items():
            if current.get(name) != identity:
                raise SheetError(f'Models/{name}: 发布 ID {identity} 不得删除、重排或复用')
    for table, entry in tables.items():
        for row in entry['rows']:
            for key, value in row.items():
                if key.endswith('ModelId') and value != 0 and value not in ids:
                    raise SheetError(f"{table}/{row['Name']}.{key}: 悬空模型 ID {value}")
            if table in ('DT_GuLiStrikeCommander_Soldiers', 'DT_GuLiStrikeBuildings_Buildings', 'DT_GuLiStrikeShip_Parts', 'DT_GuLiStrikeMech_Visuals') and row.get('ModelId', 0) <= 0:
                raise SheetError(f"{table}/{row['Name']}: 缺少 ModelId")
    parts = tables['DT_GuLiStrikeModels_Parts']['rows']
    for table, field in [('DT_GuLiStrikeShip_Tuning','HullModelId')]:
        if any(r.get(field,0) <= 0 for r in tables[table]['rows']):
            raise SheetError(f'{table}: 缺少必需模型 ID {field}')
    part_keys = {(r['ModelId'], r['PartKey']) for r in parts}
    if len(part_keys) != len(parts): raise SheetError('Parts: 重复部件键')
    edges = {i: [] for i in ids}
    for row in parts:
        if row['ChildModelId']: edges[row['ModelId']].append(row['ChildModelId'])
    def visit(node, stack):
        if node in stack: raise SheetError(f'Parts: 子模型循环引用 {stack + [node]}')
        for child in edges[node]: visit(child, stack + [node])
    for node in ids: visit(node, [])
    seen = set()
    for row in tables['DT_GuLiStrikeModels_MaterialParameters']['rows']:
        key = tuple(row[k] for k in ('ModelId', 'PartKey', 'MaterialSlotName', 'ParameterKey', 'Scope'))
        if key in seen: raise SheetError(f'MaterialParameters: 重复绑定 {key}')
        seen.add(key)
        if row['ParameterType'] not in {'Vector', 'Scalar'} or row['Driver'] not in {'MID', 'CPD'} or row['Scope'] not in {'Existing', 'Candidate'}:
            raise SheetError(f'MaterialParameters: 无效参数契约 {key}')
        if row['PartKey'] != 'Root' and (row['ModelId'], row['PartKey']) not in part_keys:
            raise SheetError(f'MaterialParameters: 缺失部件 {key}')
        if row['bTeamManaged'] and row['bRuntimeWritable']:
            raise SheetError(f'MaterialParameters: 阵营参数不得开放任意写入 {key}')
        if row['Driver'] == 'CPD':
            reserved = {'TeamPrimary': (8, 'Vector'), 'TeamSecondary': (12, 'Vector'),
                        'TeamEnabled': (16, 'Scalar'), 'TeamLightStrength': (17, 'Scalar')}
            if reserved.get(row['ParameterKey']) != (row['CustomDataIndex'], row['ParameterType']):
                raise SheetError(f'MaterialParameters: CPD 索引冲突或类型错误 {key}')
    seen = set()
    for row in tables['DT_GuLiStrikeModels_ColorRegions']['rows']:
        key = (row['ModelId'], row['PartKey'], row['RegionKey'])
        if key in seen: raise SheetError(f'ColorRegions: 重复区域 {key}')
        seen.add(key)
        if row['PartKey'] != 'Root' and (row['ModelId'],row['PartKey']) not in part_keys:
            raise SheetError(f'ColorRegions: 缺失部件 {key}')
        if row['PaintRole'] not in range(8) or row['MaskId'] != row['PaintRole'] or row['Scope'] not in {'Existing', 'Candidate'}:
            raise SheetError(f'ColorRegions: 无效区域编码 {key}')


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
                is_mech = stem == "GuLiStrikeMech"
                if is_mech and ws.title not in ("升级表", "技能表", "Visuals"):
                    raise SheetError("机甲表仅接受升级表、技能表和 Visuals")
                rows, props = export_game_texts(ws) if stem == "GuLiStrikeGameTexts" else \
                    export_sheet(ws, allow_text_id=(is_mech and ws.title == "升级表") or stem == "GuLiStrikeRogueCards")
                if consolidated and stem != SECONDARY_WORKBOOK:
                    retired = {(s, t) for s, t in SECONDARY_TABLE_IDENTITIES.values()
                               if s != "GuLiStrikeSpellFields"}
                    if (stem, ws.title) in retired:
                        raise SheetError("此武器工作表已迁至 GuLiStrikeSecondaryWeapons.xlsx；禁止重复维护")
                identity_stem, identity_sheet = SECONDARY_TABLE_IDENTITIES.get(ws.title, (stem, ws.title)) \
                    if stem == SECONDARY_WORKBOOK else (stem, ws.title)
                if is_mech:
                    identity_sheet = {"升级表": "Upgrades", "技能表": "Skills", "Visuals": "Visuals"}[ws.title]
                if stem == "GuLiStrikeCommander" and ws.title == "Camera":
                    identity_sheet = "Camera"
                    if len(rows) != 1 or rows[0]['Name'] != 'Default' or rows[0]['Id'] != 1:
                        raise SheetError('Camera必须有唯一的 1 / Default 配置行')
                    camera = rows[0]
                    if not (0 < camera['MinimumHeightMeters'] < camera['TacticalStartHeightMeters']
                            < camera['TacticalMaximumHeightMeters']
                            and camera['MinimumHeightMeters'] <= camera['InitialHeightMeters'] <= camera['TacticalMaximumHeightMeters']):
                        raise SheetError('镜头高度必须满足 0 < 最低 < 战术起点 < 战术上限，初始高度在范围内')
                    if not (0 < camera['NearPitchDegrees'] < camera['TacticalPitchDegrees'] < 90
                            and camera['OverviewPitchDegrees'] == 90 and 10 <= camera['FieldOfViewDegrees'] <= 120
                            and -180 <= camera.get('OverviewYawDegrees', float('nan')) <= 180
                            and camera['ZoomStepMultiplier'] > 1 and 0 <= camera['OverviewPaddingFraction'] < 0.4):
                        raise SheetError('镜头角度、FOV、缩放倍率或总览留边无效')
                    for key, value in camera.items():
                        if key not in ('Name', 'Id', 'Note', 'OverviewPaddingFraction', 'OverviewYawDegrees') and (not isinstance(value, (float, int)) or not math.isfinite(value) or value <= 0):
                            raise SheetError(f'镜头参数 {key} 必须是有限正数')
                    if camera['MinimumMoveMetersPerSecond'] > camera['MaximumMoveMetersPerSecond']:
                        raise SheetError('镜头最小移动速度不得大于最大移动速度')
                table = f"DT_{identity_stem}_{identity_sheet}"
                source = {"excel": wb_path.name, "sheet": ws.title}
                if table in tables:
                    raise SheetError(f"重复的DataTable身份: {table}；每张表必须只有一个维护入口")
                else:
                    tables[table] = {"stem": identity_stem, "identity_sheet": identity_sheet,
                                     "props": props, "rows": rows, "sources": [source]}
                    if stem in {"GuLiStrikeRogueCards", "GuLiStrikeRogueCardUI"}:
                        id_column = next(c.column for c in ws[1] if c.value == "id")
                        tables[table]["row_locations"] = {str(ws.cell(i, id_column).value): i
                            for i in range(4, ws.max_row + 1) if ws.cell(i, id_column).value is not None}
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
            validate_commander_state_trees(tables)
            validate_game_text_references(tables)
            validate_rogue_cards(tables)
            validate_missile_guidance_capacity(tables)
            validate_rogue_card_text_styles(tables)
            validate_vfx_references(tables)
            validate_projectile_visual_profiles(tables)
            validate_model_references(tables)
            if any(name.startswith('DT_GuLiStrikeMech_') for name in tables):
                if not all(name in tables for name in ('DT_GuLiStrikeMech_Upgrades','DT_GuLiStrikeMech_Skills')):
                    raise SheetError('GuLiStrikeMech.xlsx必须同时包含升级表与技能表')
                skills = {row['Id'] for row in tables['DT_GuLiStrikeMech_Skills']['rows']}
                for row in tables['DT_GuLiStrikeMech_Upgrades']['rows']:
                    if not re.fullmatch(r'[1-9][0-9]*\.[1-9][0-9]*', row['Id']) \
                            or row['SkillId'] not in skills or row['Level'] <= 0 \
                            or not (0 < row['FireRate'] <= 30) or not math.isfinite(row['Damage']) or row['Damage'] <= 0:
                        raise SheetError(f"机甲升级行 {row['Name']} 的ID、技能引用或数值无效")
                for row in tables['DT_GuLiStrikeMech_Skills']['rows']:
                    execution = row.get('ExecutionType', 'Weapon')
                    if execution == 'GAS':
                        positive = ('MaxFuel', 'FuelDrainPerSecond', 'FuelRecoveryPerSecond',
                                    'ThrustAcceleration', 'MaxRiseSpeed',
                                    'FuelBarHeight', 'FuelBarRightOffset', 'FuelBarFadeSeconds', 'AirSpeedMultiplier',
                                    'FallGravityMultiplier')
                        if not all(math.isfinite(row.get(key, 0)) and row.get(key, 0) > 0 for key in positive) \
                                or not math.isfinite(row.get('InitialFuel', -1)) \
                                or not 0 <= row.get('InitialFuel', -1) <= row['MaxFuel'] \
                                or not math.isfinite(row.get('FuelRecoveryDelay', -1)) \
                                or row.get('FuelRecoveryDelay', -1) < 0 \
                                or not math.isfinite(row.get('JetPitchDegrees', float('nan'))) \
                                or not math.isfinite(row.get('JetMaxTiltDegrees', float('nan'))) \
                                or not 0 <= row.get('JetMaxTiltDegrees', -1) <= 15:
                            raise SheetError(f"机甲GAS技能 {row['Name']} 的容量、推进或显示参数无效")
                        for key in ('AbilityClass',):
                            path = row.get(key, '')
                            if not path.startswith(('/Game/', '/Script/')) or '.' not in path.rsplit('/', 1)[-1]:
                                raise SheetError(f"机甲GAS技能 {row['Name']}.{key} 必须使用完整对象路径")
                        if not row.get('JetSocketLeft') or not row.get('JetSocketRight'):
                            raise SheetError(f"机甲GAS技能 {row['Name']} 缺少左右喷口")
                        continue
                    if execution != 'Weapon':
                        raise SheetError(f"机甲技能 {row['Name']} 不支持 ExecutionType={execution}")
                    if not all(math.isfinite(row[key]) and row[key] > 0 for key in ('ProjectileSpeed','ProjectileLifetime','SweepRadius','RecoilDuration')):
                        raise SheetError(f"机甲技能行 {row['Name']} 的弹丸或后坐参数无效")
                    aim_radius = row.get('AimAssistRadiusCentimeters', 0)
                    if not math.isfinite(aim_radius) or aim_radius < 0 \
                            or (row.get('AimAssistEnabled', False) and aim_radius <= 0):
                        raise SheetError(f"机甲技能行 {row['Name']} 的辅助瞄准半径必须有限且非负，启用时须大于0cm")
                    for key in ('RecoilCurve',):
                        if '.' not in row[key].rsplit('/',1)[-1]:
                            raise SheetError(f"机甲技能 {row['Name']}.{key} 必须使用完整资产对象路径（包名.对象名）")
        except SheetError as e:
            print(f"error: {e}", file=sys.stderr)
            failed = True
    if failed:
        print("导出失败，JSON、头文件和manifest均未更新", file=sys.stderr)
        sys.exit(1)
    if '--validate-only' in sys.argv:
        print(f"Validated {len(tables)} tables; no files written.")
        return
    headers = {}
    for table, entry in tables.items():
        rows = entry["rows"]
        if len(entry["sources"]) > 1:
            rows.sort(key=lambda row: row["Id"])
        json_path = JSON_DIR / f"{table}.json"
        write_if_changed(json_path, json.dumps(rows, ensure_ascii=False, indent=2))
        columns = list(rows[0]) if rows else ['Name']
        def csv_value(value):
            if isinstance(value,bool): return 'True' if value else 'False'
            if isinstance(value,dict): return '(X={X},Y={Y},Z={Z})'.format(**value)
            if isinstance(value,list): return '(' + ','.join(json.dumps(v,ensure_ascii=False) for v in value) + ')'
            return str(value)
        csv_buffer=io.StringIO(); csv_writer=csv.writer(csv_buffer,lineterminator='\n')
        csv_writer.writerow(columns)
        for row in rows: csv_writer.writerow([csv_value(row.get(c,'')) for c in columns])
        write_if_changed(json_path.with_suffix('.csv'),csv_buffer.getvalue())
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
    # Stable symbolic IDs for native call sites; never duplicate paths or scales in C++.
    effect_ids = ['// Generated from GuLiStrikeVfx.xlsx / Effects. Do not edit.', '#pragma once',
                  '#include "CoreTypes.h"', 'namespace GuLiVfxIds', '{']
    for row in tables['DT_GuLiStrikeVfx_Effects']['rows']:
        effect_ids.append(f'\tinline constexpr int32 {row["Name"]} = {row["Id"]};')
    effect_ids.extend(['}', ''])
    write_if_changed(GEN_HEADER_DIR / 'GuLiVfxIds.h', '\n'.join(effect_ids))
    model_ids = ['// Generated from GuLiStrikeModels.xlsx / Models. Do not edit.', '#pragma once',
                 '#include "CoreTypes.h"', 'namespace GuLiModelIds', '{']
    for row in tables['DT_GuLiStrikeModels_Models']['rows']:
        model_ids.append(f'\tinline constexpr int32 {row["Name"]} = {row["Id"]};')
    model_ids.extend(['}', ''])
    write_if_changed(GEN_HEADER_DIR / 'GuLiModelIds.h', '\n'.join(model_ids))
    # Appending a new ID during export publishes it; validate-only never changes the ledger.
    ledger_path=PROJECT/'Data/Models/published-model-ids.json'
    ledger_path.parent.mkdir(parents=True,exist_ok=True)
    write_if_changed(ledger_path,json.dumps({r['Name']:r['Id'] for r in tables['DT_GuLiStrikeModels_Models']['rows']},ensure_ascii=False,indent=2))
    print(f"manifest: {JSON_DIR / 'manifest.json'}（{len(manifest['tables'])} 表）")


if __name__ == "__main__":
    main()
