"""Create the secondary-unit active-skill source workbook with excelize; never overwrite it."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BOOK = ROOT / 'data/Excel/GuLiStrikeSecondaryUnitSkills.xlsx'
CLI = 'D:/UE5.7/excelize-cli/bin/xlsx.exe'
OUT = ROOT / 'outputs/wm01_q'

def run(*args):
    subprocess.run([CLI, *map(str, args)], check=True)

def column(index):
    result = ''
    while index:
        index, digit = divmod(index-1, 26)
        result = chr(65+digit)+result
    return result

def sheet(name, schema, records):
    cells = []
    for col, (key, kind, required) in enumerate(schema, 1):
        for row, value in enumerate([key, kind, 'Necessary' if required else 'Optional'], 1):
            cells.append(dict(cell=f'{column(col)}{row}', value=value, type='string'))
        for row, record in enumerate(records, 4):
            value = record.get(key, '')
            if value == '':
                continue
            cells.append(dict(cell=f'{column(col)}{row}', value=value,
                              type={'str':'string','softclass':'string','softobject':'string'}.get(kind, kind)))
    payload = OUT / (name+'-source-cells.json')
    payload.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf8')
    run('write', BOOK, '--sheet', name, '--data-file', payload)
    run('style', BOOK, '--sheet', name, '--range', f'A1:{column(len(schema))}3', '--bold', '--bg', 'DCEAF7', '--wrap')
    run('col-width', BOOK, '--sheet', name, '--col', 'A', '--to', column(len(schema)), '--width', '23')
    run('col-width', BOOK, '--sheet', name, '--col', 'C', '--width', '55')

def main():
    if BOOK.exists():
        print('Source workbook already exists; preserved. Edit it directly with excelize.')
        return
    OUT.mkdir(parents=True, exist_ok=True)
    run('new', BOOK, '--sheet', 'Skills')
    standard = [('id','int',True),('name','str',True),('Note','str',False)]
    schema = standard + [(k,t,required) for k,t,required in [
        ('TargetMode','str',True),('CooldownSeconds','float',True),('RangeCentimeters','float',False),
        ('RangeSourceSlot','str',False),('RangeMultiplier','float',True),('MaximumLevel','int',True),
        ('ExecutorClass','softclass',True),('ConfigurationClass','softclass',False),('Configuration','softobject',False),
        ('Projectile','softobject',False),('FieldConfigId','str',False),('SourceWeaponSlot','str',False),
        ('UseAuthoredTrajectory','bool',False),('GroundWarningStyle','softobject',False),
        ('TargetAreaDiameterCentimeters','float',False)]]
    sheet('Skills', schema, [dict(id=1, name='WM01_HomingMissile', Note='Scale020：战争机器Q；独立6秒；普攻射程x1.6；直径8米随机区域，半径1.6米爆炸与预警共用服务器数据',
        TargetMode='GroundPoint', CooldownSeconds=6., RangeCentimeters=4800., RangeSourceSlot='BasicAttack', RangeMultiplier=1.6,
        MaximumLevel=1, ExecutorClass='/Script/GuLiStrike.GuLiWarMachineMissileSkillExecutor',
        ConfigurationClass='/Script/GuLiStrike.GuLiPointSkillConfiguration', Configuration='/Game/GuLiStrike/Commander/Skills/DA_WM01_MissileQ',
        Projectile='/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_Missile', FieldConfigId='WM01_MissileExplosion',
        SourceWeaponSlot='MissileLauncher', UseAuthoredTrajectory=True, TargetAreaDiameterCentimeters=800.,
        GroundWarningStyle='/Game/GuLiStrike/FX/GroundWarning/DA_GroundWarning_Red')])
    run('add-sheet', BOOK, '--name', 'UnitSkills')
    units = json.loads((ROOT/'data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf8'))
    sheet('UnitSkills', standard+[('UnitTypeId','int',True),('SkillId','str',False)],
          [dict(id=u['Id'], name=u['Name'], Note=u.get('DisplayName',u['Name'])+'；空技能表示当前无主动Q',
                UnitTypeId=u['Id'], SkillId='WM01_HomingMissile' if u['Id']==2 else '') for u in units])
    run('info', BOOK)

if __name__ == '__main__':
    main()
