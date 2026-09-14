"""Move weapon fields into the shared workbook and author weapon references by id.

Uses excelize-cli; validates the complete source matrices before removing the
old WeaponFields sheet. Existing field ids, row names and tuning are preserved.
"""
import json

from migrate_secondary_weapon_tables import ROOT, DEST, column, normalized, read, run, sheets

FIELDS = ROOT / 'Data/Excel/GuLiStrikeSpellFields.xlsx'
OUT = ROOT / 'TestResults/SpellFieldSource'
REFERENCE = '产生的法术场'


def write_cells(book, sheet, cells):
    payload = OUT / f'{book.stem}-{sheet}-cells.json'
    payload.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf-8')
    run('write', book, '--sheet', sheet, '--data-file', payload)


def data_cells(matrix, rows, first_row):
    cells = []
    for row_number, row in enumerate(rows, first_row):
        for index, value in enumerate(row):
            kind = 'string'
            if value != '':
                source_type = matrix[1][index]
                if source_type == 'int':
                    value, kind = int(float(value)), 'int'
                elif source_type == 'float':
                    value, kind = float(value), 'float'
                elif source_type == 'bool':
                    value, kind = str(value).lower() in ('true', '1'), 'bool'
            cells.append({'cell': f'{column(index + 1)}{row_number}', 'value': value, 'type': kind})
    return cells


def main():
    if 'WeaponFields' not in sheets(DEST):
        if all(REFERENCE in read(DEST, sheet)[0] for sheet in ('Skills', 'WingmanWeapons')):
            print('Spell fields are already consolidated; no values overwritten.')
            return
        raise RuntimeError('WeaponFields is absent but weapon references are not migrated')
    OUT.mkdir(parents=True, exist_ok=True)
    before = {name: read(DEST, name) for name in sheets(DEST)}
    fields_before = read(FIELDS, 'Fields')
    weapon_fields = before['WeaponFields']
    if fields_before[:3] != weapon_fields[:3]:
        raise RuntimeError('Field schemas differ; merge explicitly before migrating')
    merged_rows = sorted(fields_before[3:] + weapon_fields[3:], key=lambda row: int(float(row[0])))
    if len({int(float(row[0])) for row in merged_rows}) != len(merged_rows) or len({row[1] for row in merged_rows}) != len(merged_rows):
        raise RuntimeError('Duplicate field ids or names; migration stopped before writing')
    by_name = {row[1]: int(float(row[0])) for row in merged_rows}
    reference_changes = {}
    for sheet in ('Skills', 'WingmanWeapons'):
        matrix = before[sheet]
        index = matrix[0].index('EffectConfigId')
        if REFERENCE in matrix[0]:
            raise RuntimeError(f'{sheet} has two authored field reference columns')
        values = [by_name[row[index]] if row[index] else '' for row in matrix[3:]]
        reference_changes[sheet] = (index, values)
    previous_exports = {}
    for table in ('DT_GuLiStrikeCommander_Skills', 'DT_GuLiStrikeShip_WingmanWeapons', 'DT_GuLiStrikeSpellFields_Fields'):
        previous_exports[table] = json.loads((ROOT / f'Data/Json/{table}.json').read_text(encoding='utf-8'))
    (OUT / 'before-export.json').write_text(json.dumps(previous_exports, ensure_ascii=False, indent=2), encoding='utf-8')

    # Extend the destination using its existing columns, then verify all values.
    run('insert-rows', FIELDS, '--sheet', 'Fields', '--at', 4, '--count', len(weapon_fields) - 3)
    write_cells(FIELDS, 'Fields', data_cells(fields_before, merged_rows, 4))
    if normalized(read(FIELDS, 'Fields')) != normalized(fields_before[:3] + merged_rows):
        raise RuntimeError('Unified Fields readback differs from source values')
    for sheet, (index, values) in reference_changes.items():
        letter = column(index + 1)
        cells = [{'cell': f'{letter}1', 'value': REFERENCE, 'type': 'string'},
                 {'cell': f'{letter}2', 'value': 'int', 'type': 'string'}]
        cells += [{'cell': f'{letter}{row_number}', 'value': value,
                   'type': 'int' if value != '' else 'string'}
                  for row_number, value in enumerate(values, 4)]
        write_cells(DEST, sheet, cells)
        expected = [row[:] for row in before[sheet]]
        expected[0][index], expected[1][index] = REFERENCE, 'int'
        for row, value in zip(expected[3:], values):
            row[index] = value
        if normalized(read(DEST, sheet)) != normalized(expected):
            raise RuntimeError(f'{sheet}: reference migration changed unrelated values')
    for sheet in ('UnitSkills', 'WeaponMounts', 'WingmanTargeting', 'Projectiles'):
        if read(DEST, sheet) != before[sheet]:
            raise RuntimeError(f'{sheet}: unrelated sheet changed')

    write_cells(DEST, '_Guide', [
        {'cell': 'B3', 'value': '技能执行方式和产生的法术场。该列填写 GuLiStrikeSpellFields.xlsx / Fields 的数字 id；引用法术场时 UnitSkills/Damage 填0。', 'type': 'string'},
        {'cell': 'B5', 'value': '僚机攻击和弹道参数；产生的法术场填写 Fields.id。对地轰炸 Damage 留空，伤害和范围在法术场表维护。', 'type': 'string'},
        {'cell': 'A8', 'value': '产生的法术场', 'type': 'string'},
        {'cell': 'B8', 'value': '所有AOE配置统一维护在 GuLiStrikeSpellFields.xlsx / Fields。此处填数字id；空白或0表示不产生法术场。导出时自动解析为UE行名引用。', 'type': 'string'},
    ])
    for row_number in (3, 5, 8):
        run('row-height', DEST, '--sheet', '_Guide', '--row', row_number, '--height', 42)
    # Only remove the old owner after both the destination and references agree.
    run('del-sheet', DEST, '--name', 'WeaponFields')
    report = {'passed': True, 'field_ids': by_name, 'fields': len(merged_rows),
              'moved_fields': [row[1] for row in weapon_fields[3:]],
              'reference_values': {sheet: values for sheet, (_, values) in reference_changes.items()},
              'unchanged_sheets': ['UnitSkills', 'WeaponMounts', 'WingmanTargeting', 'Projectiles'],
              'field_values_preserved': True}
    (OUT / 'migration.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()
