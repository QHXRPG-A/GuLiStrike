"""Update only the five approved card descriptions through excelize; no UE writes."""
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = Path('D:/UE5.7/excelize-cli/bin/xlsx.exe')
DESCRIPTION_PATTERNS = {
    'FireRate': '<Unit>重防号</> <Gain>普攻射速+{0}%</>',
    'HighSpeed': '<Unit>重防号</> <Gain>移动速度+{0}%</>',
    'MissileDamage': '<Unit>重防号</> <Gain>导弹伤害+{0}%</>',
    'MissilePod': '<Unit>重防号</> 解锁<Gain>导弹技能</>',
    'RainSalvo': '<Unit>重防号</> <Gain>导弹齐射数量+{0}</>',
}


def read_rows(path, sheet):
    output = subprocess.check_output([str(CLI), 'read', str(path), '--sheet', sheet,
                                      '--format', 'json'], encoding='utf-8')
    return json.loads(output)['rows']


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    path = ROOT / 'data/Excel/GuLiStrikeGameTexts.xlsx'
    cards_path = ROOT / 'data/Excel/GuLiStrikeRogueCards.xlsx'
    out = ROOT / 'Artifacts/RogueCards/SingleLineText'
    out.mkdir(parents=True, exist_ok=True)
    before_hash, cards_hash = digest(path), digest(cards_path)
    rows = read_rows(path, 'Texts')
    expected = [row.copy() for row in rows]
    keys = {row[0]: index for index, row in enumerate(rows) if index >= 3 and row}
    changes, previous, patterns = [], {}, {}
    for slug, pattern in DESCRIPTION_PATTERNS.items():
        key = f'Card.WM01.{slug}.Description'
        assert key in keys, f'Missing existing text key: {key}'
        index = keys[key]
        assert not any(char in pattern for char in '\r\n\t')
        assert re.findall(r'<([^/>]+)>', pattern) == ['Unit', 'Gain']
        assert pattern.count('</>') == 2
        assert pattern.count('{0}') == (0 if slug == 'MissilePod' else 1)
        previous[key], patterns[key] = rows[index][2], pattern
        expected[index][2] = pattern
        changes.append({'cell': f'C{index + 1}', 'value': pattern, 'type': 'string'})
    # Detect another editor changing the source after the initial read.
    assert digest(path) == before_hash, 'Text workbook changed during preparation'
    with tempfile.TemporaryDirectory() as folder:
        cells = Path(folder) / 'cells.json'
        cells.write_text(json.dumps(changes, ensure_ascii=False), encoding='utf-8')
        subprocess.run([str(CLI), 'write', str(path), '--sheet', 'Texts',
                        '--data-file', str(cells)], check=True)
    actual = read_rows(path, 'Texts')
    assert actual == expected, 'Workbook readback differs beyond the five approved descriptions'
    assert digest(cards_path) == cards_hash, 'Card gameplay workbook must remain unchanged'
    card_rows = read_rows(cards_path, 'Cards')
    formatted = {}
    for values in card_rows[3:]:
        row = dict(zip(card_rows[0], values))
        key = json.loads(row['TextIds'])[1]
        if key not in patterns:
            continue
        number = (format(float(row['BonusPercent']) * 100, '.2f').rstrip('0').rstrip('.')
                  if float(row['BonusPercent']) else str(int(row['BonusCount'])))
        formatted[row['id']] = re.sub(r'<[^>]+>', '', patterns[key].replace('{0}', number))
    report = {'success': True, 'before_sha256': before_hash, 'after_sha256': digest(path),
              'card_workbook_sha256': cards_hash, 'changed_cells': changes,
              'previous': previous, 'patterns': patterns, 'plain_samples': formatted,
              'readback': 'all rows compared; only five description cells changed'}
    (out / 'text-source.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'success': True, 'plain_samples': formatted}, ensure_ascii=False))


if __name__ == '__main__':
    main()
