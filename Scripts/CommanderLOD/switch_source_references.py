"""Switch verified complete groups in Excel; retain exact per-group rollback cells."""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / 'ArtSource/CommanderLOD_20261005'
XLSX = 'D:/UE5.7/excelize-cli/bin/xlsx.exe'
assert json.loads((ART / 'approval_B.json').read_text(encoding='utf8'))['approval_B'] == 'approved'
fields = ('ModelAsset', 'PresentationClass', 'VATDefinition')
source_files = ('DT_GuLiStrikeCommander_Soldiers', 'DT_GuLiStrikeBuildings_Buildings')
snapshot_file = ART / 'Reports/source_tables_before_switch.json'
if not snapshot_file.exists():
    snapshot = {name: json.loads((ROOT / ('Data/Json/' + name + '.json')).read_text(encoding='utf8')) for name in source_files}
    snapshot_file.write_text(json.dumps(snapshot, ensure_ascii=False, indent=2), encoding='utf8')
snapshot = json.loads(snapshot_file.read_text(encoding='utf8'))
current = json.loads((ROOT / 'Data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf8'))
old = {r['Name']: r for r in snapshot['DT_GuLiStrikeCommander_Soldiers']}
book = ROOT / 'Data/Excel/GuLiStrikeCommander.xlsx'
sheet = json.loads(subprocess.check_output([XLSX, 'read', str(book), '--sheet', 'Soldiers', '--format', 'json', '--raw'], encoding='utf8'))['rows']

def column(index):
    output = ''
    while index:
        index, remainder = divmod(index-1, 26)
        output = chr(65+remainder) + output
    return output

selected = sys.argv[1:] or [r['Name'] for r in current]
cells = []
rollback = []
groups = []
for name in selected:
    ready = json.loads((ART / ('Reports/formal_prepared_' + name + '.json')).read_text(encoding='utf8'))
    assert ready['success'] and ready['state'] in ('prepared_verified', 'native_configured_verified'), name
    row_number = next(i for i, r in enumerate(sheet, 1) if len(r) > 1 and r[1] == name)
    before = next(r for r in current if r['Name'] == name)
    after = dict(ModelAsset=ready['model_asset'], PresentationClass=ready.get('presentation_class', ''), VATDefinition=ready['vat_definition'])
    for field in fields:
        index = sheet[0].index(field)
        value = sheet[row_number-1][index] if len(sheet[row_number-1]) > index else ''
        assert value == before[field], (name, field, 'Excel/JSON disagree')
        cell = column(index+1) + str(row_number)
        cells.append(dict(cell=cell, value=after[field], type='string'))
        rollback.append(dict(cell=cell, value=before[field], type='string'))
    groups.append(dict(id=before['Id'], name=name, before={f: before[f] for f in fields}, after=after))
data = ART / 'Reports/soldier_reference_cells.json'
data.write_text(json.dumps(cells, ensure_ascii=False, indent=2), encoding='utf8')
construction = None
if 'BiZhiMao' in selected:
    building_book = ROOT / 'Data/Excel/GuLiStrikeBuildings.xlsx'
    building_sheet = json.loads(subprocess.check_output([XLSX, 'read', str(building_book), '--sheet', 'Buildings', '--format', 'json', '--raw'], encoding='utf8'))['rows']
    buildings = json.loads((ROOT / 'Data/Json/DT_GuLiStrikeBuildings_Buildings.json').read_text(encoding='utf8'))
    original = next(r for r in buildings if r['Id'] == 8)
    ready = json.loads((ART / 'Reports/formal_prepared_BiZhiMao.json').read_text(encoding='utf8'))
    mesh = ready['formal_root'] + '/Meshes/SM_BiZhiMao_Construction.SM_BiZhiMao_Construction'
    mesh_index = building_sheet[0].index('Mesh')
    id_index = building_sheet[0].index('id')
    row_number = next(i for i, r in enumerate(building_sheet, 1) if len(r) > id_index and str(r[id_index]) == '8')
    assert building_sheet[row_number-1][mesh_index] == original['Mesh']
    cell = column(mesh_index+1) + str(row_number)
    construction = dict(id=8, name=original['Name'], before=original['Mesh'], after=mesh,
        cells=[dict(cell=cell, value=mesh, type='string')],
        rollback_cells=[dict(cell=cell, value=original['Mesh'], type='string')])
    (ART / 'Reports/building_reference_cells.json').write_text(json.dumps(construction['cells'], ensure_ascii=False, indent=2), encoding='utf8')
report = dict(state='source_cells_prepared', groups=groups, rollback_cells=rollback,
    construction=construction, native_imported=False, pending_groups=[r['Name'] for r in current if r['Name'] not in selected and all(r[f] == old[r['Name']][f] for f in fields)])
report_file = ART / 'Reports/source_reference_switch.json'
if report_file.exists() and json.loads(report_file.read_text(encoding='utf8'))['state'].startswith('rolled_back'):
    (ART / 'Reports/source_reference_switch_first_rollback.json').write_text(report_file.read_text(encoding='utf8'), encoding='utf8')
report_file.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
subprocess.run([XLSX, 'write', str(book), '--sheet', 'Soldiers', '--data-file', str(data)], check=True)
if construction:
    subprocess.run([XLSX, 'write', str(building_book), '--sheet', 'Buildings', '--data-file', str(ART / 'Reports/building_reference_cells.json')], check=True)
result = subprocess.run([sys.executable, '-X', 'utf8', str(ROOT / 'Tools/DataPipeline/export_data_from_excel.py')], cwd=ROOT, text=True, encoding='utf8', capture_output=True)
(ART / 'Reports/source_export.log').write_text(result.stdout + result.stderr, encoding='utf8')
if result.returncode:
    revert = ART / 'Reports/soldier_reference_rollback_cells.json'
    revert.write_text(json.dumps(rollback, ensure_ascii=False, indent=2), encoding='utf8')
    subprocess.run([XLSX, 'write', str(book), '--sheet', 'Soldiers', '--data-file', str(revert)], check=True)
    if construction:
        revert_building = ART / 'Reports/building_reference_rollback_cells.json'
        revert_building.write_text(json.dumps(construction['rollback_cells'], ensure_ascii=False, indent=2), encoding='utf8')
        subprocess.run([XLSX, 'write', str(building_book), '--sheet', 'Buildings', '--data-file', str(revert_building)], check=True)
    subprocess.run([sys.executable, '-X', 'utf8', str(ROOT / 'Tools/DataPipeline/export_data_from_excel.py')], cwd=ROOT, check=True)
    raise RuntimeError('Export failed; all affected Excel group references restored. See source_export.log.')
updated = json.loads((ROOT / 'Data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf8'))
for row in updated:
    original = old[row['Name']]
    assert {k: v for k, v in row.items() if k not in fields} == {k: v for k, v in original.items() if k not in fields}, row['Name']
    if row['Name'] in selected:
        expected = next(g['after'] for g in groups if g['name'] == row['Name'])
        assert {f: row[f] for f in fields} == expected
    elif row['Name'] == 'BiZhiMao':
        assert {f: row[f] for f in fields} == {f: original[f] for f in fields}
building_rows = json.loads((ROOT / 'Data/Json/DT_GuLiStrikeBuildings_Buildings.json').read_text(encoding='utf8'))
old_buildings = {r['Name']:r for r in snapshot['DT_GuLiStrikeBuildings_Buildings']}
for row in building_rows:
    if construction and row['Id'] == 8:
        assert row['Mesh'] == construction['after']
        assert {k:v for k,v in row.items() if k != 'Mesh'} == {k:v for k,v in old_buildings[row['Name']].items() if k != 'Mesh'}
    else:
        assert row == old_buildings[row['Name']]
report['state'] = 'source_exported_verified'
report_file.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
print(json.dumps(dict(success=True, state=report['state'], groups=[g['name'] for g in groups], pending_groups=report['pending_groups'], gameplay_values_unchanged=True), ensure_ascii=False))
