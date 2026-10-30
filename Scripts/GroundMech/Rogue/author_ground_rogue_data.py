"""Initialize the approved Ground ricochet rows through excelize-cli; preserve existing authored rows."""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
if (ROOT / 'data/Excel/GuLiStrikeBuffs.xlsx').exists():
    raise SystemExit('The single-effect initializer is retired. Use author_burning_cards.py for the EffectIds/Buffs schema.')
CLI = r"D:\UE5.7\excelize-cli\bin\xlsx.exe"
OUT = ROOT / "Artifacts/GroundRogue_20261028"
OUT.mkdir(parents=True, exist_ok=True)

def call(*args):
    return subprocess.check_output([CLI, *map(str, args)], encoding="utf-8")

def read(book, sheet):
    return json.loads(call("read", ROOT / "data/Excel" / book, "--sheet", sheet, "--format", "json"))["rows"]

def col(index):
    value = ""
    while index:
        index, rem = divmod(index - 1, 26)
        value = chr(65 + rem) + value
    return value

def write(book, sheet, cells):
    path = OUT / (book + "." + sheet + ".cells.json")
    path.write_text(json.dumps(cells, ensure_ascii=False), encoding="utf-8")
    call("write", ROOT / "data/Excel" / book, "--sheet", sheet, "--data-file", path)

def cell(row, column, value):
    return {"cell": f"{col(column)}{row}", "value": value, "type": "string" if isinstance(value, str) else "int" if isinstance(value, int) else "float"}

cards = "GuLiStrikeRogueCards.xlsx"
rows = read(cards, "Cards")
if "EffectConfigId" not in rows[0]:
    index = len(rows[0]) + 1
    write(cards, "Cards", [cell(1, index, "EffectConfigId"), cell(2, index, "int"), cell(3, index, "Optional")])
rows = read(cards, "Cards")
if not any(row and row[0] == "06.01" for row in rows[3:]):
    values = {"id": "06.01", "name": "GroundMech_Ricochet_Lv1", "Note": "个人本局普攻折射；每次获取随机区间整体+1；每跳30米、无遮挡、不重复；直接伤害逐跳乘0.8，附加效果不衰减。", "Type": 3,
              "TextIds": json.dumps(["Card.GroundMech.Ricochet.Title", "Card.GroundMech.Ricochet.Description"]),
              "ImplementationClass": "/Script/GuLiStrike.GuLiRogueCardGroundRicochetEffect", "UnitTypeId": 0, "BonusPercent": 0,
              "ArtworkId": 6, "UpgradeVfx": "/Game/GuLiStrike/FX/RogueCards/NS_RogueUpgrade_Lite.NS_RogueUpgrade_Lite", "UpgradeVfxScale": 1,
              "UpgradeVfxColor": "(R=20,G=6.930114,B=0.933554,A=1)", "RequiredCardIds": "[]", "ExcludedCardIds": "[]", "MaxAcquisitions": 0, "BonusCount": 0, "EffectConfigId": 1}
    write(cards, "Cards", [cell(len(rows) + 1, i + 1, values.get(name, "")) for i, name in enumerate(rows[0])])
info = json.loads(call("info", ROOT / "data/Excel" / cards))
if not any(sheet["name"] == "GroundRicochet" for sheet in info["sheets"]):
    call("add-sheet", ROOT / "data/Excel" / cards, "--name", "GroundRicochet")
    fields = [("id", "int", "Necessary"), ("name", "str", "Necessary"), ("Note", "str", "Optional"),
              ("MinimumHops", "int", "Necessary"), ("MaximumHops", "int", "Necessary"), ("HopsPerStack", "int", "Necessary"),
              ("RangeCentimeters", "float", "Necessary"), ("DamageRetention", "float", "Necessary")]
    data = [[v[i] for v in fields] for i in range(3)] + [[1, "GroundMech_Ricochet", "折射段不含首个目标；附加伤害不衰减。", 5, 8, 1, 3000.0, 0.8]]
    write(cards, "GroundRicochet", [cell(r + 1, c + 1, value) for r, row in enumerate(data) for c, value in enumerate(row)])

texts = "GuLiStrikeGameTexts.xlsx"
rows = read(texts, "Texts")
entries = {"Card.GroundMech.Ricochet.Title": ("地面机甲肉鸽卡标题", "折射子弹"),
           "Card.GroundMech.Ricochet.Description": ("{0}/{1}为本次获取后的折射随机区间", "<Unit>地面机甲</> <Gain>普攻折射{0}–{1}次</>"),
           "UI.RogueCards.Capacity": ("肉鸽技术容量提示", "当前构筑已达到系统容量，无法继续强化"),
           "UI.RogueCards.GroundUnavailable": ("地面肉鸽入口不可用", "地面机甲肉鸽能力尚未就绪")}
cells = []
for key, (note, text) in entries.items():
    if any(row and row[0] == key for row in rows):
        continue
    r = len(rows) + 1
    rows.append([key, note, text])
    cells += [cell(r, c + 1, v) for c, v in enumerate([key, note, text])]
if cells:
    write(texts, "Texts", cells)

book = "GuLiStrikeMech.xlsx"
rows = read(book, "技能表")
if "ImpactVfxId" not in rows[0]:
    c = len(rows[0]) + 1
    cells = [cell(1, c, "ImpactVfxId"), cell(2, c, "int"), cell(3, c, "Optional")]
    cells += [cell(r + 1, c, 54 if row[1] == "BasicMachinegun" else 0) for r, row in enumerate(rows) if r >= 3 and len(row) > 1 and row[0]]
    write(book, "技能表", cells)
print(json.dumps({"success": True, "card": "06.01", "artwork": 6, "readback": read(cards, "GroundRicochet")}, ensure_ascii=False))
