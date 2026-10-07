"""Restore the attempted source group switch before a separately permitted build."""
import json
import subprocess
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / 'ArtSource/CommanderLOD_20261005'
report = json.loads((ART / 'Reports/source_reference_switch.json').read_text(encoding='utf8'))
cells = ART / 'Reports/soldier_reference_rollback_cells.json'
cells.write_text(json.dumps(report['rollback_cells'], ensure_ascii=False, indent=2), encoding='utf8')
subprocess.run(['D:/UE5.7/excelize-cli/bin/xlsx.exe', 'write', str(ROOT / 'Data/Excel/GuLiStrikeCommander.xlsx'),
    '--sheet', 'Soldiers', '--data-file', str(cells)], check=True)
if report.get('construction'):
    building_cells = ART / 'Reports/building_reference_rollback_cells.json'
    building_cells.write_text(json.dumps(report['construction']['rollback_cells'], ensure_ascii=False, indent=2), encoding='utf8')
    subprocess.run(['D:/UE5.7/excelize-cli/bin/xlsx.exe', 'write', str(ROOT / 'Data/Excel/GuLiStrikeBuildings.xlsx'),
        '--sheet', 'Buildings', '--data-file', str(building_cells)], check=True)
result = subprocess.run([sys.executable, '-X', 'utf8', str(ROOT / 'Tools/DataPipeline/export_data_from_excel.py')], cwd=ROOT, capture_output=True, text=True, encoding='utf8')
(ART / 'Reports/source_rollback_export.log').write_text(result.stdout + result.stderr, encoding='utf8')
assert result.returncode == 0
before = json.loads((ART / 'Reports/source_tables_before_switch.json').read_text(encoding='utf8'))
for table, rows in before.items():
    assert json.loads((ROOT / ('Data/Json/' + table + '.json')).read_text(encoding='utf8')) == rows, table
report.update(state='rolled_back_source_verified', native_imported=False,
    failure_reason='Loaded native Soldiers struct has no FacingPolicy/MassAvoidanceRadiusMeters/construction fields; full pipeline readback rejected the import.',
    pending_groups=[r['Name'] for r in before['DT_GuLiStrikeCommander_Soldiers']])
(ART / 'Reports/source_reference_switch.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
print(json.dumps(dict(success=True, state=report['state'], all_source_references_restored=True)))
