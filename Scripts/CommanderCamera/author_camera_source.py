"""Idempotently author the approved commander camera row through excelize-cli."""
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = Path('D:/UE5.7/excelize-cli/bin/xlsx.exe')
BOOK = ROOT / 'Data/Excel/GuLiStrikeCommander.xlsx'
SHEET = 'Camera'
VALUES = {
    'MinimumHeightMeters': 35.0, 'TacticalStartHeightMeters': 300.0,
    'TacticalMaximumHeightMeters': 700.0, 'InitialHeightMeters': 133.4,
    'NearPitchDegrees': 25.0, 'TacticalPitchDegrees': 55.0, 'OverviewPitchDegrees': 90.0,
    'FieldOfViewDegrees': 45.0, 'ZoomStepMultiplier': 1.18,
    'ZoomInterpolationPerSecond': 8.0, 'YawDegreesPerSecond': 70.0,
    'MoveHeightMultiplierPerSecond': 1.4, 'MinimumMoveMetersPerSecond': 12.0,
    'MaximumMoveMetersPerSecond': 92.0,
    'RiseHalfLifeSeconds': 0.2, 'DescentHalfLifeSeconds': 0.35,
    'MaximumRiseMetersPerSecond': 60.0, 'MaximumDescentMetersPerSecond': 30.0,
    'LookAheadSeconds': 0.75, 'BoundaryPaddingMeters': 10.0,
    'PivotClearanceMeters': 0.3, 'BoomClearanceMeters': 0.4,
    'CameraClearanceMeters': 1.0, 'BoomSampleSpacingMeters': 20.0,
    'OverviewTransitionSeconds': 0.6, 'OverviewPaddingFraction': 0.05,
    'OverviewUnitIconPixels': 14.0, 'OverviewBuildingIconPixels': 20.0,
    'OverviewYawDegrees': 0.0,
}


def run(*args):
    return subprocess.check_output([str(CLI), *map(str, args)], text=True, encoding='utf-8')


def column(index):
    result = ''
    while index:
        index, remainder = divmod(index - 1, 26)
        result = chr(65 + remainder) + result
    return result


def main():
    sheets = json.loads(run('sheets', BOOK, '--json'))['sheets']
    if SHEET in sheets:
        raise SystemExit('Camera已存在；请直接编辑源表，禁止用首版种子覆盖调参。')
    run('add-sheet', BOOK, '--name', SHEET)
    fields = [('id', 'int', 'Necessary', 1), ('name', 'str', 'Necessary', 'Default'),
              ('Note', 'str', 'Optional', '三档镜头；距离米、俯角向下为正、时间秒。x由战场和HUD可用画面计算；数值修改后导出并导入Camera表。')]
    fields += [(key, 'float', 'Necessary', value) for key, value in VALUES.items()]
    cells = []
    for index, field in enumerate(fields, 1):
        for row, value in enumerate(field, 1):
            cells.append({'cell': f'{column(index)}{row}', 'value': value,
                          'type': 'float' if row == 4 and index > 3 else 'int' if row == 4 and index == 1 else 'string'})
    with tempfile.TemporaryDirectory(prefix='guli-camera-') as directory:
        data = Path(directory) / 'cells.json'
        data.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf-8')
        run('write', BOOK, '--sheet', SHEET, '--data-file', data)
    run('style', BOOK, '--sheet', SHEET, '--range', f'A1:{column(len(fields))}1', '--bold', '--bg', 'D9E1F2', '--wrap')
    run('col-width', BOOK, '--sheet', SHEET, '--col', 'D', '--to', column(len(fields)), '--width', '24')
    run('col-width', BOOK, '--sheet', SHEET, '--col', 'C', '--width', '55')
    run('style', BOOK, '--sheet', SHEET, '--range', 'C4', '--wrap')
    run('row-height', BOOK, '--sheet', SHEET, '--row', '1', '--height', '48')
    run('row-height', BOOK, '--sheet', SHEET, '--row', '4', '--height', '60')
    print(run('info', BOOK))


if __name__ == '__main__':
    main()
