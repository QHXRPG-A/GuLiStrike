"""Author only WM01 flight curves and dedicated GPU pool registrations via excelize."""
from author_wm01_missile_cards import read, save, column


def main():
    path, digest, rows = read('GuLiStrikeSecondaryWeapons.xlsx', 'Projectiles')
    names = list(rows[0])
    changes = []
    def put(row, name, value, kind='string'):
        changes.append({'cell': column(names.index(name) + 1) + str(row), 'value': value, 'type': kind})
    for name in ('VerticalCurveCentimeters', 'LongitudinalCurveCentimeters'):
        if name not in names:
            names.append(name)
        for row, value in ((1, name), (2, 'float'), (3, 'Necessary')):
            put(row, name, value)
    for index, values in enumerate(rows[3:], 4):
        if not values or not values[0]:
            continue
        old = dict(zip(rows[0], values))
        if old['name'] == 'WM01_Missile':
            for name, value in {'MinimumLiftHeightCentimeters': 160, 'MaximumLiftHeightCentimeters': 420,
                                'LateralOffsetCentimeters': 260, 'VerticalCurveCentimeters': 320,
                                'LongitudinalCurveCentimeters': 220, 'ConvergenceDistanceCentimeters': 700}.items():
                put(index, name, value, 'float')
        else:
            for name in ('VerticalCurveCentimeters', 'LongitudinalCurveCentimeters'):
                if not old.get(name):
                    put(index, name, 0, 'float')
    save(path, digest, 'Projectiles', changes)

    path, digest, rows = read('GuLiStrikeVfx.xlsx', 'Effects')
    names = rows[0]
    existing = {r[names.index('name')]: i for i, r in enumerate(rows, 1) if i >= 4 and r and r[0]}
    maximum = max(int(r[0]) for r in rows[3:] if r and r[0])
    changes = []
    for suffix, note in [('Full', '24段一秒历史拖尾'), ('Lite', '8段低采样历史拖尾'), ('Minimal', '仅弹体与尾焰')]:
        name = 'WM01MissileCluster' + suffix
        index = existing.get(name, len(rows) + 1)
        ident = int(rows[index - 1][0]) if name in existing else maximum + 1
        resource = '/Game/GuLiStrike/FX/WM01Missiles/NS_WM01MissileCluster_' + suffix
        fields = {'id': ident, 'name': name, 'Note': '重防号独立GPU集群；每批64枚；' + note,
                  'ResourcePath': resource + '.' + resource.rsplit('/', 1)[-1], 'ScaleX': 1.0, 'ScaleY': 1.0, 'ScaleZ': 1.0}
        for key, value in fields.items():
            put(index, key, value, 'int' if key == 'id' else 'float' if isinstance(value, float) else 'string')
        if name not in existing:
            rows.append([str(ident)])
            maximum = ident
    save(path, digest, 'Effects', changes)


if __name__ == '__main__':
    main()
