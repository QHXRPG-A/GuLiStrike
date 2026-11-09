"""Scoped source-table update for animation-driven fire and independent explosive rounds."""
import json
import update_initial_bullet_cards as table


def add_fields(book, sheet, fields):
    rows = table.read(book, sheet)
    headers = rows[0]
    cells = []
    for name, kind, default in fields:
        if name in headers:
            continue
        headers.append(name)
        column = len(headers)
        cells.extend(table.cell(i + 1, column, value) for i, value in enumerate((name, kind, 'Optional')))
        cells.extend(table.cell(i + 1, column, default) for i in range(3, len(rows)))
    if cells:
        table.write(book, sheet, cells)


def main():
    add_fields('GuLiStrikeRogueCards.xlsx', 'GroundWeaponEffects', [
        ('ExplosiveSpeedMultiplier', 'float', 0.), ('ExplosiveOutwardOffsetCentimeters', 'float', 0.)])
    table.upsert('GuLiStrikeRogueCards.xlsx', 'GroundWeaponEffects', [dict(id=3,
        ExplosiveSpeedMultiplier=.7, ExplosiveOutwardOffsetCentimeters=50.)])
    table.upsert('GuLiStrikeRogueCards.xlsx', 'Effects', [dict(id=6, Version=2)])
    add_fields('GuLiStrikeVfx.xlsx', 'Effects', [
        ('PersistentTrailVfxId', 'int', 0), ('InitialStyleLengthMultiplier', 'float', 1.)])
    rows = table.read('GuLiStrikeVfx.xlsx', 'Effects')
    headers = rows[0]
    # All batch trail rows use the same authored ratio for the two basic infantry types.
    mass = [dict(id=int(r[0]), InitialStyleLengthMultiplier=.25)
            for r in rows[3:] if r and 'NS_MassBulletTrail_' in str(r[headers.index('ResourcePath')])]
    table.upsert('GuLiStrikeVfx.xlsx', 'Effects', [
        dict(id=62, Note='游骑兵初始弹：12cm宽、30m最大长度、零弯曲', TrailLengthCm=3000.),
        dict(id=63, ScaleX=2., ScaleY=2., ScaleZ=2., PersistentTrailVfxId=0, TrailLengthCm=12000., TrailHoldSeconds=.12, TrailFadeSeconds=.38), *mass])
    table.upsert('GuLiStrikeMech.xlsx', 'Gait', [dict(id=12,
        Asset='/Game/GuLiStrike/GroundMech/Animations/A4B2/Anim_GroundFire_AlternatingLoop.Anim_GroundFire_AlternatingLoop',
        Note='左右循环开火：右0秒/左0.5秒；循环长1秒；实际播放率=攻速/2')])
    print(json.dumps({'success': True, 'initial_length_cm': 3000, 'explosive_speed_multiplier': .7,
                      'outward_cm': 50, 'mass_rows': mass}, ensure_ascii=False))


if __name__ == '__main__':
    main()
