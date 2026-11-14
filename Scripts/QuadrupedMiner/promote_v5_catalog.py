"""Update only the approved miner's Excel rows and export their existing schemas.

Excel I/O uses excelize-cli. The production pipeline's schema/parser/validation
are reused through a small in-memory worksheet view; no unrelated files export.
"""
import copy
import csv
import io
import json
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
CLI = Path('D:/UE5.7/excelize-cli/bin/xlsx.exe')
OUT = ROOT / 'ArtSource/QuadrupedMiner_10k_v5'
BASE = '/Game/GuLiStrike/Commander/Units/ElectromagneticMiner/QuadrupedMiner_10k_v5'
META = json.loads((OUT / 'metadata.json').read_text(encoding='utf8'))
BOOK = ROOT / 'Data/Excel/GuLiStrikeModels.xlsx'


def cli(*args):
    return subprocess.check_output([str(CLI), *map(str, args)], encoding='utf8')


def letter(n):
    result = ''
    while n:
        n, d = divmod(n - 1, 26); result = chr(65 + d) + result
    return result


def grid(book, sheet):
    rows = json.loads(cli('read', book, '--sheet', sheet, '--format', 'json', '--raw'))['rows']
    return [r + [''] * (len(rows[0]) - len(r)) for r in rows]


def records(rows):
    return [dict(zip(rows[0], r)) for r in rows[3:] if r[0]]


def write_cells(book, sheet, cells):
    path = OUT / f'catalog-{sheet}-cells.json'
    path.write_text(json.dumps(cells, ensure_ascii=False, indent=2), encoding='utf8')
    if cells: cli('write', book, '--sheet', sheet, '--data-file', path)


def update(book, sheet, row, values, headers):
    cells = []
    for key, value in values.items():
        typ = 'bool' if isinstance(value, bool) else 'int' if isinstance(value, int) else 'float' if isinstance(value, float) else 'string'
        cells.append(dict(cell=f'{letter(headers.index(key)+1)}{row}', value=value, type=typ))
    write_cells(book, sheet, cells)


models = grid(BOOK, 'Models')
row = next(i for i, r in enumerate(models, 1) if r[0] == '1003')
update(BOOK, 'Models', row, dict(Note='用户放行 v5 正式替换；9854 面；增强行进与采矿；四足外侧阵营灯',
       ResourcePath=BASE + '/BP_QuadrupedMiner.BP_QuadrupedMiner_C', VATDefinition='', bTeamColorEnabled=True,
       BluePrimaryHex='#6AA4BE', BlueSecondaryHex='#274E61', EnemyPrimaryHex='#A34053', EnemySecondaryHex='#662249',
       CandidateResourcePath='', CandidateVATDefinition='', Description='四足电磁矿车；专用位置/法线 VAT 数据由表现蓝图持有；固定中立外壳，四足灯按本地敌我关系改色。'), models[0])
leaf = next((i for i, r in enumerate(models, 1) if r[0] == '8017'), len(models) + 1)
if leaf <= len(models): assert models[leaf-1][1] == 'QuadrupedMinerBody'
update(BOOK, 'Models', leaf, dict(id=8017, name='QuadrupedMinerBody', Note='v5 三档 VAT 网格，作为 1003 的 BodyVAT 装配部件',
       DisplayName='四足采矿车网格', ResourceType='StaticMesh', ResourcePath=BASE + '/Meshes/SM_QuadrupedMiner_VAT.SM_QuadrupedMiner_VAT',
       VATDefinition='', bTeamColorEnabled=False, BluePrimaryHex='#6AA4BE', BlueSecondaryHex='#274E61', EnemyPrimaryHex='#A34053',
       EnemySecondaryHex='#662249', CandidateResourcePath='', CandidateVATDefinition='', Description='完整四足网格及反向描边；9854/4614/2676 三角面；不独立生成玩法单位。'), models[0])

parts = grid(BOOK, 'Parts'); column = parts[0].index('ModelId')
old = [i for i, r in enumerate(parts, 1) if r[column] == '1003']
assert old
keep = old[0]
update(BOOK, 'Parts', keep, dict(Note='v5 完整网格，替换旧双采集器装配', ModelId=1003, PartKey='Root', ComponentPath='BodyVAT', ChildModelId=8017), parts[0])
for row in reversed(old[1:]): cli('delete-rows', BOOK, '--sheet', 'Parts', '--at', row, '--count', 1)

parameters = grid(BOOK, 'MaterialParameters')
prototype = [r for r in records(parameters) if r['ModelId'] == '1001' and r['Driver'] == 'CPD' and r['Scope'] == 'Existing']
prototype = {r['ParameterKey']: r for r in prototype}
next_id = max(int(r[0]) for r in parameters[3:]) + 1
existing = {r['ParameterKey']: i for i, r in enumerate(records(parameters), 4) if r['ModelId'] == '1003'}
next_row = len(parameters) + 1
for key in ['TeamPrimary', 'TeamSecondary', 'TeamEnabled', 'TeamLightStrength']:
    source = copy.deepcopy(prototype[key])
    if key in existing:
        target = existing[key]; source['id'] = int(parameters[target-1][0])
    else:
        target = next_row; next_row += 1; source['id'] = next_id; next_id += 1
    source.update(name='QuadrupedMiner_' + key, Note='四足灯芯队色；固定外壳不参与改色', ModelId=1003, PartKey='Root', MaterialSlotName='*')
    if key == 'TeamLightStrength': source['DefaultScalar'] = 1.8
    update(BOOK, 'MaterialParameters', target, source, parameters[0])

regions = grid(BOOK, 'ColorRegions'); next_id = max(int(r[0]) for r in regions[3:]) + 1
existing = {r['RegionKey']: i for i, r in enumerate(records(regions), 4) if r['ModelId'] == '1003'}
for leg in ['FL', 'FR', 'RL', 'RR']:
    key = 'TeamLamp_' + leg
    target = existing.get(key, len(regions) + 1)
    if target > len(regions): regions.append([''] * len(regions[0])); ident = next_id; next_id += 1
    else: ident = int(regions[target-1][0])
    update(BOOK, 'ColorRegions', target, dict(id=ident, name='QuadrupedMiner_' + key, Note='三档 LOD 保留，灯罩为固定色', ModelId=1003,
           PartKey='Root', RegionKey=key, DisplayName=leg + ' 外侧阵营灯', PaintRole=7, MaskSource='VertexColorAlpha', MaskId=7, Scope='Existing'), regions[0])

commander = ROOT / 'Data/Excel/GuLiStrikeCommander.xlsx'
soldiers = grid(commander, 'Soldiers')
row = next(i for i, r in enumerate(soldiers, 1) if r[0] == '3')
update(commander, 'Soldiers', row, dict(Note='v5 四足矿车；表现尺寸为初次接入的2.5倍；保留3.6米业务基准、900cm/s及网格缩放；按缩放后的实际步距驱动步频',
       PresentationScale=META['presentation_scale']), soldiers[0])

# Reuse the production parser with the exact typed values read by excelize.
sys.path.insert(0, str(ROOT / 'Tools/DataPipeline'))
import export_data_from_excel as pipeline


class Sheet:
    def __init__(self, name, rows):
        self.title = name; self.max_row = len(rows); self.max_column = len(rows[0]); self.rows = rows
        for r in self.rows[3:]:
            for i, value in enumerate(r):
                if value == '': r[i] = None
                elif self.rows[1][i] == 'int': r[i] = int(value)
                elif self.rows[1][i] == 'float': r[i] = float(value)
                elif self.rows[1][i] == 'bool': r[i] = str(value).lower() in ('true', '1')
    def cell(self, row, column):
        return SimpleNamespace(value=self.rows[row-1][column-1])


changed = {}
for book, sheets in [(BOOK, ['Models', 'Parts', 'MaterialParameters', 'ColorRegions']), (commander, ['Soldiers'])]:
    for sheet in sheets:
        rows, _ = pipeline.export_sheet(Sheet(sheet, grid(book, sheet)))
        changed[f'DT_{book.stem}_{sheet}'] = rows
tables = {p.stem: {'rows': json.loads(p.read_text(encoding='utf-8-sig'))} for p in (ROOT / 'Data/Json').glob('DT_*.json')}
for name, rows in changed.items(): tables[name] = {'rows': rows}
pipeline.validate_model_references(tables)
for name, rows in changed.items():
    path = ROOT / f'Data/Json/{name}.json'
    path.write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf8')
    buffer = io.StringIO(); writer = csv.writer(buffer, lineterminator='\n')
    fields = list(dict.fromkeys(k for r in rows for k in r)); writer.writerow(fields)
    def csv_value(value):
        if isinstance(value, bool): return 'True' if value else 'False'
        if isinstance(value, dict): return '(X={X},Y={Y},Z={Z})'.format(**value)
        if isinstance(value, list): return '(' + ','.join(json.dumps(v, ensure_ascii=False) for v in value) + ')'
        return str(value)
    for r in rows: writer.writerow([csv_value(r.get(k, '')) for k in fields])
    path.with_suffix('.csv').write_text(buffer.getvalue(), encoding='utf8')
models = changed['DT_GuLiStrikeModels_Models']
header = ['// Generated from GuLiStrikeModels.xlsx / Models. Do not edit.', '#pragma once', '#include "CoreTypes.h"', 'namespace GuLiModelIds', '{']
header += [f'\tinline constexpr int32 {r["Name"]} = {r["Id"]};' for r in models]
(ROOT / 'Source/GuLiStrike/Gameplay/Data/Generated/GuLiModelIds.h').write_text('\n'.join(header + ['}', '']), encoding='utf8')
ledger = ROOT / 'Data/Models/published-model-ids.json'
ids = json.loads(ledger.read_text(encoding='utf8')); ids['QuadrupedMinerBody'] = 8017
ledger.write_text(json.dumps(ids, ensure_ascii=False, indent=2), encoding='utf8')
report = dict(success=True, tables=list(changed), model_id=1003, child_model_id=8017,
              presentation_scale=META['presentation_scale'], movement_speed_cm_s=900, ue_imported=False)
(OUT / 'catalog-source-update.json').write_text(json.dumps(report, indent=2), encoding='utf8')
print(json.dumps(report))
