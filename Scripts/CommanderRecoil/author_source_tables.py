"""Author burst tuning through the project's excelize CLI, preserving all other rows."""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/CommanderRecoil_20261114'
XLSX = r'D:\UE5.7\excelize-cli\bin\xlsx.exe'


def run(*args):
    return json.loads(subprocess.check_output([XLSX, *map(str, args)], encoding='utf-8'))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    book = ROOT / 'Data/Excel/GuLiStrikeSecondaryWeapons.xlsx'
    rows = run('read', book, '--sheet', 'UnitSkills', '--format', 'json')['rows']
    headers = rows[0]
    assert headers[:16][-1] == 'ProjectileCount'
    assert len(headers) in (16, 18) and (len(headers) == 16 or headers[16:18] == ['BurstRoundCount', 'BurstRestSeconds'])
    cells = []
    for col, name, kind in [('Q', 'BurstRoundCount', 'int'), ('R', 'BurstRestSeconds', 'float')]:
        for index, value in enumerate((name, kind, 'Optional'), 1):
            cells.append({'cell': f'{col}{index}', 'value': value, 'type': 'string'})
    for index, row in enumerate(rows[3:], 4):
        count = {1: 8, 2: 4}.get(int(row[3]), 0) if row[4] == 'BasicAttack' else 0
        cells.extend([{'cell': f'Q{index}', 'value': count, 'type': 'int'},
                      {'cell': f'R{index}', 'value': 2 if count else 0, 'type': 'float'}])
    cells.extend([
        {'cell': 'C4', 'value': '先驱者双枪齐射；1轮/秒，每轮2发、每发10伤害；8轮后额外休整2秒', 'type': 'string'},
        {'cell': 'C5', 'value': '重防号左右交替；2发/秒；4发后额外休整2秒', 'type': 'string'},
    ])
    payload = OUT / 'unit-skill-cells.json'
    payload.write_text(json.dumps(cells, ensure_ascii=False, indent=2), encoding='utf-8')
    subprocess.run([XLSX, 'write', str(book), '--sheet', 'UnitSkills', '--data-file', str(payload)], check=True)
    readback = run('read', book, '--sheet', 'UnitSkills', '--format', 'json')
    assert readback['rows'][3][16:18] == ['8', '2']
    assert readback['rows'][4][16:18] == ['4', '2']
    (OUT / 'unit-skills-source-readback.json').write_text(json.dumps(readback, ensure_ascii=False, indent=2), encoding='utf-8')
    vfx = ROOT / 'Data/Excel/GuLiStrikeVfx.xlsx'
    effect_rows = run('read', vfx, '--sheet', 'Effects', '--format', 'json')['rows']
    fields, types = effect_rows[:2]
    template = next(row for row in effect_rows[3:] if row[1] == 'GroundMachineGunMuzzle')
    target = next((i for i, row in enumerate(effect_rows, 1) if len(row)>1 and row[1]=='CommanderMuzzleGlow'),len(effect_rows)+1)
    values = dict(zip(fields, template))
    values.update(id=effect_rows[target-1][0] if target<=len(effect_rows) else max(int(row[0]) for row in effect_rows[3:])+1,
                  name='CommanderMuzzleGlow', Note='先驱者与重防号普攻亮芯和光晕；逐枪口批量呈现；无动态灯')
    base='/Game/GuLiStrike/FX/CommanderWeapons/RecoilGlow_v1/'
    for key,suffix in [('ResourcePath',''),('ReducedResourcePath','_Reduced'),('MinimalResourcePath','_Minimal'),
                       ('BatchResourcePath','_Batch'),('ReducedBatchResourcePath','_Batch_Reduced'),('MinimalBatchResourcePath','_Batch_Minimal')]:
        name='NS_CommanderMuzzleGlow'+suffix;values[key]=base+name+'.'+name
    def column(index):
        result=''
        while index:index,digit=divmod(index-1,26);result=chr(65+digit)+result
        return result
    cells=[]
    for i,(key,kind) in enumerate(zip(fields,types),1):
        value=values.get(key,'')
        if kind in ('int','float'):value=(int(float(value)) if kind=='int' else float(value)) if value!='' else 0
        elif kind=='bool':value=str(value).upper() in ('1','TRUE')
        cells.append({'cell':f'{column(i)}{target}','value':value,'type':kind if kind in ('int','float','bool') else 'string'})
    payload=OUT/'muzzle-effect-cells.json';payload.write_text(json.dumps(cells,ensure_ascii=False,indent=2),encoding='utf-8')
    subprocess.run([XLSX,'write',str(vfx),'--sheet','Effects','--data-file',str(payload)],check=True)
    print('Burst source saved and read back: Pioneer 8 rounds, WM01 4 rounds, +2 seconds.')


if __name__ == '__main__':
    main()
