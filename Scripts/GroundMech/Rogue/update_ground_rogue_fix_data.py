"""Idempotent, scoped Excel migration for the approved fixed-radius burning burst."""
import json
from pathlib import Path
import author_burning_cards as tables

tables.OUT = tables.ROOT / 'Artifacts/GroundRogueFix_20261102'

def main():
    book = 'GuLiStrikeBuffs.xlsx'
    rows = tables.read(book, 'BurningBurst')
    old = 'RadiusMultiplier'
    new = 'RadiusCentimeters'
    assert (old in rows[0]) != (new in rows[0])
    if old in rows[0]:
        tables.write(book, 'BurningBurst', [tables.cell(1, rows[0].index(old) + 1, new)])
    tables.upsert(book, 'Buffs', [{'id': 1, 'Version': 2}, {'id': 2, 'Version': 2}])
    tables.upsert(book, 'BurningBurst', [{'id': 1, new: 500.0,
        'Note': '固定5米球与碰撞体相交；穿墙；冻结共同伤害与目标；零伤害仍表现'}])
    tables.upsert('GuLiStrikeVfx.xlsx', 'Effects', [{'id': 60,
        'ResourcePath': '/Game/GuLiStrike/FX/GroundMech/Burning/NS_GroundBurningBurst.NS_GroundBurningBurst',
        'Note': '复用灼烧火焰；0.5秒扩张至5米、0.5秒消散；准备阶段预加载'}])
    tables.upsert('GuLiStrikeGameTexts.xlsx', 'Texts', [
        {'文本id': 'Card.GroundMech.BurningBurst.Description',
         '内容': '<Unit>地面机甲</> 灼烧每隔一段时间发生<Gain>爆燃</>',
         '介绍': '爆燃规则由Buff配置维护'},
        {'文本id': 'Buff.BurningBurst.Description',
         '内容': '灼烧期间独立每{0}秒爆燃，造成宿主已损失生命值的{1}%范围伤害，半径{2}米，接触范围的敌人受到同等伤害。'}], key='文本id')
    rows = tables.read(book, 'BurningBurst')
    assert rows[0][6] == new and float(rows[3][6]) == 500
    print(json.dumps({'migrated': True, 'radius_cm': 500, 'buff_version': 2, 'vfx_id': 60}))

if __name__ == '__main__':
    main()
