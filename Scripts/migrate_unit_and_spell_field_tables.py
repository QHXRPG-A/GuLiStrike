"""Migrate Commander units and shared spell fields through the project's excelize CLI."""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = 'D:/UE5.7/excelize-cli/bin/xlsx.exe'
OUT = ROOT / 'outputs/unit-data-mining'
COMMANDER = ROOT / 'Data/Excel/GuLiStrikeCommander.xlsx'
SHIP = ROOT / 'Data/Excel/GuLiStrikeShip.xlsx'
GLOBAL = ROOT / 'Data/Excel/GuLiStrikeSpellFields.xlsx'


def run(*args):
    return subprocess.check_output([CLI, *map(str, args)], encoding='utf-8')


def read(book, sheet):
    return json.loads(run('read', book, '--sheet', sheet, '--format', 'json', '--raw'))['rows']


def column(index):
    result = ''
    while index:
        index, digit = divmod(index - 1, 26)
        result = chr(65 + digit) + result
    return result


def write(book, sheet, matrix):
    cells = []
    for r, row in enumerate(matrix):
        for c, value in enumerate(row):
            kind = 'string'
            if r >= 3 and value != '':
                source_type = matrix[1][c]
                if source_type == 'float': value, kind = float(value), 'float'
                elif source_type == 'int': value, kind = int(float(value)), 'int'
                elif source_type == 'bool': value, kind = str(value).lower() in ('true', '1'), 'bool'
            cells.append({'cell': f'{column(c + 1)}{r + 1}', 'value': value, 'type': kind})
    path = OUT / f'{book.stem}-{sheet}-cells.json'
    path.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf-8')
    run('write', book, '--sheet', sheet, '--data-file', path)


def main():
    if (ROOT / 'Data/Excel/GuLiStrikeSecondaryWeapons.xlsx').exists():
        print('Superseded by migrate_secondary_weapon_tables.py; existing unit/field data left intact.')
        return
    OUT.mkdir(parents=True, exist_ok=True)
    if not GLOBAL.exists():
        fields = read(COMMANDER, 'SpellFields')
        run('new', GLOBAL, '--sheet', 'Fields')
        write(GLOBAL, 'Fields', fields)
    fields = read(GLOBAL, 'Fields')
    if not any(row[1] == 'WingmanGroundMissile' for row in fields[3:]):
        weapons = read(SHIP, 'WingmanWeapons')
        source = next(dict(zip(weapons[0], row)) for row in weapons[3:] if row[1] == 'WingmanGroundMissile')
        bomb = {'id': 2, 'name': 'WingmanGroundMissile', 'Note': 'Ship僚机对地轰炸；全局共享AOE基础伤害、半径与时序',
                'FieldType': 'Combat', 'Damage': source['Damage'], 'RadiusCentimeters': source['ExplosionRadiusCentimeters'],
                'Timing': 'Instant', 'DelaySeconds': 0, 'DurationSeconds': 0, 'PulseIntervalSeconds': 1, 'DissipationSeconds': 3}
        fields.append([bomb.get(key, '') for key in fields[0]])
        write(GLOBAL, 'Fields', fields)
    if 'SpellFields' in json.loads(run('sheets', COMMANDER, '--json'))['sheets']:
        run('del-sheet', COMMANDER, '--name', 'SpellFields')

    soldiers = read(COMMANDER, 'Soldiers')
    soldiers[2][soldiers[0].index('ModelAsset')] = 'Optional'
    for name, kind in [('ActorClass', 'softclass'), ('PresentationClass', 'softclass'), ('PresentationScale', 'float')]:
        if name not in soldiers[0]:
            soldiers[0].append(name)
            soldiers[1].append(kind)
            soldiers[2].append('Optional' if kind == 'softclass' else 'Necessary')
            for row in soldiers[3:]:
                row.extend([''] * (len(soldiers[0]) - len(row)))
                row[-1] = 1 if kind == 'float' else ''
    miner = {'id': 3, 'name': 'ElectromagneticMiner', 'Note': '指挥官可控制矿车；原车18米，4500cm/s；采矿业务由矿车Manager调度',
             'MovementSpeedCmPerSecond': 4500, 'MaxHealth': 1000, 'Defense': 0,
             'ActorClass': '/Script/GuLiStrike.GuLiMiningVehiclePawn',
             'PresentationClass': '/Game/GuLiStrike/Vehicles/ElectromagneticMiner/BP_MiningVehicle_TransporterLvl2.BP_MiningVehicle_TransporterLvl2_C',
             'PresentationScale': 1800 / 591.6596}
    row = [miner.get(key, '') for key in soldiers[0]]
    index = next((i for i in range(3, len(soldiers)) if soldiers[i][1] == miner['name']), None)
    if index is None: soldiers.append(row)
    else: soldiers[index] = row
    write(COMMANDER, 'Soldiers', soldiers)

    weapons = read(SHIP, 'WingmanWeapons')
    if 'ExplosionRadiusCentimeters' in weapons[0]:
        index = weapons[0].index('ExplosionRadiusCentimeters')
        run('delete-cols', SHIP, '--sheet', 'WingmanWeapons', '--col', column(index + 1), '--count', 1)
        weapons = read(SHIP, 'WingmanWeapons')
    if 'EffectConfigId' not in weapons[0]:
        weapons[0].append('EffectConfigId'); weapons[1].append('str'); weapons[2].append('Optional')
    weapons[2][weapons[0].index('Damage')] = 'Optional'
    for row in weapons[3:]:
        row.extend([''] * (len(weapons[0]) - len(row)))
        if row[weapons[0].index('AttackPattern')] == 'GroundDive':
            row[weapons[0].index('Damage')] = ''
            row[weapons[0].index('EffectConfigId')] = 'WingmanGroundMissile'
    write(SHIP, 'WingmanWeapons', weapons)
    run('style', GLOBAL, '--sheet', 'Fields', '--range', 'A1:R3', '--bold', '--bg', 'D9EAD3', '--wrap')
    run('col-width', GLOBAL, '--sheet', 'Fields', '--col', 'A', '--to', 'R', '--width', 22)
    run('col-width', GLOBAL, '--sheet', 'Fields', '--col', 'C', '--width', 60)
    for book, sheet in [(COMMANDER, 'Soldiers'), (GLOBAL, 'Fields'), (SHIP, 'WingmanWeapons')]:
        data = read(book, sheet)
        (OUT / f'{book.stem}-{sheet}-readback.json').write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
        print(f'{book.name}/{sheet}: {len(data)-3} rows, {len(data[0])} columns')


if __name__ == '__main__':
    main()
