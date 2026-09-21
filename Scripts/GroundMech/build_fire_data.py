"""Author the two-sheet mech source workbook with the project's excelize CLI."""
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
import sys
sys.path.insert(0,str(ROOT/'Scripts/Vfx'))
from vfx_registry import vfx_id
CLI = r'D:\UE5.7\excelize-cli\bin\xlsx.exe'
BOOK = ROOT / 'Data/Excel/GuLiStrikeMech.xlsx'
FX = '/Game/GuLiStrike/FX/GroundMech'
ANIM = '/Game/GuLiStrike/GroundMech/Animations'


def run(*args):
    subprocess.run([CLI, *map(str, args)], check=True, capture_output=True)


def column(index):
    result = ''
    while index:
        index, remainder = divmod(index - 1, 26)
        result = chr(65 + remainder) + result
    return result


def write_sheet(sheet, schema, rows):
    cells = []
    for c, (name, kind, required) in enumerate(schema, 1):
        for r, value in enumerate([name, kind, 'Necessary' if required else 'Optional'], 1):
            cells.append({'cell': f'{column(c)}{r}', 'value': value, 'type': 'string'})
        for r, row in enumerate(rows, 4):
            cells.append({'cell': f'{column(c)}{r}', 'value': row[c-1],
                          'type': kind if kind in ('int', 'float', 'bool') else 'string'})
    with tempfile.TemporaryDirectory(prefix='guli-mech-data-') as directory:
        data = Path(directory) / 'cells.json'
        data.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf-8')
        run('write', BOOK, '--sheet', sheet, '--data-file', data)
    end = column(len(schema))
    run('style', BOOK, '--sheet', sheet, '--range', f'A1:{end}1', '--bold', '--bg', 'D9EAF7', '--wrap')
    run('style', BOOK, '--sheet', sheet, '--range', f'A2:{end}3', '--font-color', '666666')
    run('col-width', BOOK, '--sheet', sheet, '--col', 'A', '--to', end, '--width', '23')
    run('col-width', BOOK, '--sheet', sheet, '--col', 'C', '--width', '48')
    run('row-height', BOOK, '--sheet', sheet, '--row', '1', '--height', '42')


if __name__ == '__main__':
    if BOOK.exists():
        raise SystemExit('Workbook already exists; edit the authored source instead of resetting tuning.')
    run('new', BOOK, '--sheet', '升级表')
    run('add-sheet', BOOK, '--name', '技能表')
    write_sheet('升级表', [('id','str',True),('name','str',True),('Note','str',False),
                ('SkillId','int',True),('Level','int',True),('FireRate','float',True),('Damage','float',True)],
                [[f'1.{level}',f'Machinegun_Lv{level}','基础机枪；FireRate单位为发/秒；本级绝对数值',1,level,rate,damage]
                 for level,rate,damage in [(1,2,10),(2,4,15),(3,6,20)]])
    write_sheet('技能表', [('id','int',True),('name','str',True),('Note','str',False),
                ('ProjectileSpeed','float',True),('ProjectileLifetime','float',True),('SweepRadius','float',True),
                ('MuzzleSocket','str',True),('RecoilBone','str',True),('RecoilTargetLocalZCentimeters','float',True),
                ('RecoilDuration','float',True),('RecoilCurve','softobject',True),
                ('WeaponAnimation','softclass',True),('BulletVfxId','int',True),('MuzzleVfxId','int',True),
                ('AimAssistEnabled','bool',False),('AimAssistRadiusCentimeters','float',False)],
                [[1,'BasicMachinegun','基础机枪；距离cm、时间s；射速/伤害只在升级表维护',12000,5,15,
                  'Muzzle','Barrel_big',152,0.15,ANIM+'/CF_Machinegun_Recoil.CF_Machinegun_Recoil',
                  ANIM+'/ABP_GroundMech_Machinegun.ABP_GroundMech_Machinegun_C',
                  vfx_id('PlayerBullet'),vfx_id('PlayerMuzzle'),True,100]])
    print(BOOK)
