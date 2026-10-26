"""Replace the short moving sprite with a long, delayed-fade Ground trace."""
import json
import tempfile
from pathlib import Path
from author_mobility_tables import ROOT, cli, column


def apply():
    book = ROOT/'Data/Excel/GuLiStrikeVfx.xlsx'
    rows = json.loads(cli('read', book, '--sheet', 'Effects', '--format', 'json'))['rows']
    headers = rows[0]
    # At 24000 cm/s, cover the complete 0.5-second per-point lifetime so the
    # distance cap cannot erase an otherwise still-visible piece midway through fading.
    values = dict(TrailLengthCm=12000., TrailHoldSeconds=.12, TrailFadeSeconds=.38, TrailBendCm=45.)
    cells = []
    for key in values:
        if key not in headers:
            headers.append(key)
            for line, value in enumerate((key, 'float', 'Optional'), 1):
                cells.append(dict(cell=f'{column(len(headers))}{line}', value=value, type='string'))
    for line, row in enumerate(rows[3:], 4):
        name = row[headers.index('name')]
        for key, value in values.items():
            cells.append(dict(cell=f'{column(headers.index(key)+1)}{line}',
                              value=value if name == 'PlayerBullet' else 0., type='float'))
        if name == 'PlayerBullet':
            asset = 'NS_GroundMech_PersistentCurve_A4'
            for key, value in dict(ResourcePath=f'/Game/GuLiStrike/FX/GroundMech/FiringFeedback_A4/{asset}.{asset}',
                                   Note='Ground长曲线尾迹；长度/残留/消散/弯曲按独立字段；ScaleY控制24cm线宽，真实弹道保持直线').items():
                cells.append(dict(cell=f'{column(headers.index(key)+1)}{line}', value=value, type='string'))
    with tempfile.TemporaryDirectory(prefix='guli_trail_cells_') as temp:
        path = Path(temp)/'cells.json'
        path.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf8')
        cli('write', book, '--sheet', 'Effects', '--data-file', path)
    print('PlayerBullet: max length 12000 cm, hold 0.12 s, fade 0.38 s, bend 45 cm.')


if __name__ == '__main__':
    apply()
