"""Remove retired Ground model routes through the project's Excel authoring CLI."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
BOOK = ROOT / 'data/Excel/GuLiStrikeModels.xlsx'
OUT = ROOT / 'Artifacts/GroundMechLegacyCleanup_20261028'
CLI = Path('D:/UE5.7/excelize-cli/bin/xlsx.exe')
RETIRED = {'4002', '4003', '4004'}

def run(*args):
    return subprocess.run([str(CLI), *map(str, args)], check=True, capture_output=True,
                          encoding='utf-8').stdout

def rows(sheet):
    return json.loads(run('read', BOOK, '--sheet', sheet, '--format', 'json', '--raw'))['rows']

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    result = []
    for sheet, key in [('Models', 'id'), ('Parts', 'ModelId'), ('MaterialParameters', 'ModelId')]:
        data = rows(sheet)
        column = data[0].index(key)
        removed = [index for index, row in enumerate(data[3:], 4)
                   if len(row) > column and row[column] in RETIRED]
        for index in reversed(removed):
            run('delete-rows', BOOK, '--sheet', sheet, '--at', index, '--count', 1)
        data = rows(sheet)
        edits = []
        if sheet == 'Parts':
            for index, row in enumerate(data[3:], 4):
                if len(row) > column and row[column] == '4001':
                    edits.append({'cell': f'C{index}', 'value': 'Ground当前唯一整机A4 B2，含双枪和喷气口；旧分件ID4002-4004已退役', 'type': 'string'})
        if edits:
            edits_path = OUT / 'parts_notes.json'
            edits_path.write_text(json.dumps(edits, ensure_ascii=False), encoding='utf-8')
            run('write', BOOK, '--sheet', sheet, '--data-file', edits_path)
        after = rows(sheet)
        assert all(len(row) <= column or row[column] not in RETIRED for row in after[3:])
        result.append({'sheet': sheet, 'removed_rows': len(removed)})
    models = rows('Models')
    active = next(row for row in models[3:] if row and row[0] == '4001')
    assert '/MechaDestroyer_A4B2/' in active[5]
    report = {'retired_ids': sorted(RETIRED), 'changes': result, 'retained_model': active[:6]}
    (OUT / 'excel_retirement.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))

if __name__ == '__main__':
    main()
