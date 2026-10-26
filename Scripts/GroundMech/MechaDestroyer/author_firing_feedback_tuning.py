"""Approved A_v3 firing tuning; absolute values make repeat runs idempotent."""
import json
import subprocess
import tempfile
from pathlib import Path
from author_mobility_tables import ROOT, BOOK, CLI, cli, column, write


def apply():
    arm = dict(ArmRecoilDegrees=18., ArmRecoilAttackSeconds=.025,
               ArmRecoilHoldSeconds=.015, ArmRecoilRecoverySeconds=.20)
    for sheet in ('升级表', '技能表', 'Gait'):
        rows = json.loads(cli('read', BOOK, '--sheet', sheet, '--format', 'json'))['rows']
        headers = rows[0]
        cells = []
        if sheet == 'Gait':
            for key in arm:
                if key not in headers:
                    headers.append(key)
                    for line, value in enumerate((key, 'float', 'Optional'), 1):
                        cells.append(dict(cell=f'{column(len(headers))}{line}', value=value, type='string'))
        for line, row in enumerate(rows[3:], 4):
            name = row[headers.index('name')]
            values = {}
            if sheet == '升级表' and name in ('Machinegun_Lv1', 'Machinegun_Lv2', 'Machinegun_Lv3'):
                values['FireRate'] = {'Machinegun_Lv1': 4., 'Machinegun_Lv2': 8., 'Machinegun_Lv3': 12.}[name]
            elif sheet == '技能表' and name == 'BasicMachinegun':
                values['ProjectileSpeed'] = 24000.
            elif sheet == 'Gait':
                values.update({key: value if name == 'Shoot' else 0. for key, value in arm.items()})
                if name == 'Shoot':
                    values['Note'] = '整臂制退：肩关节18度；0.025s冲击/0.015s制退/0.20s复位；枪管原独立后坐和旋转保留'
            for key, value in values.items():
                cells.append(dict(cell=f'{column(headers.index(key)+1)}{line}', value=value,
                                  type='string' if isinstance(value, str) else 'float'))
        if cells:
            write(sheet, cells)

    book = ROOT/'Data/Excel/GuLiStrikeVfx.xlsx'
    rows = json.loads(cli('read', book, '--sheet', 'Effects', '--format', 'json'))['rows']
    headers = rows[0]
    cells = []
    for line, row in enumerate(rows[3:], 4):
        name = row[headers.index('name')]
        if name not in ('PlayerBullet', 'PlayerMuzzle'):
            continue
        asset = 'NS_GroundMech_Bullet_SingleCurve_A3' if name == 'PlayerBullet' else 'NS_GroundMech_Muzzle_A3'
        values = dict(ResourcePath=f'/Game/GuLiStrike/FX/GroundMech/FiringFeedback_A3/{asset}.{asset}',
                      ScaleX=2., ScaleY=2., ScaleZ=2.)
        values['Note'] = ('玩家Ground单曲线子弹；X=90cm长度倍率，Y/Z=12cm弹头尺寸倍率；导入同步粒子尺寸，勿再乘组件缩放'
                          if name == 'PlayerBullet' else
                          '玩家Ground枪焰；统一线性尺寸倍率由User.Size接收，组件保持单位缩放')
        for key, value in values.items():
            cells.append(dict(cell=f'{column(headers.index(key)+1)}{line}', value=value,
                              type='string' if isinstance(value, str) else 'float'))
    with tempfile.TemporaryDirectory(prefix='guli_fire_cells_') as temp:
        path = Path(temp)/'cells.json'
        path.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf8')
        subprocess.run([CLI, 'write', str(book), '--sheet', 'Effects', '--data-file', str(path)], check=True)
    print('FireRate=4/8/12; speed=24000; VFX dimensions=2x; independent whole-arm recoil configured.')


if __name__ == '__main__':
    apply()
