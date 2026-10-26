"""Apply the approved backward gait and powered-forward tuning through excelize."""
import json
from author_mobility_tables import BOOK, cli, column, write


def apply():
    for sheet in ('技能表', 'Gait'):
        rows = json.loads(cli('read', BOOK, '--sheet', sheet, '--format', 'json'))['rows']
        headers = rows[0]
        cells = []
        if sheet == '技能表' and 'JetForwardSpeedMultiplier' not in headers:
            headers.append('JetForwardSpeedMultiplier')
            col = column(len(headers))
            for line, value in enumerate(('JetForwardSpeedMultiplier', 'float', 'Optional'), 1):
                cells.append(dict(cell=f'{col}{line}', value=value, type='string'))
        for line, row in enumerate(rows[3:], 4):
            name = row[headers.index('name')]
            values = {}
            if sheet == '技能表':
                values['JetForwardSpeedMultiplier'] = 2.0 if name == 'RocketJump' else 0.0
                if name == 'RocketJump':
                    values['Note'] = ('火箭跳；cm/s；普通空中速度=WalkSpeed×AirSpeedMultiplier；'
                                      '仅有效喷气+前进：前向速度=SprintSpeed×JetForwardSpeedMultiplier；'
                                      '空中机体方向WASD，W渐转镜头；上升/燃料独立；退出推进平滑制动')
            elif name in ('WalkBackward', 'RunBackward'):
                values['HorizontalMotionScale'] = 1.0
            for key, value in values.items():
                cells.append(dict(cell=f'{column(headers.index(key)+1)}{line}', value=value,
                                  type='string' if isinstance(value, str) else 'float'))
        if cells:
            write(sheet, cells)
    print('Backward horizontal scale=1.0; powered forward multiplier=2.0; other tuning preserved.')


if __name__ == '__main__':
    apply()
