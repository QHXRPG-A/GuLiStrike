"""Targeted Nov 3 source-table update; excelize performs all XLSX operations."""
import json
import author_burning_cards as tables

tables.OUT = tables.ROOT / 'Artifacts/GroundRevision_20261103'


def main():
    tables.upsert('GuLiStrikeRogueCards.xlsx', 'GroundRicochet', [
        {'id': 1, 'RangeCentimeters': 5000.0,
         'Note': '首次命中点为固定球心；受击中心距球心不超过50米；各跳就近、无遮挡、不重复；直接伤害逐跳乘0.8。'}])
    tables.upsert('GuLiStrikeRogueCards.xlsx', 'Cards', [
        {'id': '06.01', 'Note': '个人本局普攻折射；每次获取随机区间整体+1；首次命中点周围50米三维球；牌面显示取得后等级。'}])
    tables.upsert('GuLiStrikeGameTexts.xlsx', 'Texts', [
        {'文本id': 'Card.GroundMech.Ricochet.Description', '内容': '<Unit>游骑兵</> <Gain>普攻折射-{0}</>', '介绍': '{0}为取得后的卡牌等级'},
        {'文本id': 'Card.GroundMech.BurningBullet.Description', '内容': '<Unit>游骑兵</> 普攻附加<Gain>灼烧-{0}</>', '介绍': '{0}为取得后的卡牌等级，当前限取一次'},
        {'文本id': 'Card.GroundMech.BurningBurst.Description', '内容': '<Unit>游骑兵</> 灼烧每隔一段时间发生<Gain>爆燃</>', '介绍': '单行效果；数值规则仍由Buff配置维护'}], key='文本id')
    tables.upsert('GuLiStrikeVfx.xlsx', 'Effects', [{'id': 60,
        'Note': '独立火焰脉冲；0.5秒扩张至5米外缘、0.5秒保持范围淡出；总长1秒，准备阶段缓存参数'}])
    rows = tables.read('GuLiStrikeRogueCards.xlsx', 'GroundRicochet')
    assert float(rows[3][rows[0].index('RangeCentimeters')]) == 5000
    print(json.dumps({'success': True, 'range_cm': 5000, 'center': 'first_impact', 'duration': [0.5, 0.5]}))


if __name__ == '__main__':
    main()
