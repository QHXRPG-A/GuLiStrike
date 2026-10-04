"""Apply the approved WM01 guidance rules through excelize; preserve other source cells."""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = Path('D:/UE5.7/excelize-cli/bin/xlsx.exe')
OUT = ROOT / 'Artifacts/WM01Guidance60'


def command(*args):
    return subprocess.check_output([str(CLI), *map(str, args)], encoding='utf-8')


def column(index):
    result = ''
    while index:
        index, rest = divmod(index - 1, 26)
        result = chr(65 + rest) + result
    return result


def read(book, sheet):
    path = ROOT / 'data/Excel' / book
    return path, json.loads(command('read', path, '--sheet', sheet, '--format', 'json', '--raw'))['rows']


def write(path, sheet, changes):
    cells = OUT / (path.stem + '-cells.json')
    cells.write_text(json.dumps(changes, ensure_ascii=False, indent=2), encoding='utf-8')
    command('write', path, '--sheet', sheet, '--data-file', cells)


def cell(address, value, kind='string'):
    return {'cell': address, 'value': value, 'type': kind}


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    path, rows = read('GuLiStrikeSecondaryUnitSkills.xlsx', 'Skills')
    header = rows[0]
    field = 'MaxProjectilesPerActivation'
    col = column(header.index(field) + 1 if field in header else len(header) + 1)
    changes = [cell(col+'1', field), cell(col+'2', 'int'), cell(col+'3', 'Optional')]
    for index, row in enumerate(rows[3:], 4):
        if not row or not row[0]:
            continue
        missile = row[header.index('name')] == 'WM01_HomingMissile'
        if missile or field not in header:
            changes.append(cell(f'{col}{index}', 60 if missile else 0, 'int'))
        if missile:
            note_col = column(header.index('Note')+1)
            changes.append(cell(f'{note_col}{index}', '重防号Q；每圈最多60发，整台齐射不拆分，未分配单位留待下一Q；成功单位独立6秒冷却；共享引导圈半径8米，保留单弹3米预警；射程取最终普攻x3.2，落点在直径16米内随机。'))
    write(path, 'Skills', changes)

    path, rows = read('GuLiStrikeRogueCards.xlsx', 'Cards')
    header = rows[0]
    index = next(i for i, row in enumerate(rows, 1) if row and row[0] == '05.01')
    write(path, 'Cards', [cell(f"{column(header.index('MaxAcquisitions')+1)}{index}", 59, 'int'),
        cell(f"{column(header.index('Note')+1)}{index}", '己方本局当前及后续重防号：雨点攻势。完整齐射数量每次加1，基础1发，每队本局最多59次，单台上限60发。')])

    path, rows = read('GuLiStrikeGameTexts.xlsx', 'Texts')
    key = 'UI.CommanderSkills.GuidanceBatch'
    index = next((i for i, row in enumerate(rows, 1) if row and row[0] == key), len(rows)+1)
    write(path, 'Texts', [cell(f'A{index}', key), cell(f'B{index}', '重防号Q本批导弹及下一Q就绪单位反馈'),
        cell(f'C{index}', ' · 本次发射{0}发导弹，{1}台留待下次Q')])
    print('WM01_GUIDANCE_SOURCE_UPDATED')


if __name__ == '__main__':
    main()
