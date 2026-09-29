"""Author the initial Cards workbook and append shared text through excelize-cli.

Run once for initial content. Existing Cards workbooks are never overwritten.
"""
import json
import subprocess
import tempfile
from pathlib import Path
from author_rogue_card_text import DESCRIPTION_PATTERNS

ROOT = Path(__file__).resolve().parents[2]
CLI = r"D:\UE5.7\excelize-cli\bin\xlsx.exe"


def xlsx(*args):
    return subprocess.check_output([CLI, *map(str, args)], encoding="utf-8")


def write(path, sheet, rows, start=1):
    cells = [{"cell": f"{chr(65+c)}{r}", "value": value,
              "type": "string" if isinstance(value, str) else "float" if isinstance(value, float) else "int"}
             for r, row in enumerate(rows, start) for c, value in enumerate(row)]
    with tempfile.TemporaryDirectory() as temp:
        data = Path(temp) / "cells.json"
        data.write_text(json.dumps(cells, ensure_ascii=False), encoding="utf-8")
        xlsx("write", path, "--sheet", sheet, "--data-file", data)


def main():
    path = ROOT / "data/Excel/GuLiStrikeRogueCards.xlsx"
    if path.exists():
        raise SystemExit("Cards workbook exists; edit it with xlsx rather than re-seeding.")
    xlsx("new", path, "--sheet", "Cards")
    rows = [
        ["id", "name", "Note", "Type", "TextIds", "ImplementationClass", "UnitTypeId", "BonusPercent", "FrontMaterial"],
        ["str", "str", "str", "int", "str[]", "softclass", "int", "float", "softobject"],
        ["Necessary", "Necessary", "Optional", "Necessary", "Necessary", "Necessary", "Necessary", "Necessary", "Necessary"],
    ]
    texts = []
    for ident, row_name, art, effect, bonus, title, description in [
        ("01.01", "WM01_FireRate_Lv1", "FireRate", "FireRate", .2, "增加射速", DESCRIPTION_PATTERNS['FireRate']),
        ("02.01", "WM01_MoveSpeed_Lv1", "HighSpeed", "MoveSpeed", .5, "极速机动", DESCRIPTION_PATTERNS['HighSpeed']),
        ("03.01", "WM01_MissileDamage_Lv1", "MissileDamage", "MissileDamage", .2, "增加导弹伤害", DESCRIPTION_PATTERNS['MissileDamage']),
    ]:
        keys = [f"Card.WM01.{art}.Title", f"Card.WM01.{art}.Description"]
        folder = 'MoveSpeed' if art == 'HighSpeed' else art
        material = f"/Game/GuLiStrike/Cards/Commander/WM01/{folder}/Materials/MI_{art}_ModelComic_v9"
        rows.append([ident, row_name, f"己方本局所有现有及后续重防号：{title}；独立来源逐次乘算。", 1,
                     json.dumps(keys, ensure_ascii=False), f"/Script/GuLiStrike.GuLiRogueCard{effect}Effect", 2, bonus,
                     material + "." + material.rsplit("/", 1)[-1]])
        texts.extend([[keys[0], "重防号肉鸽卡标题", title], [keys[1], "重防号肉鸽卡说明；{0}来自BonusPercent×100", description]])
    write(path, "Cards", rows)
    xlsx("style", path, "--sheet", "Cards", "--range", "A1:I1", "--bold", "--bg", "17365D", "--font-color", "FFFFFF")
    xlsx("style", path, "--sheet", "Cards", "--range", "A2:I3", "--bg", "DCE6F1")
    xlsx("col-width", path, "--sheet", "Cards", "--col", "A", "--to", "I", "--width", "24")
    xlsx("col-width", path, "--sheet", "Cards", "--col", "C", "--width", "58")
    xlsx("col-width", path, "--sheet", "Cards", "--col", "E", "--to", "F", "--width", "65")
    xlsx("col-width", path, "--sheet", "Cards", "--col", "I", "--width", "90")
    xlsx("add-sheet", path, "--name", "_说明")
    write(path, "_说明", [["项目", "规则"], ["数据格式", "Cards 前三行为字段名/类型/必要性，第四行起为数据；_说明不导出。"],
        ["id", "文本：两位卡族编号.两位等级，例如01.01、01.10；禁止转换为数值。重复取得不升级ID。"],
        ["name / Note", "name是稳定UE行名；Note仅供策划阅读，不作为游戏文字。"],
        ["Type", "1=指挥官部队；2=Ship；3=地面机甲；4=建筑。本批仅实现1。"],
        ["TextIds", 'JSON字符串数组，固定2项：["标题文本ID","说明文本ID"]；索引0=卡名，1=底部效果说明。'],
        ["ImplementationClass", "完整软类路径，必须继承GuLiRogueCardEffect；服务器调用实际效果。"],
        ["UnitTypeId", "引用Commander/Soldiers.id；重防号WM01=2。"],
        ["BonusPercent", "百分比小数。0.2=20%、0.5=50%；显示{0}参数由该值生成，不能重复维护数值。"],
        ["FrontMaterial", "完整软资源路径，引用六层视差卡面材质。"],
        ["叠加", "每次获得创建独立来源，逐次乘算；攻速两次×1.44，移速两次×2.25；同一确认幂等。"],
        ["范围", "本局己方当前/后续重防号，换任指挥官保留，新战局清空。"],
        ["文本与翻译", "只维护GuLiStrikeGameTexts.xlsx；导出StringTable稳定键，后续通过UE本地化收集翻译。"],
        ["入口", "指挥官实战F4；左射速/中导弹/右机动；第一次翻面、第二次确认；提交前Esc取消。"]])
    xlsx("col-width", path, "--sheet", "_说明", "--col", "A", "--width", "26")
    xlsx("col-width", path, "--sheet", "_说明", "--col", "B", "--width", "120")
    shared = ROOT / "data/Excel/GuLiStrikeGameTexts.xlsx"
    existing = json.loads(xlsx("read", shared, "--sheet", "Texts", "--format", "json"))["rows"]
    ui = {"Title": "命运之选", "Entering": "卡牌入场中…", "Choose": "移动鼠标轻压牌面 · 单击选择 · Esc 取消",
          "Flip": "翻牌中…", "Confirm": "再次点击所选卡牌确认 · Esc 取消", "Submitting": "正在确认加成…",
          "Success": "加成已生效", "Failed": "选牌失败：{0} · Esc 返回战场", "OpenFailed": "无法打开选牌：{0}",
          "Replay": "重新播放", "Done": "选择完成"}
    texts.extend([[f"UI.RogueCards.{key}", "实战选牌界面", value] for key, value in ui.items()])
    ids = {row[0] for row in existing if row}
    write(shared, "Texts", [r for r in texts if r[0] not in ids], len(existing)+1)
    print(path)


if __name__ == "__main__":
    main()
