"""Move secondary-unit weapon authoring to one workbook using excelize.

Run once after capturing projectile values in TestResults/SecondaryWeapons/before-editor.json.
Existing UE table/row identities are retained by export_data_from_excel.py.
Source sheets are removed only after complete value/type readback succeeds.
"""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = 'D:/UE5.7/excelize-cli/bin/xlsx.exe'
OUT = ROOT / 'TestResults/SecondaryWeapons'
DEST = ROOT / 'Data/Excel/GuLiStrikeSecondaryWeapons.xlsx'
MOVES = {
    'GuLiStrikeCommander.xlsx': ['Skills', 'UnitSkills', 'WeaponMounts'],
    'GuLiStrikeShip.xlsx': ['WingmanWeapons', 'WingmanTargeting'],
}


def run(*args):
    return subprocess.check_output([CLI, *map(str, args)], encoding='utf-8')


def sheets(book):
    return json.loads(run('sheets', book, '--json'))['sheets']


def read(book, sheet):
    rows = json.loads(run('read', book, '--sheet', sheet, '--format', 'json', '--raw'))['rows']
    width = len(rows[0])
    return [row + [''] * (width - len(row)) for row in rows]


def column(index):
    result = ''
    while index:
        index, digit = divmod(index - 1, 26)
        result = chr(65 + digit) + result
    return result


def normalized(matrix):
    result = []
    for r, row in enumerate(matrix):
        values = []
        for c, value in enumerate(row):
            if r >= 3 and value != '':
                kind = matrix[1][c]
                if kind in ('int', 'float'): value = float(value)
                elif kind == 'bool': value = str(value).lower() in ('true', '1')
            values.append(value)
        result.append(values)
    return result


def write(sheet, matrix):
    if sheet in sheets(DEST):
        if normalized(read(DEST, sheet)) != normalized(matrix):
            raise RuntimeError(f'{sheet}: destination already exists with different values; merge explicitly')
        return
    run('add-sheet', DEST, '--name', sheet)
    cells = []
    for r, row in enumerate(matrix):
        for c, value in enumerate(row):
            kind = 'string'
            if r >= 3 and value != '':
                source_type = matrix[1][c]
                if source_type == 'float': value, kind = float(value), 'float'
                elif source_type == 'int': value, kind = int(float(value)), 'int'
                elif source_type == 'bool': value, kind = str(value).lower() in ('true', '1'), 'bool'
            cells.append({'cell': f'{column(c + 1)}{r + 1}', 'value': value, 'type': kind})
    path = OUT / f'{sheet}-cells.json'
    path.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf-8')
    run('write', DEST, '--sheet', sheet, '--data-file', path)
    if normalized(read(DEST, sheet)) != normalized(matrix):
        raise RuntimeError(f'{sheet}: source/destination readback mismatch')
    end = column(len(matrix[0]))
    run('style', DEST, '--sheet', sheet, '--range', f'A1:{end}1', '--bold', '--bg', '203864', '--font-color', 'FFFFFF', '--wrap')
    run('style', DEST, '--sheet', sheet, '--range', f'A2:{end}3', '--bg', 'D9E2F3', '--wrap')
    run('col-width', DEST, '--sheet', sheet, '--col', 'A', '--width', 8)
    run('col-width', DEST, '--sheet', sheet, '--col', 'B', '--width', 32)
    run('col-width', DEST, '--sheet', sheet, '--col', 'C', '--width', 55)
    run('col-width', DEST, '--sheet', sheet, '--col', 'D', '--to', end, '--width', 24)
    run('row-height', DEST, '--sheet', sheet, '--row', '1', '--height', 48)


def main():
    # This historical migration split Combat fields out of the global workbook.
    # A consolidated source must never be split again by rerunning it.
    if DEST.exists() and 'WeaponFields' not in sheets(DEST) and 'Skills' in sheets(DEST):
        if '产生的法术场' in read(DEST, 'Skills')[0]:
            print('Unified SpellFields authoring is active; historical split migration skipped.')
            return
    OUT.mkdir(parents=True, exist_ok=True)
    pending = []
    for filename, names in MOVES.items():
        book = ROOT / 'Data/Excel' / filename
        pending.extend((book, name, read(book, name)) for name in names if name in sheets(book))
    global_book = ROOT / 'Data/Excel/GuLiStrikeSpellFields.xlsx'
    fields = read(global_book, 'Fields')
    kind_column = fields[0].index('FieldType')
    combat = [row for row in fields[3:] if row[kind_column] == 'Combat']
    if not pending and not combat and DEST.exists():
        print('Secondary weapon authoring is already migrated; no values overwritten.')
        return
    if not DEST.exists():
        run('new', DEST, '--sheet', '_Guide')
    for _, name, matrix in pending:
        write(name, matrix)
    if combat:
        write('WeaponFields', fields[:3] + combat)
    snapshot = json.loads((OUT / 'before-editor.json').read_text(encoding='utf-8'))
    projectile_path = '/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_Missile'
    motion = snapshot['projectiles'][projectile_path]
    motion_fields = [
        ('SpeedCentimetersPerSecond', 'speed'), ('LiftSeconds', 'lift_seconds'),
        ('MinimumLiftHeightCentimeters', 'minimum_lift_height'), ('MaximumLiftHeightCentimeters', 'maximum_lift_height'),
        ('LateralOffsetCentimeters', 'lateral_offset'), ('ConvergenceDistanceCentimeters', 'convergence_distance'),
        ('TurnRateDegreesPerSecond', 'turn_rate'), ('SweepRadiusCentimeters', 'sweep_radius'),
        ('MaximumLifetimeSeconds', 'maximum_lifetime'),
    ]
    write('Projectiles', [
        ['id', 'name', 'Note', 'ProjectileAsset'] + [name for name, _ in motion_fields],
        ['int', 'str', 'str', 'softobject'] + ['float'] * len(motion_fields),
        ['Necessary', 'Necessary', 'Optional', 'Necessary'] + ['Necessary'] * len(motion_fields),
        [1, 'WM01_Missile', 'WM01追踪导弹；数值在发射时从本表冻结；厘米/秒、厘米、秒、度/秒', projectile_path + '.DA_WM01_Missile']
        + [motion[key] for _, key in motion_fields],
    ])
    guide = [
        ['次级单位武器配置（非玩家直接操控单位）', '修改数值从第4行开始；前三行为导出元数据。'],
        ['UnitSkills', '地面兵种的武器槽、伤害、每秒发数、射程；兵种ID对应Commander/Soldiers。'],
        ['Skills', '技能执行方式和效果ID。带EffectConfigId的武器伤害来自WeaponFields，UnitSkills/Damage填0。'],
        ['WeaponMounts', '枪口和受击瞄准点，模型局部空间厘米。'],
        ['WingmanWeapons', '僚机对空/对地攻击、弹速、寿命、碰撞半径、枪口及攻击动作参数。'],
        ['WingmanTargeting', '僚机索敌、脱战距离和扫描间隔。'],
        ['Projectiles', 'WM01实体导弹飞行参数；ProjectileAsset用于自动接线。'],
        ['WeaponFields', '武器爆炸伤害、半径、时序。Teleport相关列留空；与通用Fields合并为全局运行时目录。'],
        ['即时命中', '普通机枪和僚机对空机枪按开火时结算伤害；僚机对空弹速字段目前不产生飞行延迟。'],
        ['导出', 'python Tools/DataPipeline/export_data_from_excel.py'],
        ['导入', 'python Scripts/ue_exec.py Scripts/import_secondary_weapon_data.py（编辑器内停止PIE后执行）'],
        ['新增列', '先导出、编译GuLiStrikeEditor，再导入。不要直接维护生成的JSON/CSV或DataTable数值。'],
        ['资产名称', '迁移保留已有UE DataTable及行结构ID，manifest记录新的Excel来源；这些名字不代表维护入口。'],
    ]
    guide_file = OUT / 'guide-cells.json'
    guide_file.write_text(json.dumps([{'cell': f'{column(c+1)}{r+1}', 'value': v, 'type': 'string'}
                                      for r, row in enumerate(guide) for c, v in enumerate(row)], ensure_ascii=False), encoding='utf-8')
    run('write', DEST, '--sheet', '_Guide', '--data-file', guide_file)
    run('col-width', DEST, '--sheet', '_Guide', '--col', 'A', '--width', 36)
    run('col-width', DEST, '--sheet', '_Guide', '--col', 'B', '--width', 105)
    run('style', DEST, '--sheet', '_Guide', '--range', 'A1:B13', '--wrap', '--valign', 'top')
    run('style', DEST, '--sheet', '_Guide', '--range', 'A1:B1', '--bold', '--bg', '203864', '--font-color', 'FFFFFF')
    # Persist the exact pre-migration matrices before removing any old source.
    report = {'workbook': str(DEST.relative_to(ROOT)), 'source_matrices': {
        f'{book.name}/{name}': matrix for book, name, matrix in pending},
        'global_fields_before': fields, 'verified_sheets': [name for _, name, _ in pending] + ['WeaponFields', 'Projectiles']}
    (OUT / 'migration.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    for book, name, _ in pending:
        run('del-sheet', book, '--name', name)
    for index in range(len(fields) - 1, 2, -1):
        if fields[index][kind_column] == 'Combat':
            run('delete-rows', global_book, '--sheet', 'Fields', '--at', index + 1, '--count', 1)
    print(json.dumps({'workbook': str(DEST), 'sheets': sheets(DEST), 'source_values_preserved': True}, ensure_ascii=False))


if __name__ == '__main__':
    main()
