"""Three-stage blink and camera damping; absolute values, Excel remains the source."""
import json
from author_mobility_tables import BOOK, cli, column, write


def update(sheet, values):
    rows = json.loads(cli('read', BOOK, '--sheet', sheet, '--format', 'json'))['rows']
    headers = rows[0]
    cells = []
    for name, value in values.items():
        if name not in headers:
            headers.append(name)
            for row, text in enumerate((name, 'float', 'Necessary'), 1):
                cells.append(dict(cell=f'{column(len(headers))}{row}', value=text, type='string'))
        for row in range(4, len(rows)+1):
            cells.append(dict(cell=f'{column(headers.index(name)+1)}{row}', value=value, type='float'))
    write(sheet, cells)


if __name__ == '__main__':
    update('Mobility', dict(BlinkWindupSeconds=.1, BlinkLoopSeconds=.8,
                           BlinkRecoverySeconds=.2, BlinkLoopSwayDegrees=3.))
    update('Camera', dict(BlinkHoldSeconds=.04, BlinkCatchupSeconds=.45))
    print('Blink Begin/Loop/End = 0.1/0.8/0.2 s; total = 1.1 s; camera hold/95% settling = 0.04/0.45 s')
