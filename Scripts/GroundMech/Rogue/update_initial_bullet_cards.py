"""Apply the 2026-11-08 weapon/card rows via excelize, preserving unrelated cells."""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CLI = r'D:\UE5.7\excelize-cli\bin\xlsx.exe'
OUT = ROOT / 'Artifacts/GroundAmmo_20261108'


def cli(*args):
    return subprocess.check_output([CLI, *map(str, args)], encoding='utf-8')


def read(book, sheet):
    return json.loads(cli('read', ROOT/'data/Excel'/book, '--sheet', sheet, '--format', 'json'))['rows']


def column(i):
    result = ''
    while i:
        i, rem = divmod(i-1, 26)
        result = chr(65+rem)+result
    return result


def write(book, sheet, cells):
    OUT.mkdir(parents=True, exist_ok=True)
    path = OUT/f'{book}.{sheet}.cells.json'
    path.write_text(json.dumps(cells, ensure_ascii=False, indent=2), encoding='utf-8')
    cli('write', ROOT/'data/Excel'/book, '--sheet', sheet, '--data-file', path)


def cell(r, c, v):
    return dict(cell=f'{column(c)}{r}', value=v,
                type='bool' if isinstance(v, bool) else 'int' if isinstance(v, int) else 'float' if isinstance(v, float) else 'string')


def upsert(book, sheet, records, key='id'):
    rows = read(book, sheet)
    headers = rows[0]
    pending = []
    for record in records:
        matches = [i for i, row in enumerate(rows[3:], 3) if row and str(row[headers.index(key)]) == str(record[key])]
        assert len(matches) <= 1, (sheet, record[key])
        index = matches[0] if matches else len(rows)
        if not matches:
            rows.append(['']*len(headers))
        elif 'name' in record:
            assert rows[index][headers.index('name')] == record['name'], ('ID occupied', sheet, record[key])
        for name, value in record.items():
            c = headers.index(name)
            pending.append(cell(index+1, c+1, value))
    write(book, sheet, pending)


def main():
    book = 'GuLiStrikeRogueCards.xlsx'
    sheet = 'GroundWeaponEffects'
    fields = [('id','int'),('name','str'),('Note','str'),('FireRateBonusPercent','float'),
              ('ExplosiveIntervals','str[]'),('ExplosiveDamageMultiplier','float'),('ExplosiveRadiusCentimeters','float'),
              ('ExplosiveBulletVfxId','int'),('ExplosiveImpactVfxId','int')]
    if sheet not in [s['name'] for s in json.loads(cli('info',ROOT/'data/Excel'/book))['sheets']]:
        cli('add-sheet',ROOT/'data/Excel'/book,'--name',sheet)
        write(book,sheet,[cell(r+1,c+1,values[r]) for c,(name,kind) in enumerate(fields)
                         for values in [(name,kind,'Optional' if name=='Note' else 'Necessary')] for r in range(3)])
    write(book,sheet,[cell(3,8,'Optional'),cell(3,9,'Optional')])
    params=[]
    for ident,name in [(1,'GroundFireRate'),(2,'GroundEnhancedBullet'),(3,'GroundExplosivePackage')]:
        params.append(dict(id=ident,name=name,Note='2026-11-08正式武器卡配置',FireRateBonusPercent=.08 if ident==1 else 0.,
                           ExplosiveIntervals='["5","4","2"]' if ident==3 else '[]',ExplosiveDamageMultiplier=2. if ident==3 else 0.,
                           ExplosiveRadiusCentimeters=1500. if ident==3 else 0.,ExplosiveBulletVfxId=63 if ident==3 else 0,ExplosiveImpactVfxId=64 if ident==3 else 0))
    upsert(book,sheet,params)
    upsert(book,'Effects',[dict(id=ident,name=name,Note='个人本局武器效果',Handler=handler,Version=1,ConfigId=ident-3,BuffId=0)
                          for ident,name,handler in [(4,'GroundFireRate','ground/fire_rate'),(5,'GroundEnhancedBullet','ground/enhanced_bullet'),(6,'GroundExplosivePackage','ground/explosive_package')]])
    cards=[]
    for ident,name,title,effect,limit in [('09.01','FireRate','增加攻速',4,0),('10.01','EnhancedBullet','强化子弹',5,1),('11.01','ExplosivePackage','爆破套餐',6,3)]:
        cards.append(dict(id=ident,name=f'GroundMech_{name}_Lv1',Note=f'游骑兵个人本局：{title}；候选显示取得后等级。',Type=3,
            TextIds=json.dumps([f'Card.GroundMech.{name}.Title',f'Card.GroundMech.{name}.Description']),
            ImplementationClass='/Script/GuLiStrike.GuLiRogueCardGroundEffect',UnitTypeId=0,BonusPercent=0.,BonusCount=0,
            ArtworkId=9,IconId=0,RequiredCardIds='[]',ExcludedCardIds='[]',MaxAcquisitions=limit,EffectIds=f'["{effect}"]',
            UpgradeVfx='/Game/GuLiStrike/FX/RogueCards/NS_RogueUpgrade_Lite.NS_RogueUpgrade_Lite',UpgradeVfxScale=1.,UpgradeVfxColor='(R=20,G=6.930114,B=0.933554,A=1)'))
    upsert(book,'Cards',cards+[dict(id='06.01',RequiredCardIds='["10.01"]'),dict(id='07.01',RequiredCardIds='["10.01"]')])
    texts = {
        'FireRate':('增加攻速','<Unit>游骑兵</> <Gain>普攻攻速+{1}% · {0}级</>'),
        'EnhancedBullet':('强化子弹','<Unit>游骑兵</> <Gain>普攻升级为强化子弹</>'),
        'ExplosivePackage':('爆破套餐','<Unit>游骑兵</> <Gain>{0}级 · 每{1}发附爆炸弹 · {2}倍伤害/{3}米</>')}
    upsert('GuLiStrikeGameTexts.xlsx','Texts',[{'文本id':f'Card.GroundMech.{name}.{suffix}','介绍':'游骑兵武器肉鸽；参数来自效果配置','内容':value}
        for name,(title,description) in texts.items() for suffix,value in [('Title',title),('Description',description)]],key='文本id')
    artroot='/Game/GuLiStrike/Cards/Shared/Placeholder'
    upsert('GuLiStrikeCardArtwork.xlsx','Templates',[dict(id=5,name='BlankPlaceholder',Note='纯空白占位，无图片和逻辑图层',ParentMaterial=artroot+'/M_CardBlank.M_CardBlank',Layout='None',CompositePreset='BlankPlaceholder')])
    upsert('GuLiStrikeCardArtwork.xlsx','Artworks',[dict(id=9,name='GroundWeaponPlaceholder',Note='三张新武器卡共用空白牌面',Title='空白占位',TemplateId=5,Material=artroot+'/MI_CardBlank.MI_CardBlank',AssetRoot=artroot,SourceRoot='',ReferenceFile='',PreviewUrl='')])
    rows=read('GuLiStrikeVfx.xlsx','Effects'); headers=rows[0]
    initial=dict(zip(headers,next(r for r in rows[3:] if r[0]=='8')))
    for i,h in enumerate(headers):
        if rows[1][i]=='float':initial[h]=float(initial.get(h) or 0)
        elif rows[1][i]=='int':initial[h]=int(initial.get(h) or 0)
    initial.update(id=62,name='GroundInitialBullet',Note='游骑兵初始弹：12cm宽、60m最大长度、零弯曲',ScaleX=1.,ScaleY=1.,ScaleZ=1.,TrailLengthCm=6000.,TrailBendCm=0.)
    vfx=[initial]
    fxroot='/Game/GuLiStrike/FX/GroundMech/ExplosivePackage'
    for ident,name,asset in [(63,'GroundExplosiveFlight','NS_GroundExplosiveFlight'),(64,'GroundExplosiveImpact','NS_GroundExplosiveImpact')]:
        v={h:('' if rows[1][i] in ('str','softobject') else 0.) for i,h in enumerate(headers)}
        v.update(id=ident,name=name,Note='爆破套餐指定资源的项目副本',ResourcePath=f'{fxroot}/{asset}.{asset}',ScaleX=1.,ScaleY=1.,ScaleZ=1.)
        vfx.append(v)
    upsert('GuLiStrikeVfx.xlsx','Effects',vfx)
    mech='GuLiStrikeMech.xlsx'; sheet='技能表'; rows=read(mech,sheet); headers=rows[0]; pending=[]
    for name,kind,default in [('InitialBulletVfxId','int',0),('InitialBulletDamageRatio','float',0.)]:
        if name not in headers:
            headers.append(name);c=len(headers)
            pending += [cell(1,c,name),cell(2,c,kind),cell(3,c,'Optional')]
            pending += [cell(r+1,c,default) for r in range(3,len(rows))]
    if pending:write(mech,sheet,pending)
    upsert(mech,sheet,[dict(id=1,name='BasicMachinegun',InitialBulletVfxId=62,InitialBulletDamageRatio=.6)])
    print(json.dumps({'success':True,'cards':['09.01','10.01','11.01'],'vfx':[62,63,64],'artwork':9},ensure_ascii=False))


if __name__=='__main__':
    main()
