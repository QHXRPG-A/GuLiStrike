"""Approved Blink/Impact A_v1. Extend source workbooks through excelize only."""
import json
import subprocess
import tempfile
from pathlib import Path
from author_mobility_tables import ROOT, BOOK, CLI, cli, column, write

BASE = '/Game/GuLiStrike/FX/GroundMech/BlinkImpact_B1'

def apply():
    values = dict(BlinkDistance=1200., BlinkCooldownSeconds=2., BlinkWindupSeconds=.1,
                  BlinkLoopSeconds=.8, BlinkRecoverySeconds=.2, BlinkLoopSwayDegrees=3., BlinkArmBackDegrees=120., BlinkBodyLeanDegrees=18.,
                  BlinkAfterimageCount=3, BlinkAfterimageSeconds=.38, BlinkAfterimageOpacity=.55)
    rows = json.loads(cli('read', BOOK, '--sheet', 'Mobility', '--format', 'json'))['rows']
    headers = rows[0]
    cells = []
    for key, value in values.items():
        kind = 'int' if isinstance(value, int) else 'float'
        if key not in headers:
            headers.append(key)
            for line, text in enumerate((key, kind, 'Necessary'), 1):
                cells.append(dict(cell=f'{column(len(headers))}{line}', value=text, type='string'))
        cells.append(dict(cell=f'{column(headers.index(key)+1)}4', value=value, type=kind))
    write('Mobility', cells)
    book = ROOT/'Data/Excel/GuLiStrikeVfx.xlsx'
    rows = json.loads(cli('read', book, '--sheet', 'Effects', '--format', 'json'))['rows']
    headers = rows[0]
    baseline = next(r for r in rows[3:] if r[headers.index('name')]=='MachineGunImpact')
    max_id = max(int(r[headers.index('id')]) for r in rows[3:])
    cells = []
    for name, asset, note in (
        ('GroundMechImpact','NS_GroundMech_Impact_Adjusted','Ground 命中：保留原命中形态/贴图/运动；线性尺寸 x2；子弹橙黄；0.20秒淡出'),
        ('GroundMechAfterimage','M_GroundMech_Afterimage_B1','Ground 闪现：冻结双臂后展姿势；阵营色；世界位置固定；材质淡出')):
        existing = next((i for i,r in enumerate(rows[3:],4) if len(r)>headers.index('name') and r[headers.index('name')]==name),None)
        line = existing or len(rows)+1
        if not existing:
            rows.append([]); max_id += 1
        data = {'id': int(rows[line-1][headers.index('id')]) if existing else max_id,
                'name':name,'Note':note,'ResourcePath':f'{BASE}/{asset}.{asset}'}
        for axis in 'XYZ': data['Scale'+axis] = float(baseline[headers.index('Scale'+axis)])*2 if name=='GroundMechImpact' else 1.
        for i,key in enumerate(headers):
            value=data.get(key,0. if rows[1][i] in ('float','int') else '')
            cells.append(dict(cell=f'{column(i+1)}{line}',value=value,type='string' if isinstance(value,str) else 'int' if key=='id' else 'float'))
    with tempfile.TemporaryDirectory(prefix='guli_blink_cells_') as temp:
        path=Path(temp)/'cells.json'; path.write_text(json.dumps(cells,ensure_ascii=False),encoding='utf8')
        subprocess.run([CLI,'write',str(book),'--sheet','Effects','--data-file',str(path)],check=True)
    print(json.dumps(values))

if __name__=='__main__': apply()
