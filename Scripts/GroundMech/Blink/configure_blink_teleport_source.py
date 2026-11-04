"""Current approved Blink: endpoint teleport, conceal cloud, finite ribbon burst."""
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
CLI = r'D:\UE5.7\excelize-cli\bin\xlsx.exe'
MECH = ROOT / 'data/Excel/GuLiStrikeMech.xlsx'
VFX = ROOT / 'data/Excel/GuLiStrikeVfx.xlsx'

def cli(*args):
    return subprocess.check_output([CLI, *map(str, args)], encoding='utf8')


def read(book, sheet):
    return json.loads(cli('read', book, '--sheet', sheet, '--format', 'json'))['rows']


def column(index):
    result = ''
    while index:
        index, rest = divmod(index - 1, 26)
        result = chr(65 + rest) + result
    return result


def write(book, sheet, cells):
    if not cells:
        return
    with tempfile.TemporaryDirectory(prefix='guli_blink_source_') as folder:
        path = Path(folder) / 'cells.json'
        path.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf8')
        cli('write', book, '--sheet', sheet, '--data-file', path)


def cell(c, r, value, kind='string'):
    return dict(cell=f'{column(c)}{r}', value=value, type=kind)


def append_fields(book, sheet, values, required='Optional'):
    rows = read(book, sheet)
    headers = list(rows[0])
    edits = []
    for key, (kind, _) in values.items():
        if key not in headers:
            headers.append(key)
            edits.extend(cell(len(headers), i, v) for i, v in enumerate((key, kind, required), 1))
    return rows, headers, edits


def main():
    sheet='\u6280\u80fd\u8868'
    rows=read(MECH,sheet)
    # Retire the moving Loop and body-material controls, rather than silently
    # giving old fields a different meaning. Stable skill/effect IDs are retained.
    retired={'BlinkLoopSeconds','BlinkLoopSwayDegrees','BlinkBodyVfxId',
             'BlinkFlickerHz','BlinkFragmentOffsetCm','BlinkBodyEmission',
             'BlinkRibbonLayers','BlinkRibbonSpawnRate','BlinkCloudVfxId',
             'BlinkCloudRadiusCm','BlinkCloudLifetimeSeconds'}
    for i in reversed(range(len(rows[0]))):
        if rows[0][i] in retired:cli('delete-cols',MECH,'--sheet',sheet,'--col',column(i+1),'--count',1)
    values={'BlinkDistance':('float',3200),'BlinkConcealSeconds':('float',.22),'BlinkShapeVfxId':('int',57),
            'BlinkShapeFlickerHz':('float',18),'BlinkShapeFragmentOffsetCm':('float',6),
            'BlinkRibbonCount':('int',96),'BlinkRibbonPoints':('int',24),
            'BlinkRibbonWidthCm':('float',6),
            'BlinkTrailLengthCm':('float',3200),
            'BlinkShardCount':('int',128),'BlinkShardDuration':('float',.32),'BlinkShardSpeed':('float',1000),
            'BlinkKneeAngleDegrees':('float',80),
            'BlinkRibbonLifetimeSeconds':('float',.65),'BlinkEnergyEmission':('float',8),
            'BlinkLandingSearchStepCm':('float',100),'BlinkLandingProbeCm':('float',600),
            'BlinkWindupSeconds':('float',.02),'BlinkRecoverySeconds':('float',.3)}
    rows,head,edits=append_fields(MECH,sheet,values)
    line=next(i for i,r in enumerate(rows[3:],4) if len(r)>1 and r[1]=='Blink')
    edits.extend(cell(head.index(k)+1,line,v,t) for k,(t,v) in values.items())
    edits.append(cell(head.index('Note')+1,line,'Endpoint teleport; one moving mech-shaped energy proxy; real model concealed; no Blink Loop animation; cm/s'))
    write(MECH,sheet,edits)
    old_camera=read(MECH,'Camera')
    for index in reversed(range(len(old_camera[0]))):
        if old_camera[0][index]=='BlinkSocketOffsetY':cli('delete-cols',MECH,'--sheet','Camera','--col',column(index+1),'--count',1)
    camera={'BlinkFovRiseSeconds':('float',.08),'BlinkArmLength':('float',500),
            'BlinkSideOffsetCm':('float',160),'BlinkPitchOffsetDegrees':('float',11)}
    rows,head,edits=append_fields(MECH,'Camera',camera,'Necessary')
    for i,r in enumerate(rows[3:],4):
        if len(r)>1 and r[1] in ('Free','Aim'):
            vals={'BlinkFovRiseSeconds':.08,'BlinkArmLength':500 if r[1]=='Free' else 350,
                  'BlinkSideOffsetCm':160 if r[1]=='Free' else 110,'BlinkPitchOffsetDegrees':11 if r[1]=='Free' else 4}
            edits.extend(cell(head.index(k)+1,i,v,'float') for k,v in vals.items())
    write(MECH,'Camera',edits)
    rows=read(VFX,'Effects');head=rows[0]
    line=next(i for i,r in enumerate(rows[3:],4) if r and str(r[0])=='57')
    data={'name':'GroundMechBlinkShape','ResourcePath':'/Game/GuLiStrike/FX/GroundMech/Blink/DA_GroundMech_BlinkVfx.DA_GroundMech_BlinkVfx',
          'Note':'Single moving mech-shaped energy proxy; original model materials stay unchanged'}
    write(VFX,'Effects',[cell(head.index(k)+1,line,v) for k,v in data.items()])
    print(json.dumps({'skill':'Blink','instant_teleport':True,'conceal_seconds':.22,'ribbons':96,'visual_trail_cm':3200,'fov_rise_seconds':.08}))


if __name__=='__main__':main()
