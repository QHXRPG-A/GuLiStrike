"""Merge the four teleport tiers into SpellFields with the project's excelize CLI."""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
XLSX = Path('D:/UE5.7/excelize-cli/bin/xlsx.exe')
BOOK = ROOT / 'Data/Excel/GuLiStrikeSpellFields.xlsx'
SHEET = 'Fields'
LEGACY_SHEET = 'TeleportFields'
columns = [
    ('id', 'int', True), ('name', 'str', True), ('Note', 'str', False),
    ('FieldType', 'str', True), ('Damage', 'float', False),
    ('RadiusCentimeters', 'float', True), ('Timing', 'str', False),
    ('DelaySeconds', 'float', False), ('DurationSeconds', 'float', False),
    ('PulseIntervalSeconds', 'float', False), ('DissipationSeconds', 'float', False),
    ('Level', 'int', False), ('bAllowPlayerVehicles', 'bool', False),
    ('WindupSeconds', 'float', False), ('RecoverySeconds', 'float', False),
    ('BeamHeightCentimeters', 'float', False), ('MaxTargetWaitSeconds', 'float', False),
    ('MaxShipHeightCentimeters', 'float', False),
]

def read_existing_combat_rows():
    result = json.loads(subprocess.check_output(
        [str(XLSX), 'read', str(BOOK), '--sheet', SHEET, '--format', 'json'],
        encoding='utf-8'))
    matrix = result['rows']
    headers = matrix[0]
    preserved = []
    for values in matrix[3:]:
        record = dict(zip(headers, values))
        name = str(record.get('name', '')).strip()
        field_type = str(record.get('FieldType', '')).strip()
        if not name or name.startswith('Teleport_L') or field_type.lower() == 'teleport':
            continue
        record['FieldType'] = field_type or 'Combat'
        preserved.append(record)
    return preserved

def typed_value(raw, value_type):
    if raw is None or raw == '':
        return None
    if value_type == 'bool':
        return raw if isinstance(raw, bool) else str(raw).lower() == 'true'
    if value_type == 'int':
        return int(float(raw))
    if value_type == 'float':
        return float(raw)
    return str(raw)

records = read_existing_combat_rows()
radii = [4000, 10000, 20000, 50000]
for level, radius in enumerate(radii, 1):
    records.append({
        'id': 1000 + level, 'name': f'Teleport_L{level}',
        'Note': '指挥官双点传送；4级额外允许玩家WM/Ship及所属僚机',
        'FieldType': 'Teleport', 'RadiusCentimeters': radius, 'Level': level,
        'bAllowPlayerVehicles': level == 4, 'WindupSeconds': 3,
        'RecoverySeconds': .5, 'BeamHeightCentimeters': 50000,
        'MaxTargetWaitSeconds': 10, 'MaxShipHeightCentimeters': 10000,
    })

rows = [[x[0] for x in columns], [x[1] for x in columns],
        ['Necessary' if x[2] else 'Optional' for x in columns]]
rows.extend([[typed_value(record.get(name), kind) for name, kind, _ in columns]
             for record in records])
cells = [{'cell': f'{chr(65 + c)}{r + 1}', 'value': value,
          'type': 'string' if isinstance(value, str) else 'bool' if isinstance(value, bool) else 'int' if isinstance(value, int) else 'float'}
         for r, row in enumerate(rows) for c, value in enumerate(row) if value is not None]
output = ROOT / 'outputs/teleport'
output.mkdir(parents=True, exist_ok=True)
cell_file = output / 'teleport_cells.json'
cell_file.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf-8')
subprocess.run([str(XLSX), 'write', str(BOOK), '--sheet', SHEET, '--data-file', str(cell_file)], check=True)
info = json.loads(subprocess.check_output([str(XLSX), 'info', str(BOOK)], encoding='utf-8'))
if LEGACY_SHEET in [sheet['name'] for sheet in info['sheets']]:
    subprocess.run([str(XLSX), 'del-sheet', str(BOOK), '--name', LEGACY_SHEET], check=True)
subprocess.run([str(XLSX), 'read', str(BOOK), '--sheet', SHEET, '--format', 'tsv'], check=True)
