"""Update only the Ground AI engine, its five-rank prerequisite and optical presentation rows."""
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CLI = Path("D:/UE5.7/excelize-cli/bin/xlsx.exe")
ATLAS = "/Game/GuLiStrike/GroundMech/UI/AIEngine/T_AIEngine_Optics.T_AIEngine_Optics"
FX = "/Game/GuLiStrike/FX/GroundMech/EnhancedBullet"


def column(index):
    letters = ""
    while index:
        index, remainder = divmod(index-1, 26)
        letters = chr(65+remainder)+letters
    return letters


def update(book, sheet, rows, extra=()):
    path = ROOT / "data/Excel" / f"GuLiStrike{book}.xlsx"
    table = json.loads(subprocess.check_output([CLI, "read", path, "--sheet", sheet, "--format", "json"]))["rows"]
    header, types = table[0][:], table[1][:]
    cells = []

    def set_cell(row, col, value, kind="string"):
        cells.append(dict(cell=f"{column(col+1)}{row}", value=value, type=kind))

    for field in extra:
        key, field_type = field if isinstance(field,tuple) else (field,"str[]")
        if key in header:
            continue
        for row, value in enumerate((key, field_type, "Optional"), 1):
            set_cell(row, len(header), value)
        header.append(key)
        types.append(field_type)
    positions = {str(row[0]): i for i, row in enumerate(table[3:], 4) if row and row[0]}
    records = {str(row[0]): dict(zip(header,row)) for row in table[3:] if row}
    next_row = len(table)+1
    for reference, overrides in rows:
        record = {**(records[reference] if reference else {}), **overrides}
        ident = str(record[header[0]])
        row = positions.get(ident)
        if row is None:
            row, next_row = next_row, next_row+1
        elif 'name' in record and record['name']!=records[ident]['name']:
            raise ValueError(f'{book}/{sheet} id {ident} belongs to {records[ident]["name"]}')
        for key, value in record.items():
            idx = header.index(key)
            if isinstance(value,list): value=json.dumps([str(item) for item in value],ensure_ascii=False,separators=(',',':'))
            kind = types[idx] if value!="" and types[idx] in ("int","float") else "string"
            if kind=="int": value=int(value)
            if kind=="float": value=float(value)
            set_cell(row,idx,value,kind)
    with tempfile.TemporaryDirectory() as directory:
        changes = Path(directory)/"cells.json"
        changes.write_text(json.dumps(cells, ensure_ascii=False),encoding="utf-8")
        subprocess.run([CLI,"write",path,"--sheet",sheet,"--data-file",changes],check=True,stdout=subprocess.DEVNULL)


def main():
    update("RogueCards","Cards",[
        ("10.01",dict(id="14.01",name="GroundMech_AIEngine_Lv1",Note="游骑兵个人本局，强化子弹三级后解锁",
                      TextIds=["Card.GroundMech.AIEngine.Title","Card.GroundMech.AIEngine.Description"],
                      ArtworkId=17,IconId=16,RequiredCardIds=["10.01"],RequiredCardLevels=[3],ExcludedCardIds=[],MaxAcquisitions=3,EffectIds=[9])),
        (None,dict(id="10.01",MaxAcquisitions=5,Note="按当前等级修正武器档位的基础伤害与射速"))])
    update("RogueCards","Effects",[(None,dict(id=9,name="GroundAIEngine",Note="按等级取得锁定时间和范围",Handler="ground/ai_engine",Version=1,ConfigId=6,BuffId=0))])
    update("RogueCards","GroundWeaponEffects",[
        ("2",dict(id=6,name="GroundAIEngine",Note="游骑兵单目标枪口视场",LockSeconds=[2,1.5,1],LockRangeCentimeters=[8000,10000,15000],
                  LockAperturePixels=342,LockHexPixels=62,
                  BulletDamageMultipliers=[],BulletRateMultipliers=[],BulletFlightVfxIds=[])),
        (None,dict(id=2,BulletDamageMultipliers=[1.5,1.7,2.5,2.8,4],BulletRateMultipliers=[.9,.9,.8,.8,.7],BulletFlightVfxIds=[69,69,70,70,71]))],
        ("LockSeconds","LockRangeCentimeters","BulletDamageMultipliers","BulletRateMultipliers","BulletFlightVfxIds",
         ("LockAperturePixels","float"),("LockHexPixels","float")))
    update("GameTexts","Texts",[(None,{"文本id":"Card.GroundMech."+key,"介绍":"游骑兵个人肉鸽","内容":value}) for key,value in [
        ("AIEngine.Title","人工智能引擎"),("AIEngine.Description","<Unit>游骑兵</> 仅自动射击，<Gain>{2} 米</>内瞄准 <Gain>{1} 秒</>后锁定开火"),
        ("EnhancedBullet.Description","<Unit>游骑兵</> 普攻伤害 <Gain>+{1}%</>，射速降低 {2}%。")]])
    update("GroundHUD","Elements",[(None,dict(id=88,name="AIEngineOptics",Note="已审绿色枪口光圈、侧轨和分层六边形图集",Texture=ATLAS,Sprite=-1,Value=0,ColorSRGB="",IconId=0))])
    bullet_styles()


def bullet_styles():
    colors=[(69,"Gold","F1CF69"),(70,"Violet","B08AE8"),(71,"Silver","D9E6ED")]
    update("Vfx","Effects",[("8",dict(id=ident,name="GroundEnhanced"+color,Note="强化子弹等级颜色",
            ResourcePath=f"{FX}/NS_Enhanced{color}.NS_Enhanced{color}",TrailTintSRGB=tint)) for ident,color,tint in colors],
            (("TrailTintSRGB","str"),))


if __name__ == "__main__":
    main()
