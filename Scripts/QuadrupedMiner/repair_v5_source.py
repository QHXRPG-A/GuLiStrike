"""Idempotent source correction: UE winding and the approved 2.5x display size.

Texture samples, normal atlas IDs, reviewed Blender source and gameplay fields
are preserved. Excel is read and written exclusively through excelize-cli.
"""
import csv
import io
import json
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/QuadrupedMiner_10k_v5'
CLI = 'D:/UE5.7/excelize-cli/bin/xlsx.exe'
meta = json.loads((OUT / 'metadata.json').read_text(encoding='utf8'))
geometry = []
for lod in range(3):
    path = OUT / f'lod{lod}.json'
    mesh = json.loads(path.read_text(encoding='utf8'))
    if mesh.get('winding') != 'ue_clockwise':
        order = np.arange(len(mesh['triangles']) * 3).reshape((-1, 3))[:, [0, 2, 1]].reshape(-1)
        mesh['triangles'] = [[t[0], t[2], t[1], t[3]] for t in mesh['triangles']]
        for attr in ['colors', 'normals']:
            mesh[attr] = [mesh[attr][i] for i in order]
        mesh['uvs'] = [[channel[i] for i in order] for channel in mesh['uvs']]
        mesh['winding'] = 'ue_clockwise'
    p = np.array(mesh['positions'])[np.array(mesh['triangles'])[:, :3]]
    normal = np.array(mesh['normals']).reshape((-1, 3, 3)).mean(axis=1)
    aligned = (np.cross(p[:, 2] - p[:, 0], p[:, 1] - p[:, 0]) * normal).sum(axis=1) > 0
    assert aligned.all()
    path.write_text(json.dumps(mesh, separators=(',', ':')), encoding='utf8')
    geometry.append(dict(lod=lod, triangles=len(p), outward_winding=int(aligned.sum())))
meta.update(base_presentation_scale=360 / meta['rest_length_cm'], presentation_size_multiplier=2.5,
            presentation_scale=900 / meta['rest_length_cm'], winding='ue_clockwise')
(OUT / 'metadata.json').write_text(json.dumps(meta, ensure_ascii=False, indent=2), encoding='utf8')

book = ROOT / 'Data/Excel/GuLiStrikeCommander.xlsx'


def read_sheet():
    text = subprocess.check_output([CLI, 'read', str(book), '--sheet', 'Soldiers', '--format', 'json', '--raw'], encoding='utf8')
    rows = json.loads(text)['rows']
    return [row + [''] * (len(rows[0]) - len(row)) for row in rows]


def column_name(n):
    text = ''
    while n:
        n, digit = divmod(n - 1, 26)
        text = chr(65 + digit) + text
    return text


before = read_sheet()
row_index = next(i for i, r in enumerate(before) if r[0] == '3')
updates = dict(PresentationScale=meta['presentation_scale'],
               Note='v5 四足矿车；表现尺寸为初次接入的2.5倍；保留3.6米业务基准、900cm/s及网格缩放；按缩放后的实际步距驱动步频')
cells = [dict(cell=f'{column_name(before[0].index(k)+1)}{row_index+1}', value=v,
              type='float' if isinstance(v, float) else 'string') for k, v in updates.items()]
cell_path = OUT / 'size-repair-cells.json'
cell_path.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf8')
subprocess.run([CLI, 'write', str(book), '--sheet', 'Soldiers', '--data-file', str(cell_path)], check=True)
after = read_sheet()
assert len(after) == len(before)
for i, (old, new) in enumerate(zip(before, after)):
    for j, (a, b) in enumerate(zip(old, new)):
        assert a == b or (i == row_index and before[0][j] in updates), (i, j)

sys.path.insert(0, str(ROOT / 'Tools/DataPipeline'))
import export_data_from_excel as pipeline


class Sheet:
    title = 'Soldiers'

    def __init__(self, rows):
        self.max_row, self.max_column = len(rows), len(rows[0])
        self.rows = rows
        for row in self.rows[3:]:
            for i, value in enumerate(row):
                if value == '': row[i] = None
                elif rows[1][i] == 'int': row[i] = int(value)
                elif rows[1][i] == 'float': row[i] = float(value)
                elif rows[1][i] == 'bool': row[i] = str(value).lower() in ('true', '1')

    def cell(self, row, column):
        return SimpleNamespace(value=self.rows[row-1][column-1])


rows, _ = pipeline.export_sheet(Sheet(after))
path = ROOT / 'Data/Json/DT_GuLiStrikeCommander_Soldiers.json'
previous = json.loads(path.read_text(encoding='utf-8-sig'))
assert [r for r in previous if r['Id'] != 3] == [r for r in rows if r['Id'] != 3]
miner = next(r for r in rows if r['Id'] == 3)
old_miner = next(r for r in previous if r['Id'] == 3)
assert all(miner[k] == v for k, v in old_miner.items() if k not in updates)
path.write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf8')
fields = list(dict.fromkeys(k for r in rows for k in r))
buffer = io.StringIO()
writer = csv.writer(buffer, lineterminator='\n')
writer.writerow(fields)


def csv_value(value):
    if isinstance(value, bool): return 'True' if value else 'False'
    if isinstance(value, dict): return '(X={X},Y={Y},Z={Z})'.format(**value)
    if isinstance(value, list): return '(' + ','.join(json.dumps(v, ensure_ascii=False) for v in value) + ')'
    return str(value)


for row in rows:
    writer.writerow([csv_value(row.get(k, '')) for k in fields])
path.with_suffix('.csv').write_text(buffer.getvalue(), encoding='utf8')
report = dict(success=True, geometry=geometry, presentation_size_multiplier=2.5,
              presentation_scale=meta['presentation_scale'], movement_speed_cm_s=miner['MovementSpeedCmPerSecond'],
              gameplay_fields_unchanged=True, texture_samples_unchanged=True)
(OUT / 'source-repair.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(report))
