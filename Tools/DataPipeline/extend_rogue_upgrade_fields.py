"""Idempotent Excelize migration of the three rogue-card upgrade visual fields."""
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
XLSX = Path('D:/UE5.7/excelize-cli/bin/xlsx.exe')
BOOK = ROOT / 'data/Excel/GuLiStrikeRogueCards.xlsx'
SYSTEM = '/Game/GuLiStrike/FX/RogueCards/NS_RogueUpgrade_Lite.NS_RogueUpgrade_Lite'
COLOR = '(R=20,G=6.930114,B=0.933554,A=1)'
WM01_DEFAULT_SCALE = 6.0  # 12.5 m in-game width; the shared effect's base diameter is only 3.2 m.


def run(*args):
    return subprocess.check_output([str(XLSX), *map(str, args)], encoding='utf-8')


def write(sheet, cells):
    with tempfile.TemporaryDirectory(prefix='guli-rogue-upgrade-') as directory:
        source = Path(directory) / 'cells.json'
        source.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf-8')
        run('write', BOOK, '--sheet', sheet, '--data-file', source)


def main():
    rows = json.loads(run('read', BOOK, '--sheet', 'Cards', '--format', 'json'))['rows']
    fields = [('UpgradeVfx', 'softobject', SYSTEM), ('UpgradeVfxScale', 'float', WM01_DEFAULT_SCALE),
              ('UpgradeVfxColor', 'str', COLOR)]
    if rows[0][:9] != ['id', 'name', 'Note', 'Type', 'TextIds', 'ImplementationClass', 'UnitTypeId', 'BonusPercent', 'FrontMaterial']:
        raise RuntimeError('Unexpected Cards schema; no changes made')
    if len(rows[0]) > 9 and rows[0][9:12] != [f[0] for f in fields]:
        raise RuntimeError('Columns J:L already belong to another feature')
    cells = []
    for col, (name, kind, value) in zip('JKL', fields):
        for row, text in enumerate([name, kind, 'Necessary'], 1):
            cells.append({'cell': f'{col}{row}', 'value': text, 'type': 'string'})
        for index, row in enumerate(rows[3:], 4):
            if not row or not row[0]:
                continue
            # Preserve future authored values when the migration is re-run.
            old = row[ord(col)-65] if len(row) > ord(col)-65 else ''
            if old not in ('', None):
                continue
            cells.append({'cell': f'{col}{index}', 'value': value, 'type': 'float' if kind == 'float' else 'string'})
    write('Cards', cells)
    notes = [
        ('UpgradeVfx', '完整Niagara System软对象路径。首批共用NS_RogueUpgrade_Lite；特效配置失败只跳过表现，不撤销属性。'),
        ('UpgradeVfxScale', '有限正数。战争机器当前默认6.0（实战宽约12.5米）；1.0为基础特效尺寸，光圈精灵直径约3.2米。统一缩放光圈、辉光和光粒，其他单位应单独适配。'),
        ('UpgradeVfxColor', '线性HDR色值：(R=20,G=6.930114,B=0.933554,A=1)。RGB>=0且有限，A在0~1；来自原UpgradeGlow04主要光圈初始颜色。'),
        ('敌方颜色', '每个客户端按自身阵营判断：己方读本行颜色；敌方统一红色，保留亮度与透明度变化。'),
        ('升级播放', '成功结算时冻结当前存活受益单位。选牌和模糊退出后广播一次；新生单位继承加成、不补播特效。'),
        ('低耗版本', '两个GPU发射器；每单位1辉光网格+1光圈+18/6/0个独立随机短光粒（高/中/低画质）；每批1024单位槽、20480个GPU粒子容量，组件共享池化。'),
    ]
    write('_说明', [{'cell': f'{col}{row}', 'value': value, 'type': 'string'}
                    for row, pair in enumerate(notes, 18) for col, value in zip('AB', pair)])
    run('style', BOOK, '--sheet', 'Cards', '--range', 'J1:L1', '--bold', '--bg', 'D9E1F2', '--border')
    for col, width in [('J', 65), ('K', 20), ('L', 58)]:
        run('col-width', BOOK, '--sheet', 'Cards', '--col', col, '--width', width)
    print(run('read', BOOK, '--sheet', 'Cards', '--range', 'J1:L6', '--format', 'json'))


if __name__ == '__main__':
    main()
