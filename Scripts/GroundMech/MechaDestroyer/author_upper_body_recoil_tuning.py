"""Configure only Shoot's optional chest recoil columns through excelize."""
import json

from author_mobility_tables import ANIMS, BOOK, cli, column, write


def apply():
    rows = json.loads(cli('read', BOOK, '--sheet', 'Gait', '--format', 'json'))['rows']
    headers = list(rows[0])
    fields = {
        'UpperBodyRecoilLeftAsset': ('softobject', ANIMS + 'Anim_Mecha_Rocket_L.Anim_Mecha_Rocket_L'),
        'UpperBodyRecoilRightAsset': ('softobject', ANIMS + 'Anim_Mecha_Rocket_R.Anim_Mecha_Rocket_R'),
        'UpperBodyRecoilScale': ('float', .35),
        'UpperBodyRecoilMaxDegrees': ('float', 8.),
    }
    cells = []
    for name, (kind, _) in fields.items():
        if name not in headers:
            headers.append(name)
            for line, value in enumerate((name, kind, 'Optional'), 1):
                cells.append(dict(cell=f'{column(len(headers))}{line}', value=value, type='string'))
        else:
            index = headers.index(name)
            assert rows[1][index] == kind and rows[2][index] == 'Optional', name
    shoot_count = 0
    for line, row in enumerate(rows[3:], 4):
        shoot = row[headers.index('name')] == 'Shoot'
        shoot_count += int(shoot)
        for name, (kind, value) in fields.items():
            cells.append(dict(cell=f'{column(headers.index(name)+1)}{line}',
                              value=value if shoot else '' if kind == 'softobject' else 0.,
                              type='string' if kind == 'softobject' else 'float'))
    assert shoot_count == 1, f'Expected one Shoot row, found {shoot_count}'
    write('Gait', cells)
    readback = json.loads(cli('read', BOOK, '--sheet', 'Gait', '--format', 'json'))['rows']
    actual_headers = readback[0]
    shoot = next(row for row in readback[3:] if row[actual_headers.index('name')] == 'Shoot')
    actual = {key: shoot[actual_headers.index(key)] for key in fields}
    for key, (kind, expected) in fields.items():
        assert (actual[key] == expected if kind == 'softobject' else float(actual[key]) == expected), (key, actual[key])
    print(json.dumps({'sheet': 'Gait', 'row': 'Shoot', 'values': actual}, ensure_ascii=False))


if __name__ == '__main__':
    apply()
