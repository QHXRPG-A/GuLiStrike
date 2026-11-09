"""Targeted Excel update; keep unrelated rows and the authored explosion size intact."""
import json
from pathlib import Path
import update_initial_bullet_cards as x

x.OUT = x.ROOT / 'Artifacts/GroundExplosiveEnhancement_20261109'


def columns(book, sheet, fields):
    rows = x.read(book, sheet)
    headers = list(rows[0])
    cells = []
    for name, kind, default in fields:
        if name in headers:
            continue
        headers.append(name)
        c = len(headers)
        cells += [x.cell(1, c, name), x.cell(2, c, kind), x.cell(3, c, 'Optional')]
        cells += [x.cell(r+1, c, default) for r in range(3, len(rows))]
    if cells:
        x.write(book, sheet, cells)


def main():
    book = 'GuLiStrikeRogueCards.xlsx'
    columns(book, 'GroundWeaponEffects', [('ExplosiveRadiusBonuses', 'str[]', '[]'),
                                         ('ExplosiveDamageBonuses', 'str[]', '[]')])
    columns('GuLiStrikeVfx.xlsx', 'Effects', [('RadiusAtUnitScaleCm', 'float', 0.)])
    rows = x.read(book, 'GroundWeaponEffects')
    base = dict(zip(rows[0], next(r for r in rows[3:] if r[0] == '3')))
    calibration = x.OUT / 'radius-calibration.json'
    radius = json.loads(calibration.read_text(encoding='utf8'))['radius_cm'] if calibration.exists() else float(base['ExplosiveRadiusCentimeters'])
    x.upsert(book, 'GroundWeaponEffects', [dict(id=3, ExplosiveRadiusCentimeters=radius),
        dict(id=4, name='GroundExplosiveEnhancement', Note='三级各乘1.02/1.02/1.04；不修改爆破进度',
             FireRateBonusPercent=0., ExplosiveIntervals='[]', ExplosiveDamageMultiplier=0.,
             ExplosiveRadiusCentimeters=0., ExplosiveBulletVfxId=0, ExplosiveImpactVfxId=0,
             ExplosiveSpeedMultiplier=0., ExplosiveOutwardOffsetCentimeters=0.,
             ExplosiveRadiusBonuses='["0.02","0.02","0.04"]', ExplosiveDamageBonuses='["0.02","0.02","0.04"]')])
    x.upsert('GuLiStrikeVfx.xlsx', 'Effects', [dict(id=64, RadiusAtUnitScaleCm=radius)])
    x.upsert(book, 'Effects', [dict(id=7, name='GroundExplosiveEnhancement', Note='爆炸半径和伤害逐级乘算',
                                   Handler='ground/explosive_enhancement', Version=1, ConfigId=4, BuffId=0)])
    x.upsert(book, 'Cards', [dict(id='12.01', name='GroundMech_ExplosiveEnhancement_Lv1',
        Note='游骑兵个人本局；前置任意爆破套餐；三级；不重置发射进度', Type=3,
        TextIds='["Card.GroundMech.ExplosiveEnhancement.Title","Card.GroundMech.ExplosiveEnhancement.Description"]',
        ImplementationClass='/Script/GuLiStrike.GuLiRogueCardGroundEffect', UnitTypeId=0, BonusPercent=0., BonusCount=0,
        ArtworkId=9, IconId=0, RequiredCardIds='["11.01"]', ExcludedCardIds='[]', MaxAcquisitions=3, EffectIds='["7"]',
        UpgradeVfx='/Game/GuLiStrike/FX/RogueCards/NS_RogueUpgrade_Lite.NS_RogueUpgrade_Lite',
        UpgradeVfxScale=1., UpgradeVfxColor='(R=20,G=6.930114,B=0.933554,A=1)')])
    x.upsert('GuLiStrikeGameTexts.xlsx', 'Texts', [
        {'文本id':'Card.GroundMech.ExplosiveEnhancement.Title','介绍':'游骑兵爆炸强化卡标题','内容':'强化爆炸弹'},
        {'文本id':'Card.GroundMech.ExplosiveEnhancement.Description','介绍':'参数为取得后等级、本次半径%、本次伤害%',
         '内容':'<Unit>游骑兵</> <Gain>{0}级 · 爆炸半径+{1}%、伤害+{2}%</>'}], key='文本id')
    print(json.dumps({'card':'12.01','radius_cm':radius,'calibration_applied':calibration.exists()},ensure_ascii=False))


if __name__ == '__main__':
    main()
