"""Idempotently mark WM01 missiles active; all workbook writes use excelize."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'Data/Excel/GuLiStrikeSecondaryWeapons.xlsx'
CLI = 'D:/UE5.7/excelize-cli/bin/xlsx.exe'
OUT = ROOT / 'outputs/wm01_q'


def read(sheet):
    return json.loads(subprocess.check_output([CLI, 'read', str(BOOK), '--sheet', sheet, '--format', 'json', '--raw'], encoding='utf-8'))['rows']


def column(index):
    value = ''
    while index:
        index, digit = divmod(index - 1, 26)
        value = chr(65 + digit) + value
    return value


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for sheet in ('UnitSkills', 'Skills'):
        rows = read(sheet)
        columns = rows[0]
        edits = []
        if sheet == 'UnitSkills':
            index = columns.index('TriggerMode') if 'TriggerMode' in columns else len(columns)
            for row, value in enumerate(('TriggerMode', 'str', 'Optional'), 1):
                edits.append({'cell': f'{column(index + 1)}{row}', 'value': value, 'type': 'string'})
            for row, values in enumerate(rows[3:], 4):
                if not values or not values[0]:
                    continue
                mode = 'Active' if values[columns.index('name')] == 'WM01_MissileLauncher' else (values[index] if len(values) > index and values[index] else 'Automatic')
                edits.append({'cell': f'{column(index + 1)}{row}', 'value': mode, 'type': 'string'})
                if values[columns.index('name')] == 'WM01_MissileLauncher':
                    edits.append({'cell': f'{column(columns.index("Note") + 1)}{row}', 'value': '主动Q导弹；技能CD6秒、普攻射程x0.8；保留本槽伤害升级与挂点，自动攻速列不参与Q冷却', 'type': 'string'})
        else:
            for row, values in enumerate(rows[3:], 4):
                if values[columns.index('name')] == 'WM01_HomingMissile':
                    edits.append({'cell': f'{column(columns.index("Note") + 1)}{row}', 'value': '战争机器Q主动导弹；定点弹道，爆炸由独立法术场结算', 'type': 'string'})
        data = OUT / f'{sheet}-edits.json'
        data.write_text(json.dumps(edits, ensure_ascii=False), encoding='utf-8')
        subprocess.run([CLI, 'write', str(BOOK), '--sheet', sheet, '--data-file', str(data)], check=True)
    rows = read('UnitSkills')
    assert rows[0][-1] == 'TriggerMode'
    assert next(r for r in rows[3:] if r[1] == 'WM01_MissileLauncher')[-1] == 'Active'
    print('WM01 source workbook updated and read back.')


if __name__ == '__main__':
    main()
