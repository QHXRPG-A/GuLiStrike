"""Idempotent WM01 v3 migration in the three existing workbooks, via excelize."""
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Artifacts/WM01MissileCards/Visual_v3/DataSource'
CLI = 'D:/UE5.7/excelize-cli/bin/xlsx.exe'
VISUAL_FIELDS = {
    'SmokeInitialWidthCentimeters': 63,
    'SmokeMaximumWidthCentimeters': 99,
    'FlameWidthCentimeters': 42,
    'FlameLengthCentimeters': 108,
}


def column(index):
    result = ''
    while index:
        index, remainder = divmod(index - 1, 26)
        result = chr(65 + remainder) + result
    return result


def cli(*args):
    return subprocess.check_output([CLI, *map(str, args)], encoding='utf-8')


def edit(book, sheet, key, values, add_visual=False):
    path = ROOT / 'Data/Excel' / book
    assert path.is_file(), 'New workbooks require explicit approval'
    info = json.loads(cli('info', path))
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    rows = json.loads(cli('read', path, '--sheet', sheet, '--format', 'json', '--raw'))['rows']
    names = list(rows[0])
    target = [i for i, row in enumerate(rows[3:], 4) if row and row[names.index('name')] == key]
    assert len(target) == 1, (book, sheet, key)
    changes = []

    def put(row, name, value, kind):
        changes.append({'cell': column(names.index(name) + 1) + str(row), 'value': value, 'type': kind})

    if add_visual:
        for name in VISUAL_FIELDS:
            if name not in names:
                names.append(name)
            for row, value in [(1, name), (2, 'float'), (3, 'Optional')]:
                put(row, name, value, 'string')
    old = dict(zip(rows[0], rows[target[0] - 1]))
    for name, (before, after) in values.items():
        assert name in names
        if before is not None:
            assert float(old[name]) in (before, after), (name, old[name])
        put(target[0], name, after, 'string' if isinstance(after, str) else 'float')
    payload = OUT / f'{Path(book).stem}-{sheet}-cells.json'
    payload.write_text(json.dumps(changes, ensure_ascii=False, indent=2), encoding='utf-8')
    assert hashlib.sha256(path.read_bytes()).hexdigest() == digest, 'Workbook changed during migration'
    cli('write', path, '--sheet', sheet, '--data-file', payload)
    after = json.loads(cli('read', path, '--sheet', sheet, '--format', 'json', '--raw'))['rows']
    actual = dict(zip(after[0], after[target[0] - 1]))
    for name, (_, value) in values.items():
        assert actual[name] == value if isinstance(value, str) else float(actual[name]) == value
    return {'book': book, 'sheet': sheet, 'sha256_before': digest,
            'sha256_after': hashlib.sha256(path.read_bytes()).hexdigest(),
            'info_before': info, 'before': old, 'after': actual}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    report = []
    report.append(edit('GuLiStrikeSecondaryWeapons.xlsx', 'Projectiles', 'WM01_Missile', {
        'SpeedCentimetersPerSecond': (1200, 2400),
        **{name: (None, value) for name, value in VISUAL_FIELDS.items()},
    }, add_visual=True))
    report.append(edit('GuLiStrikeSecondaryUnitSkills.xlsx', 'Skills', 'WM01_HomingMissile', {
        'RangeMultiplier': (1.6, 3.2), 'RangeCentimeters': (4800, 9600),
        'Note': (None, '重防号Q；每台独立6秒；射程取最终普通攻击x3.2（基线96米）；直径16米圆内独立随机落点，圆心验射程、落点允许越界；爆炸与预警半径3米，伤害与挂点引用武器槽'),
    }))
    report.append(edit('GuLiStrikeSpellFields.xlsx', 'Fields', 'WM01_MissileExplosion', {
        'RadiusCentimeters': (200, 300),
    }))
    report.append(edit('GuLiStrikeSecondaryWeapons.xlsx', 'UnitSkills', 'WM01_MissileLauncher', {
        'Note': (None, '主动Q导弹；射程与冷却取SecondaryUnitSkills/Skills；保留本槽伤害升级与挂点，自动攻速列不参与Q冷却'),
    }))
    destination = OUT / 'migration.json'
    # Preserve the first pre-v3 cell values if the fixed-value migration is rerun.
    if not destination.exists():
        destination.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print('WM01 v3: verified 3 existing workbooks; no workbook or sheet created')


if __name__ == '__main__':
    main()
