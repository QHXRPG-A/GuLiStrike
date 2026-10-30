"""Targeted, repeatable Excel authoring for the approved burning cards. Uses excelize only."""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "Artifacts/BurningRogue_20261029"
CLI = r"D:\UE5.7\excelize-cli\bin\xlsx.exe"

def call(*args):
    return subprocess.check_output([CLI, *map(str, args)], encoding="utf-8")

def read(book, sheet):
    return json.loads(call("read", ROOT / "data/Excel" / book, "--sheet", sheet, "--format", "json"))["rows"]

def col(n):
    s = ""
    while n:
        n, r = divmod(n - 1, 26)
        s = chr(65 + r) + s
    return s

def write(book, sheet, changes):
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT / f"{book}.{sheet}.cells.json"
    path.write_text(json.dumps(changes, ensure_ascii=False), encoding="utf-8")
    call("write", ROOT / "data/Excel" / book, "--sheet", sheet, "--data-file", path)

def cell(r, c, v):
    return {"cell": f"{col(c)}{r}", "value": v, "type": "string" if isinstance(v, str) else "bool" if isinstance(v,bool) else "int" if isinstance(v, int) else "float"}

def table(book, sheet, fields, records):
    path = ROOT / "data/Excel" / book
    if not path.exists():
        call("new", path, "--sheet", sheet)
    elif sheet not in {s["name"] for s in json.loads(call("info", path))["sheets"]}:
        call("add-sheet", path, "--name", sheet)
    values = [[f[i] for f in fields] for i in range(3)] + records
    write(book, sheet, [cell(r+1, c+1, v) for r, row in enumerate(values) for c, v in enumerate(row)])

def upsert(book, sheet, records, key="id"):
    rows = read(book, sheet)
    headers = rows[0]
    k = headers.index(key)
    positions = {str(row[k]): i+1 for i, row in enumerate(rows[3:], 3) if len(row)>k and row[k] != ""}
    changes = []
    next_row = len(rows)+1
    for record in records:
        r = positions.get(str(record[key]))
        if r is None:
            r = next_row
            next_row += 1
        for name, v in record.items():
            if name not in headers:
                raise ValueError(f"{book}/{sheet}: missing {name}")
            c=headers.index(name)
            if v!="":
                if rows[1][c]=='float': v=float(v)
                elif rows[1][c]=='int': v=int(v)
                elif rows[1][c]=='bool' and isinstance(v,str): v=v.lower()=='true'
            changes.append(cell(r, c+1, v))
    write(book, sheet, changes)

STD = [("id", "int", "Necessary"), ("name", "str", "Necessary"), ("Note", "str", "Optional")]
def fields(*extra):
    return STD + [(name, kind, "Necessary") for name, kind in extra]

def main():
    cards = "GuLiStrikeRogueCards.xlsx"
    rows = read(cards, "Cards")
    if "EffectConfigId" in rows[0]:
        c = rows[0].index("EffectConfigId")+1
        changes = [cell(1,c,"EffectIds"), cell(2,c,"str[]"), cell(3,c,"Optional")]
        changes += [cell(i+1,c,'["1"]' if row[0]=="06.01" else "[]") for i,row in enumerate(rows) if i>=3 and row and row[0]]
        write(cards,"Cards",changes)
    common = {"Type":3,"UnitTypeId":0,"BonusPercent":0,"BonusCount":0,"ExcludedCardIds":"[]",
              "ImplementationClass":"/Script/GuLiStrike.GuLiRogueCardGroundEffect",
              "UpgradeVfx":"/Game/GuLiStrike/FX/RogueCards/NS_RogueUpgrade_Lite.NS_RogueUpgrade_Lite", "UpgradeVfxScale":1,
              "UpgradeVfxColor":"(R=20,G=6.930114,B=0.933554,A=1)"}
    upsert(cards,"Cards", [
        {"id":"06.01","ImplementationClass":common["ImplementationClass"],"EffectIds":'["1"]'},
        dict(common,id="07.01",name="GroundMech_BurningBullet_Lv1",Note="有效普攻和折射命中施加单份灼烧；刷新8秒，不重置周期。",TextIds='["Card.GroundMech.BurningBullet.Title","Card.GroundMech.BurningBullet.Description"]',ArtworkId=7,MaxAcquisitions=1,RequiredCardIds="[]",EffectIds='["2"]'),
        dict(common,id="08.01",name="GroundMech_BurningBurst_Lv1",Note="爆裂独立2秒计时；刷新不重置；一次爆裂冻结统一伤害及目标。",TextIds='["Card.GroundMech.BurningBurst.Title","Card.GroundMech.BurningBurst.Description"]',ArtworkId=8,MaxAcquisitions=1,RequiredCardIds='["07.01"]',EffectIds='["3"]')])
    table(cards,"Effects",fields(("Handler","str"),("Version","int"),("ConfigId","int"),("BuffId","int")),[
        [1,"GroundRicochet","重复获取整体增加折射段区间","ricochet",1,1,0],
        [2,"GroundBurningOnHit","由账本接受的命中施加Buff","buff/on_hit",1,0,1],
        [3,"GroundBurningBurst","为同次命中附加依附灼烧的爆裂","buff/on_hit",1,0,2]])
    buffs="GuLiStrikeBuffs.xlsx"
    table(buffs,"Buffs",fields(("DescriptionTextId","str"),("ImplementationClass","softclass"),("Version","int"),("ConfigId","int")),[
        [1,"Burning","同目标单份；刷新不重置每秒跳伤","Buff.Burning.Description","/Script/GuLiStrike.GuLiBurningBuffHandler",1,1],
        [2,"BurningBurst","依附灼烧；独立2秒周期","Buff.BurningBurst.Description","/Script/GuLiStrike.GuLiBurningBurstBuffHandler",1,1]])
    # VFX IDs are allocated from the current source workbook, not a stale header.
    vbook="GuLiStrikeVfx.xlsx"
    info=json.loads(call("info",ROOT/"data/Excel"/vbook))
    vsheet=next(s["name"] for s in info["sheets"] if not s["name"].startswith("_"))
    vrows=read(vbook,vsheet)
    vh=vrows[0]
    existing={r[vh.index("name")]:int(r[0]) for r in vrows[3:] if r and r[0]}
    next_id=max(existing.values())+1
    paths={"GroundBurningStatus":"/Game/GuLiStrike/FX/GroundMech/Burning/NS_GroundBurningStatus.NS_GroundBurningStatus",
           "GroundBurningBurst":"/Game/Assets/Niagara_Toon_Projectiles_2/Effects/NS_Hit_15.NS_Hit_15",
           "GroundBurningBullet":"/Game/GuLiStrike/FX/GroundMech/Burning/NS_GroundBurningBullet.NS_GroundBurningBullet"}
    # Preserve the table's existing row contract by cloning the Ground impact/tracer defaults.
    base={name:v for name,v in zip(vh,next(r for r in vrows[3:] if r[1]=="GroundMechImpact"))}
    bullet_base={name:v for name,v in zip(vh,next(r for r in vrows[3:] if r[1]=="PlayerBullet"))}
    records=[]
    for name,path in paths.items():
        ident=existing.get(name)
        if ident is None:
            ident=next_id
            next_id+=1
        rec=dict(bullet_base if name=='GroundBurningBullet' else base,id=ident,name=name,Note="灼烧卡牌正式表现；来源与适配见任务归档")
        for scale in ('ScaleX','ScaleY','ScaleZ'):rec[scale]=1.0
        resource=next(k for k in vh if k in ("ResourcePath","Resource","Asset","System","AssetPath"))
        rec[resource]=path
        records.append(rec)
        existing[name]=ident
    upsert(vbook,vsheet,records)
    resource=next(k for k in vh if k in ("ResourcePath","Resource","Asset","System","AssetPath"))
    upsert(vbook,vsheet,[{"id":existing["GroundMechImpact"],resource:"/Game/Assets/Niagara_Toon_Projectiles_2/Effects/NS_Hit_1.NS_Hit_1"}])
    table(buffs,"Burning",fields(("DurationSeconds","float"),("TickIntervalSeconds","float"),("MaxHealthDamageRatio","float"),("StatusVfxId","int"),("BulletVfxId","int")),[
        [1,"Burning","伤害不受折射直接伤害倍率影响",8.0,1.0,0.05,existing["GroundBurningStatus"],existing["GroundBurningBullet"]]])
    table(buffs,"BurningBurst",fields(("ParentBuffId","int"),("IntervalSeconds","float"),("LostHealthDamageRatio","float"),("RadiusMultiplier","float"),("BurstVfxId","int")),[
        [1,"BurningBurst","范围穿墙；先冻结伤害及目标；零伤害仍表现",1,2.0,0.03,1.5,existing["GroundBurningBurst"]]])
    texts="GuLiStrikeGameTexts.xlsx"
    tr=read(texts,"Texts")
    th=tr[0]
    entries={
        "Card.GroundMech.BurningBullet.Title":("灼烧子弹","卡牌标题"),
        "Card.GroundMech.BurningBullet.Description":("<Unit>地面机甲</> 普攻附加<Gain>{0}秒灼烧</>","参数来自Buff持续时间"),
        "Card.GroundMech.BurningBurst.Title":("灼烧爆裂子弹","卡牌标题"),
        "Card.GroundMech.BurningBurst.Description":("<Unit>地面机甲</> 灼烧每<Gain>{0}秒爆裂</>","参数来自Buff独立爆裂周期"),
        "Buff.Burning.Description":("每秒受到最大生命值的{0}%伤害，持续{1}秒。重复命中刷新时间，不叠加。","Buff参数化说明"),
        "Buff.BurningBurst.Description":("灼烧期间独立每{0}秒爆裂，造成宿主已损失生命值的{1}%范围伤害，半径为宿主的{2}倍。","Buff参数化说明")}
    changes=[]
    for key,(value,note) in entries.items():
        r=next((i+1 for i,row in enumerate(tr) if row and row[0]==key),None)
        if r is None:
            r=len(tr)+1
            tr.append([key,note,value])
        changes.extend(cell(r,i+1,v) for i,v in enumerate([key,note,value]))
    write(texts,"Texts",changes)
    print(json.dumps({"success":True,"vfx_ids":{k:existing[k] for k in paths},"cards":["06.01","07.01","08.01"],"buffs":[1,2]},ensure_ascii=False))

if __name__=="__main__":
    main()
