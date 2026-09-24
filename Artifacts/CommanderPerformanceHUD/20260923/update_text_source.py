import hashlib
import json
import runpy
import subprocess
from pathlib import Path
from types import SimpleNamespace

project = Path('D:/UE5.7/test1')
artifact = project / 'Artifacts/CommanderPerformanceHUD/20260923'
book = project / 'Data/Excel/GuLiStrikeGameTexts.xlsx'
cli = 'D:/UE5.7/excelize-cli/bin/xlsx.exe'

def read_sheet():
    return json.loads(subprocess.check_output(
        [cli, 'read', str(book), '--sheet', 'Texts', '--format', 'json'], encoding='utf-8'))['rows']

before = read_sheet()
entries = {
    'UI.Performance.FPSPending': 'FPS --',
    'UI.Performance.FPS': 'FPS {0}',
    'UI.Performance.LatencyPending': '延迟 --',
    'UI.Performance.LatencyLocal': '延迟 本地',
    'UI.Performance.LatencyRTT': '延迟 {0} ms (RTT)',
}
assert not set(entries) & {r[0] for r in before[3:]}
cells = []
for row_index, (key, content) in enumerate(entries.items(), len(before) + 1):
    values = [key, 'Commander/Presentation/GuLiCommanderHUD.cpp；本地性能指标', content]
    cells.extend({'cell': f'{column}{row_index}', 'value': value, 'type': 'string'}
                 for column, value in zip('ABC', values))
cell_file = artifact / 'text-cells.json'
cell_file.write_text(json.dumps(cells, ensure_ascii=False, indent=2), encoding='utf-8')
subprocess.run([cli, 'write', str(book), '--sheet', 'Texts', '--data-file', str(cell_file)], check=True)
after = read_sheet()
assert after[:len(before)] == before
assert len(after) == len(before) + len(entries)

# Reuse the standard exporter's validation and row mapping with excelize readback.
pipeline = runpy.run_path(str(project / 'Tools/DataPipeline/export_data_from_excel.py'))
sheet = SimpleNamespace(title='Texts', max_row=len(after), max_column=3,
    cell=lambda row, column: SimpleNamespace(value=after[row - 1][column - 1] or None))
rows, _ = pipeline['export_game_texts'](sheet)
output = project / 'Data/Json/DT_GuLiStrikeGameTexts_Texts.json'
old_rows = json.loads(output.read_text(encoding='utf-8'))
assert rows[:len(old_rows)] == old_rows
pipeline['validate_game_text_references']({'DT_GuLiStrikeGameTexts_Texts': {'rows': rows}})
output.write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf-8')
report = {'previous_rows': len(old_rows), 'rows': len(rows), 'existing_rows_unchanged': True,
          'new_keys': entries, 'sha256': {str(p.relative_to(project)): hashlib.sha256(p.read_bytes()).hexdigest()
                                         for p in (book, output)}}
(artifact / 'text-source-readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=False, indent=2))
