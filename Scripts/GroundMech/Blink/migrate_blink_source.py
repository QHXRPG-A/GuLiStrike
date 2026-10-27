"""Idempotent Ground Blink source migration; all workbook I/O uses excelize-cli."""
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
CLI = r'D:\UE5.7\excelize-cli\bin\xlsx.exe'
MECH = ROOT / 'data/Excel/GuLiStrikeMech.xlsx'
VFX = ROOT / 'data/Excel/GuLiStrikeVfx.xlsx'
FIELDS = ('BlinkDistance', 'BlinkCooldownSeconds', 'BlinkWindupSeconds',
          'BlinkLoopSeconds', 'BlinkRecoverySeconds', 'BlinkArmBackDegrees',
          'BlinkBodyLeanDegrees', 'BlinkLoopSwayDegrees')
RESOURCE = '/Game/GuLiStrike/FX/GroundMech/Blink/NS_GroundMech_Blink.NS_GroundMech_Blink'
SCALE = 1.0  # Skeletal-mesh sampling already returns the production mech's world-space size.


def cli(*args):
    return subprocess.check_output([CLI, *map(str, args)], encoding='utf8')


def read(book, sheet):
    return json.loads(cli('read', book, '--sheet', sheet, '--format', 'json'))['rows']


def column(index):
    result = ''
    while index:
        index, rest = divmod(index - 1, 26)
        result = chr(65 + rest) + result
    return result


def write(book, sheet, cells):
    if not cells:
        return
    with tempfile.TemporaryDirectory(prefix='guli_blink_source_') as folder:
        path = Path(folder) / 'cells.json'
        path.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf8')
        cli('write', book, '--sheet', sheet, '--data-file', path)


def cell(c, r, value, kind='string'):
    return dict(cell=f'{column(c)}{r}', value=value, type=kind)


def migrate():
    if 'BlinkConcealSeconds' in read(MECH, '\u6280\u80fd\u8868')[0]:
        raise RuntimeError('Blink is now an endpoint teleport; run configure_blink_teleport_source.py. Legacy Loop/material authoring is retired.')
    effects = read(VFX, 'Effects')
    head = effects[0]
    named = [(i, dict(zip(head, row))) for i, row in enumerate(effects[3:], 4)
             if len(row) > 1 and row[1]]
    existing = next(((i, row) for i, row in named if row['name'] == 'GroundMechBlink'), None)
    if existing:
        _, effect = existing
        if effect['ResourcePath'] != RESOURCE:
            raise RuntimeError('Existing GroundMechBlink has a different resource; refusing to replace it')
        vfx_id = int(effect['id'])
    else:
        vfx_id = max(int(row['id']) for _, row in named) + 1
        line = max(i for i, _ in named) + 1
        data = dict(id=vfx_id, name='GroundMechBlink', ResourcePath=RESOURCE,
                    Note='Ground 闪现：GreenCrystalTrail 机甲适配；世界空间骨骼采样；基础缩放1；保留原绿色',
                    ScaleX=SCALE, ScaleY=SCALE, ScaleZ=SCALE)
        write(VFX, 'Effects', [cell(i + 1, line, data.get(key, 0 if effects[1][i] in ('int', 'float') else ''),
                                  'int' if effects[1][i] == 'int' else 'float' if effects[1][i] == 'float' else 'string')
                               for i, key in enumerate(head)])

    mobility = read(MECH, 'Mobility')
    old = dict(zip(mobility[0], mobility[3]))
    skills = read(MECH, '技能表')
    headers = list(skills[0])
    blink_line = next((i for i, row in enumerate(skills[3:], 4) if len(row) > 1 and row[1] == 'Blink'), None)
    cells = []
    for key in (*FIELDS, 'BlinkVfxId'):
        if key not in headers:
            headers.append(key)
            for row, value in enumerate((key, 'int' if key.endswith('VfxId') else 'float', 'Optional'), 1):
                cells.append(cell(len(headers), row, value))
    if blink_line is None:
        missing = [key for key in FIELDS if key not in old]
        if missing:
            raise RuntimeError(f'Blink has not been migrated but Mobility fields are missing: {missing}')
        blink_line = len(skills) + 1
        data = {key: float(old[key]) for key in FIELDS}
        data.update(id=max(int(row[0]) for row in skills[3:] if row and row[0]) + 1,
                    name='Blink', Note='Ground 闪现；距离cm，时间s，角度deg；Hybrid时钟；无消耗/伤害/无敌',
                    ExecutionType='GAS', AbilityClass='/Script/GuLiStrike.GuLiGA_Blink', BlinkVfxId=vfx_id)
        for i, key in enumerate(headers):
            kind = skills[1][i] if i < len(skills[1]) else 'int' if key.endswith('VfxId') else 'float'
            value = data.get(key, False if kind == 'bool' else 0 if kind in ('int', 'float') else '')
            cells.append(cell(i + 1, blink_line, value, kind if kind in ('bool', 'int', 'float') else 'string'))
    write(MECH, '技能表', cells)
    # Drop migrated and retired ghost columns from right to left; other tuning is untouched.
    for i in reversed(range(len(mobility[0]))):
        if mobility[0][i].startswith('Blink'):
            cli('delete-cols', MECH, '--sheet', 'Mobility', '--col', column(i + 1), '--count', 1)
    print(json.dumps({'skill': 'Blink', 'vfx_id': vfx_id, 'base_scale': SCALE}, ensure_ascii=False))


if __name__ == '__main__':
    migrate()
