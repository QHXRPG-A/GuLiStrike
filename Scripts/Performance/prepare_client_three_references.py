"""Apply the user's three-optimization adoption through Excel and the normal exporter.

Absolute values make repeat runs idempotent. Four existing effect rows change
and the independent Wingman batch receives ID 53; existing scales are preserved.
"""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'outputs/performance/20261009-client-three-optimizations'
CLI = 'D:/UE5.7/excelize-cli/bin/xlsx.exe'
BOOK = ROOT / 'Data/Excel/GuLiStrikeVFX.xlsx'
DEST = '/Game/GuLiStrike/FX/CommanderWeapons/'
FIELDS = ['ResourcePath', 'ReducedResourcePath', 'MinimalResourcePath',
          'BatchResourcePath', 'ReducedBatchResourcePath', 'MinimalBatchResourcePath']
COLUMNS = ['D', 'H', 'I', 'J', 'K', 'L']


def object_path(package):
    return package + '.' + package.rsplit('/', 1)[1]


PATHS = {
    5: {
        'ReducedResourcePath': DEST + 'NS_MachineGunImpact_AllOptimizations_Reduced',
        'MinimalResourcePath': DEST + 'NS_MachineGunImpact_AllOptimizations_Minimal',
        'BatchResourcePath': DEST + 'NS_MachineGunImpact_Batch',
        'ReducedBatchResourcePath': DEST + 'NS_MachineGunImpact_Batch_Reduced',
        'MinimalBatchResourcePath': DEST + 'NS_MachineGunImpact_Batch_Minimal',
    },
    52: {
        'ReducedResourcePath': DEST + 'NS_MachineGunMuzzle_AllOptimizations_Reduced',
        'MinimalResourcePath': DEST + 'NS_MachineGunMuzzle_AllOptimizations_Minimal',
    },
    36: {'ResourcePath': '/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Optimized_GPU_All_ThreeTier'},
    45: {'ResourcePath': '/Game/GuLiStrike/Buildings/Construction/NS_ConstructionLaser_Optimized_GPU_All_ThreeTier'},
    53: {'ResourcePath': DEST + 'NS_WingmanGroundFlight_SphereTrail'},
}


def read_rows():
    return json.loads(subprocess.check_output(
        [CLI, 'read', str(BOOK), '--sheet', 'Effects', '--format', 'json'],
        text=True, encoding='utf-8'))['rows']


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    before = read_rows()
    assert before[0][:7] == ['id', 'name', 'Note', 'ResourcePath', 'ScaleX', 'ScaleY', 'ScaleZ']
    assert before[0][7:12] == FIELDS[1:]
    assert len(before) in (55, 56)
    cells, rows = [], []
    if len(before) == 55:
        new_values = [53, 'WingmanGroundFlightBatch', '僚机对地导弹独立不透明脉冲光点与实体尾迹；Profile专属，Scale=1',
                      object_path(PATHS[53]['ResourcePath']), 1, 1, 1]
        for col, value in enumerate(new_values):
            cells.append({'cell': f'{chr(65+col)}56', 'value': value,
                          'type': 'string' if col in (1, 2, 3) else 'int'})
        rows.append({'id': 53, 'new_row': True, 'changes': {'ResourcePath': {
            'before': '', 'after': object_path(PATHS[53]['ResourcePath'])}}, 'base_scale': [1, 1, 1]})
    else:
        assert before[55][0] == '53' and before[55][1] == 'WingmanGroundFlightBatch', 'ID 53 already occupied'
    for index, row in enumerate(before[3:], 4):
        effect_id = int(row[0])
        if effect_id not in PATHS:
            continue
        changes = {}
        for field, package in PATHS[effect_id].items():
            column = COLUMNS[FIELDS.index(field)]
            value = object_path(package)
            cells.append({'cell': f'{column}{index}', 'value': value, 'type': 'string'})
            old = row[ord(column) - 65] if len(row) > ord(column) - 65 else ''
            changes[field] = {'before': old, 'after': value}
        rows.append({'id': effect_id, 'changes': changes,
                     'base_scale': [float(x) for x in row[4:7]]})
    assert sorted(x['id'] for x in rows) == [5, 36, 45, 52, 53]
    proposal_path = OUT / 'client-three-formal-proposal.json'
    if proposal_path.exists():
        proposal = json.loads(proposal_path.read_text(encoding='utf-8'))
        assert proposal['authorization'] == '应用所有新的变更'
        if 53 not in {r['id'] for r in proposal['rows']}:
            proposal['rows'].append(next(r for r in rows if r['id'] == 53))
            proposal['applied'] = False
            proposal_path.write_text(json.dumps(proposal, ensure_ascii=False, indent=2), encoding='utf-8')
        assert {r['id']: {k: v['after'] for k, v in r['changes'].items()} for r in proposal['rows']} == {
            r['id']: {k: v['after'] for k, v in r['changes'].items()} for r in rows}
    else:
        proposal = {'authorization': '应用所有新的变更', 'authorized_date': '2026-10-10',
                    'visual_acceptance': 'pending_user_pie', 'performance': 'deferred_by_user',
                    'applied': False, 'rows': rows}
        proposal_path.write_text(json.dumps(proposal, ensure_ascii=False, indent=2), encoding='utf-8')
    payload = OUT / 'client-three-formal-cells.json'
    payload.write_text(json.dumps(cells, ensure_ascii=False, indent=2), encoding='utf-8')
    subprocess.run([CLI, 'write', str(BOOK), '--sheet', 'Effects', '--data-file', str(payload)], check=True)
    after = read_rows()
    assert len(after) == 56
    for index, (old, new) in enumerate(zip(before, after)):
        changed = set()
        if index >= 3:
            changed = {ord(COLUMNS[FIELDS.index(field)]) - 65 for field in PATHS.get(int(old[0]), {})}
        for col in range(max(len(old), len(new))):
            a = old[col] if col < len(old) else ''
            b = new[col] if col < len(new) else ''
            if col not in changed:
                assert a == b, (index + 1, col + 1, a, b)
    subprocess.run(['python', '-X', 'utf8', str(ROOT / 'Tools/DataPipeline/export_data_from_excel.py')],
                   cwd=ROOT, check=True)
    print(json.dumps({'success': True, 'ids': [r['id'] for r in rows],
                      'cells': len(cells), 'scales_and_other_rows_preserved': True}))


if __name__ == '__main__':
    main()
