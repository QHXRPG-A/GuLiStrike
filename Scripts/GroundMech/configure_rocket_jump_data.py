"""Add the approved RocketJump row with excelize; preserve existing and subsequently tuned values."""
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
import sys
sys.path.insert(0,str(ROOT/'Scripts/Vfx'))
from vfx_registry import vfx_id
CLI = r'D:\UE5.7\excelize-cli\bin\xlsx.exe'
BOOK = ROOT / 'Data/Excel/GuLiStrikeMech.xlsx'


def run(*args):
    return subprocess.check_output([CLI, *map(str, args)], encoding='utf-8')


def column(index):
    result = ''
    while index:
        index, digit = divmod(index - 1, 26)
        result = chr(65 + digit) + result
    return result


def main():
    rows = json.loads(run('read', BOOK, '--sheet', '技能表', '--format', 'json'))['rows']
    headers = list(rows[0])
    additions = [
        ('ExecutionType', 'str', 'GAS'),
        ('AbilityClass', 'softclass', '/Script/GuLiStrike.GuLiGA_RocketJump'),
        ('MaxFuel', 'float', 100), ('InitialFuel', 'float', 100),
        ('FuelDrainPerSecond', 'float', 20), ('FuelRecoveryPerSecond', 'float', 20),
        ('FuelRecoveryDelay', 'float', 1), ('ThrustAcceleration', 'float', 2180),
        ('MaxRiseSpeed', 'float', 1200),
        ('JetVfxId', 'int', vfx_id('RocketJet')),
        ('JetSocketLeft', 'str', 'Jet_Outlet_L'), ('JetSocketRight', 'str', 'Jet_Outlet_R'),
        ('JetPitchDegrees', 'float', -90),
        ('FuelBarVfxId', 'int', vfx_id('RocketFuelBar')),
        ('FuelBarHeight', 'float', 400), ('FuelBarRightOffset', 'float', 450),
        ('FuelBarFadeSeconds', 'float', 1.5),
        ('AirSpeedMultiplier', 'float', 1.5), ('JetMaxTiltDegrees', 'float', 15),
        ('FallGravityMultiplier', 'float', 2),
    ]
    cells = []
    added = []
    for name, kind, value in additions:
        if name not in headers:
            added.append((name, kind, value))
            headers.append(name)
            c = column(len(headers))
            for r, text in enumerate((name, kind, 'Necessary' if name == 'ExecutionType' else 'Optional'), 1):
                cells.append({'cell': f'{c}{r}', 'value': text, 'type': 'string'})
    # Required fields are validated according to ExecutionType by the exporter.
    for i in range(3, 14):
        cells.append({'cell': f'{column(i+1)}3', 'value': 'Optional', 'type': 'string'})
    existing = {row[1]: n for n, row in enumerate(rows[3:], 4) if len(row) > 1}
    if 'RocketJump' in existing:
        for name, kind, value in added:
            cells.append({'cell': f'{column(headers.index(name)+1)}{existing["RocketJump"]}', 'value': value,
                          'type': kind if kind in ('int', 'float') else 'string'})
    for name, r in existing.items():
        row = rows[r-1]
        c = headers.index('ExecutionType')
        if len(row) <= c or not row[c]:
            cells.append({'cell': f'{column(c+1)}{r}', 'value': 'Weapon' if name == 'BasicMachinegun' else 'GAS', 'type': 'string'})
    if 'RocketJump' not in existing:
        r = len(rows) + 1
        values = [('id', 'int', 2), ('name', 'str', 'RocketJump'),
                  ('Note', 'str', '火箭跳；距离cm、时间s；有效空格即时双喷并推进；落地松键1秒后恢复；未满常显，满后1.5秒淡出；空中WASD，下身随实际飞行方向，允许开火')] + additions
        for name, kind, value in values:
            cells.append({'cell': f'{column(headers.index(name)+1)}{r}', 'value': value,
                          'type': kind if kind in ('int', 'float') else 'string'})
    with tempfile.TemporaryDirectory(prefix='guli-rocket-data-') as directory:
        path = Path(directory) / 'cells.json'
        path.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf-8')
        run('write', BOOK, '--sheet', '技能表', '--data-file', path)
    end = column(len(headers))
    run('style', BOOK, '--sheet', '技能表', '--range', f'O1:{end}1', '--bold', '--bg', 'D9EAF7', '--wrap')
    run('style', BOOK, '--sheet', '技能表', '--range', f'O2:{end}3', '--font-color', '666666')
    run('col-width', BOOK, '--sheet', '技能表', '--col', 'O', '--to', end, '--width', '25')
    print('RocketJump configuration added; existing tuning preserved.')


if __name__ == '__main__':
    main()
