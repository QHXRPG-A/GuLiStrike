"""Update the maintained Excel catalogue via excelize-cli, without rebuilding IDs."""
import hashlib
import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Models.migrate_model_workbooks import cli, read, letter

BOOK = ROOT / 'Data/Excel/GuLiStrikeModels.xlsx'
OUT = ROOT / 'ArtSource/SweeperTeamColor_v1_20261008'
MASK = '/Game/GuLiStrike/Models/TeamColor_v1/SweeperSummon/Textures/T_Sweeper_OrangeTeamMask.T_Sweeper_OrangeTeamMask'


def main():
    before_hash = hashlib.sha256(BOOK.read_bytes()).hexdigest()
    sheets = {s: read(BOOK, s) for s in ['Models', 'MaterialParameters', 'ColorRegions']}
    changed, deleted = {}, {}
    updates = {
        'Models': {
            1005: {'Note': '2026-10-09 用户放行橙区B_v1正式导入；原网格/三档LOD/固定色保留；UV0精确遮罩与CPD8、16驱动本地阵营色', 'bTeamColorEnabled': True},
            2004: {'Note': '用户取消克隆兵营占位改色制作；保留稳定模型ID、原资源和配色', 'bTeamColorEnabled': False},
            2007: {'Note': '用户取消领地据点占位改色制作；保留稳定模型ID、原资源和配色', 'bTeamColorEnabled': False}},
        'MaterialParameters': {
            2791: {'Scope': 'Existing', 'Note': '扫荡者原橙色UV0精确遮罩；浅队色CPD8–11，其他原底色固定'},
            2793: {'Scope': 'Existing', 'Note': '扫荡者统一队色启用CPD16；未知阵营使用中性深灰'}},
        'ColorRegions': {
            9396: {'RegionKey': 'OriginalOrangeArmor', 'DisplayName': '原橙色队色区',
                   'MaskSource': MASK, 'Scope': 'Existing',
                   'Note': '原UV0橙区精确贴图遮罩；跨色简化面逐像素限制替色；其他原色、黄灯及三档明暗不变'}}}
    removed = {'Models': set(), 'MaterialParameters': {2792, 2794, *range(2811, 2815), *range(2823, 2827)},
               'ColorRegions': {9397, 9398}}
    # Validate every source identity and field before the first workbook mutation.
    for sheet, data in sheets.items():
        names = data[0]
        ids = {int(r[0]) for r in data[3:] if r and r[0]}
        if not set(updates[sheet]) <= ids:
            raise RuntimeError('Missing stable rows: ' + sheet)
        if not all(set(fields) <= set(names) for fields in updates[sheet].values()):
            raise RuntimeError('Source schema changed: ' + sheet)
    if hashlib.sha256(BOOK.read_bytes()).hexdigest() != before_hash:
        raise RuntimeError('Workbook changed during preparation; reread before writing.')
    for sheet, data in sheets.items():
        deleted[sheet] = [{'row': i, 'id': int(r[0])} for i, r in enumerate(data, 1)
                          if i > 3 and r and r[0] and int(r[0]) in removed[sheet]]
        for item in reversed(deleted[sheet]):
            cli('delete-rows', BOOK, '--sheet', sheet, '--at', item['row'], '--count', 1)
        current = read(BOOK, sheet)
        cells = []
        changed[sheet] = []
        for row_index, row in enumerate(current[3:], 4):
            if not row or not row[0] or int(row[0]) not in updates[sheet]:
                continue
            rid = int(row[0])
            for field, value in updates[sheet][rid].items():
                col = current[0].index(field)
                old = row[col] if col < len(row) else ''
                cells.append({'cell': f'{letter(col + 1)}{row_index}', 'value': value,
                              'type': 'bool' if isinstance(value, bool) else 'string'})
                changed[sheet].append({'id': rid, 'field': field, 'before': old, 'after': value})
        with tempfile.TemporaryDirectory(prefix='guli-sweeper-cells-') as tmp:
            cell_file = Path(tmp) / 'cells.json'
            cell_file.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf8')
            cli('write', BOOK, '--sheet', sheet, '--data-file', cell_file)
        actual = read(BOOK, sheet)
        old_by_id = {int(r[0]): r for r in data[3:] if r and r[0]}
        new_by_id = {int(r[0]): r for r in actual[3:] if r and r[0]}
        if data[:3] != actual[:3] or set(new_by_id) != set(old_by_id) - removed[sheet]:
            raise RuntimeError('Workbook schema/row identity parity failed: ' + sheet)
        for rid, row in new_by_id.items():
            expected = list(old_by_id[rid])
            for field, value in updates[sheet].get(rid, {}).items():
                expected[actual[0].index(field)] = str(value).lower() if isinstance(value, bool) else value
            def canonical(values):
                return [str(v).lower() in ('true', '1') if actual[1][i]=='bool' else str(v)
                        for i, v in enumerate(values)]
            if canonical(row) != canonical(expected):
                raise RuntimeError('Unexpected cell difference: ' + str((sheet, rid)))
    report = {'success': True, 'workbook': str(BOOK.relative_to(ROOT)),
              'source_sha256_before': before_hash, 'source_sha256_after': hashlib.sha256(BOOK.read_bytes()).hexdigest(),
              'changed': changed, 'deleted_unused_bindings': deleted,
              'stable_model_ids_preserved': True, 'unrelated_rows_exact': True,
              'counts': {s: len(read(BOOK, s)) - 3 for s in ['Models', 'Parts', 'MaterialParameters', 'ColorRegions']}}
    (OUT / 'catalog-source-update.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps({'success': True, 'counts': report['counts'], 'unrelated_rows_exact': True}))


if __name__ == '__main__':
    main()
