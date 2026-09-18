"""Versioned, allow-listed Scale020 workbook migration through the project xlsx CLI.

No heuristic 'all numbers' scaling. Mesh-local sockets and time/economy fields are
explicitly excluded. The manifest stores original cell values and immutable
targets; re-running apply is a no-op, not another multiplication.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import subprocess
from decimal import Decimal
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = Path('D:/UE5.7/excelize-cli/bin/xlsx.exe')
OUT = ROOT / 'TestResults/Scale020'
MANIFEST = OUT / 'workbook-migration-v1.json'
FACTOR = Decimal('0.2')

def xyz(prefix):
    return [prefix + axis for axis in 'XYZ']

COLUMNS = {
    'GuLiStrikeCommander.xlsx': {'Soldiers': ['MovementSpeedCmPerSecond', 'PresentationScale']},
    'GuLiStrikeBuildings.xlsx': {'Buildings': xyz('CollisionExtent') + xyz('VisualOffset') + xyz('MeshScale') + ['ShieldRadius']},
    'GuLiStrikeShip.xlsx': {
        'Tuning': ['BaseMaxSpeed', 'BaseAcceleration'],
        'Camera': ['CameraDefaultArmLength', 'CameraZoomStep', 'CameraZoomMin', 'CameraZoomMax', 'CameraCollisionProbeRadius', 'CameraCollisionMinArm'],
    },
    'GuLiStrikeSecondaryWeapons.xlsx': {
        'UnitSkills': ['RangeCentimeters', 'ProjectileSpeedCentimetersPerSecond', 'ProjectileSweepRadiusCentimeters'],
        'Projectiles': ['SpeedCentimetersPerSecond', 'MinimumLiftHeightCentimeters', 'MaximumLiftHeightCentimeters', 'LateralOffsetCentimeters', 'ConvergenceDistanceCentimeters', 'SweepRadiusCentimeters'],
        'WingmanWeapons': ['RangeCentimeters', 'FlightSpeedCentimetersPerSecond', 'StripLengthCentimeters', 'PullUpHeightCentimeters', 'AirFireStartDistanceCentimeters', 'AirFireStopDistanceCentimeters', 'ProjectileSpeedCentimetersPerSecond', 'SweepRadiusCentimeters'] + xyz('Muzzle'),
        'WingmanTargeting': ['AcquireRadiusCentimeters', 'ReleaseRadiusCentimeters'],
    },
    'GuLiStrikeSecondaryUnitSkills.xlsx': {'Skills': ['RangeCentimeters', 'TargetAreaDiameterCentimeters']},
    'GuLiStrikeSpellFields.xlsx': {'Fields': ['RadiusCentimeters', 'BeamHeightCentimeters', 'MaxShipHeightCentimeters', 'LaneHeightCentimeters', 'ExitRadiusCentimeters']},
}

NOTE_TARGETS = {
    ('GuLiStrikeCommander.xlsx','Soldiers','ElectromagneticMiner'):
        '指挥官可控制矿车；Scale020后车长3.6米，900cm/s；采矿业务由矿车Manager调度',
    ('GuLiStrikeShip.xlsx','Camera','Dreadnought'):
        '无畏舰CombatAvatarFly-01；Scale020后使用最终厘米值，臂长5000、探测半径666.6666；角度不变',
    ('GuLiStrikeSecondaryUnitSkills.xlsx','Skills','WM01_HomingMissile'):
        '战争机器Q；每台独立6秒；射程取最终普通攻击x1.6（基线48米）；直径8米圆内独立随机落点，圆心验射程、落点允许越界；爆炸与预警半径1.6米，伤害与挂点引用武器槽',
}

def add_reviewed_annotations(manifest):
    """Append explicit text changes; never recapture or silently accept unrelated edits."""
    changed=False
    for book in manifest['books']:
        for sheet, rows in book['sheets_before'].items():
            headers=rows[0]
            for ri,row in enumerate(rows[3:],start=4):
                target=NOTE_TARGETS.get((Path(book['path']).name,sheet,row[headers.index('name')]))
                if target is None: continue
                ci=headers.index('Note');cell=f'{column_name(ci+1)}{ri}'
                existing=next((c for c in book['changes'] if c['sheet']==sheet and c['cell']==cell),None)
                if existing:
                    assert existing['target']==target,'Annotation target changed; explicit new revision required'
                    continue
                book['changes'].append({'sheet':sheet,'row_name':row[headers.index('name')],'field':'Note',
                    'cell':cell,'old':row[ci],'target':target,'space':'annotation','revision':'scale020-notes-v1'})
                changed=True
    if changed: MANIFEST.write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return manifest

def run_cli(*args):
    result = subprocess.run([str(CLI), *map(str, args)], check=True, capture_output=True, encoding='utf-8')
    return json.loads(result.stdout)

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def column_name(number):
    name = ''
    while number:
        number, rem = divmod(number - 1, 26)
        name = chr(65 + rem) + name
    return name

def read_rows(book, sheet):
    return run_cli('read', book, '--sheet', sheet, '--raw', '--format', 'json')['rows']

def capture():
    if MANIFEST.exists():
        return json.loads(MANIFEST.read_text(encoding='utf-8'))
    books = []
    for filename, sheets in COLUMNS.items():
        path = ROOT / 'Data/Excel' / filename
        info = run_cli('info', path)
        changes, baseline = [], {}
        for sheet, fields in sheets.items():
            rows = read_rows(path, sheet)
            baseline[sheet] = rows
            headers = rows[0]
            missing = set(fields) - set(headers)
            if missing:
                raise RuntimeError(f'{filename}/{sheet}: unknown columns {sorted(missing)}; do not guess')
            for index, row in enumerate(rows[3:], start=4):
                for field in fields:
                    col = headers.index(field)
                    value = row[col] if col < len(row) else ''
                    if value == '':
                        continue
                    target = str(Decimal(value) * FACTOR)
                    changes.append({'sheet': sheet, 'row_name': row[headers.index('name')], 'field': field,
                                    'cell': f'{column_name(col + 1)}{index}', 'old': value, 'target': target,
                                    'space': 'effective-model-scale' if 'Scale' in field else 'world-centimeters'})
        books.append({'path': str(path.relative_to(ROOT)).replace('\\', '/'), 'sha256_before': digest(path), 'info': info,
                      'sheets_before': baseline, 'changes': changes})
    manifest = {'schema': 'guli-scale020/workbooks-v1', 'factor': .2,
                'unchanged': ['time', 'counts', 'damage', 'health', 'angles', 'ratios', 'Ship.Parts.Muzzle', 'Ship.Tuning.HullMeshOffset', 'WeaponMounts (source mesh local; resolved once at load)'],
                'books': books}
    OUT.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return manifest

def check_sheet(book, sheet, before, changes):
    current = read_rows(book, sheet)
    if len(current) != len(before) or current[:3] != before[:3]:
        raise RuntimeError(f'{book.name}/{sheet}: schema or row count changed since capture')
    allowed = {c['cell']: c for c in changes}
    writes = []
    for ri, oldrow in enumerate(before):
        for ci in range(max(len(oldrow), len(current[ri]))):
            original = oldrow[ci] if ci < len(oldrow) else ''
            value = current[ri][ci] if ci < len(current[ri]) else ''
            cell = f'{column_name(ci + 1)}{ri + 1}'
            change = allowed.get(cell)
            if not change:
                if value != original:
                    raise RuntimeError(f'{book.name}/{sheet}!{cell}: unrelated concurrent edit; recapture/merge explicitly')
                continue
            if change.get('space')=='annotation':
                if value==change['target']: continue
                if value!=change['old']: raise RuntimeError(f'{book.name}/{sheet}!{cell}: unexpected annotation edit')
                writes.append({'cell':cell,'value':change['target'],'type':'string'})
                continue
            actual = Decimal(value)
            if abs(actual - Decimal(change['target'])) <= Decimal('1e-10'):
                continue
            if abs(actual - Decimal(change['old'])) > Decimal('1e-10'):
                raise RuntimeError(f'{book.name}/{sheet}!{cell}: unexpected value {value}')
            writes.append({'cell': cell, 'value': float(Decimal(change['target'])), 'type': 'float'})
    return writes

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    manifest = add_reviewed_annotations(capture())
    batches = []
    # Validate the whole batch before any workbook writes.
    for book in manifest['books']:
        path = ROOT / book['path']
        for sheet, before in book['sheets_before'].items():
            changes = [c for c in book['changes'] if c['sheet'] == sheet]
            writes = check_sheet(path, sheet, before, changes)
            batches.append((path, sheet, before, changes, writes))
    count = sum(len(b[4]) for b in batches)
    if args.apply:
        for path, sheet, before, changes, writes in batches:
            if not writes:
                continue
            batch_path = OUT / f'{path.stem}-{sheet}-cells.json'
            batch_path.write_text(json.dumps(writes, ensure_ascii=False, indent=2), encoding='utf-8')
            subprocess.run([str(CLI), 'write', str(path), '--sheet', sheet, '--data-file', str(batch_path)], check=True)
            if check_sheet(path, sheet, before, changes):
                raise RuntimeError(f'{path.name}/{sheet}: readback not at target')
    report = {'schema': 'guli-scale020/workbook-readback-v1', 'applied': args.apply, 'cells_to_write': count,
              'books': [{'path': b['path'], 'sha256': digest(ROOT/b['path'])} for b in manifest['books']]}
    (OUT / ('workbook-apply.json' if args.apply else 'workbook-dry-run.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    main()
