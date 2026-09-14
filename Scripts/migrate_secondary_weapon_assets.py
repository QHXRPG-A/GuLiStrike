"""Complete secondary-weapon authoring from the captured editor asset values.

Includes the still-granted legacy Wingman basic gun/missile so no production
secondary-unit weapon retains its gameplay tuning only in a DataAsset.
"""
import json
from migrate_secondary_weapon_tables import ROOT, OUT, DEST, read, run, column


def overwrite(sheet, matrix):
    cells = []
    for r, row in enumerate(matrix):
        for c, value in enumerate(row):
            kind = 'string'
            if r >= 3 and value != '':
                t = matrix[1][c]
                if t == 'float': value, kind = float(value), 'float'
                elif t == 'int': value, kind = int(float(value)), 'int'
                elif t == 'bool': value, kind = str(value).lower() in ('true', '1'), 'bool'
            cells.append({'cell': f'{column(c+1)}{r+1}', 'value': value, 'type': kind})
    path = OUT / f'{sheet}-asset-migration-cells.json'
    path.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf-8')
    run('write', DEST, '--sheet', sheet, '--data-file', path)


def extend(matrix, fields):
    for name, kind, mark in fields:
        if name not in matrix[0]:
            matrix[0].append(name); matrix[1].append(kind); matrix[2].append(mark)
    for row in matrix[3:]:
        row.extend([''] * (len(matrix[0]) - len(row)))


def main():
    matrix = read(DEST, 'WingmanWeapons')
    if 'WeaponAsset' in matrix[0]:
        print('Secondary weapon asset authoring is already migrated; no values overwritten.')
        return
    snapshot = json.loads((OUT / 'before-editor.json').read_text(encoding='utf-8'))['wingman_weapons']
    fields = [('WeaponAsset', 'softobject', 'Necessary'), ('bRequiresLineOfSight', 'bool', 'Necessary'),
              ('MaximumHomingTurnRateDegreesPerSecond', 'float', 'Necessary'), ('AttackProjectile', 'softobject', 'Optional')]
    extend(matrix, fields)
    assets = {path.rsplit('_', 1)[-1]: (path, values) for path, values in snapshot.items()}
    # The existing air/ground rows already own their effective values. Read only
    # the two previously asset-only fields; do not copy stale inline defaults.
    for row in matrix[3:]:
        suffix = row[matrix[0].index('name')].removeprefix('Wingman')
        path, values = assets[suffix]
        row[matrix[0].index('WeaponAsset')] = path
        row[matrix[0].index('bRequiresLineOfSight')] = values['requires_line_of_sight']
        row[matrix[0].index('MaximumHomingTurnRateDegreesPerSecond')] = values['maximum_homing_turn_rate_degrees_per_second']
        if suffix == 'GroundMissile':
            row[matrix[0].index('AttackProjectile')] = '/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundMissile.DA_WingmanGroundMissile'
    columns = {
        'Damage': 'damage', 'CooldownSeconds': 'cooldown_seconds', 'RangeCentimeters': 'range_centimeters',
        'FireConeHalfAngleDegrees': 'target_cone_half_angle_degrees',
        'bRequiresLineOfSight': 'requires_line_of_sight',
        'ProjectileSpeedCentimetersPerSecond': 'projectile_speed_centimeters_per_second',
        'ProjectileLifetimeSeconds': 'projectile_lifetime_seconds', 'SweepRadiusCentimeters': 'sweep_radius_centimeters',
        'MaximumHomingTurnRateDegreesPerSecond': 'maximum_homing_turn_rate_degrees_per_second',
    }
    for index, suffix, skill in [(3, 'BasicAuto', 'Wingman.Basic.Auto'), (4, 'MissileSalvo', 'Wingman.Missile.Salvo')]:
        path, values = assets[suffix]
        record = {name: 0 for name, kind in zip(matrix[0], matrix[1]) if kind in ('float', 'int')}
        record.update({'id': index, 'name': 'Wingman' + suffix, 'Note': '兼容武器槽；沿用现有资产数值，Legacy表示既有攻击执行链',
                       'WeaponAsset': path, 'SkillId': skill, 'AttackPattern': 'Legacy', 'ExecutorId': 'Legacy'})
        record.update({key: values[prop] for key, prop in columns.items()})
        matrix.append([record.get(key, '') for key in matrix[0]])
    overwrite('WingmanWeapons', matrix)
    run('style', DEST, '--sheet', 'WingmanWeapons', '--range', 'AA1:AD3', '--bold', '--bg', 'D9E2F3', '--wrap')
    run('col-width', DEST, '--sheet', 'WingmanWeapons', '--col', 'AA', '--to', 'AD', '--width', 32)
    projectiles = read(DEST, 'Projectiles')
    extend(projectiles, [('UnitTypeId', 'int', 'Necessary'), ('SlotId', 'str', 'Necessary'), ('SkillId', 'str', 'Necessary')])
    for row in projectiles[3:]:
        row[projectiles[0].index('UnitTypeId')] = 2
        row[projectiles[0].index('SlotId')] = 'MissileLauncher'
        row[projectiles[0].index('SkillId')] = 'WM01_HomingMissile'
    overwrite('Projectiles', projectiles)
    print('Migrated all four granted Wingman weapons and Commander projectile bindings.')


if __name__ == '__main__':
    main()
