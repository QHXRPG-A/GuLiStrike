"""Create the card text style source once; subsequent edits belong in Excel."""
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = 'D:/UE5.7/excelize-cli/bin/xlsx.exe'


def main():
    path = ROOT / 'data/Excel/GuLiStrikeRogueCardUI.xlsx'
    if path.exists():
        raise SystemExit('Style workbook already exists; edit its cells instead of reseeding.')
    rows = [
        ['id', 'name', 'Note', 'FontAsset', 'FontSize', 'Typeface', 'ColorSRGB', 'OutlineSize', 'OutlineColorSRGB'],
        ['int', 'str', 'str', 'softobject', 'int', 'str', 'str', 'int', 'str'],
        ['Necessary', 'Necessary', 'Optional', 'Necessary', 'Necessary', 'Necessary', 'Necessary', 'Necessary', 'Necessary'],
        [1, 'Default', '普通连接词；标题不使用本表', '/Engine/EngineFonts/Roboto.Roboto', 38, 'Regular', '#FAFAF5', 0, '#000000'],
        [2, 'Unit', '单位名；Bold与同色描边共同加粗中文回退字形', '/Engine/EngineFonts/Roboto.Roboto', 38, 'Bold', '#FFD84A', 1, '#FFD84A'],
        [3, 'Gain', '增益属性及数值；样式由公共文本中的Gain标签引用', '/Engine/EngineFonts/Roboto.Roboto', 38, 'Bold', '#FFD84A', 1, '#FFD84A'],
    ]
    cells = [{'cell': f'{chr(65+c)}{r}', 'value': value,
              'type': 'int' if isinstance(value, int) else 'string'}
             for r, row in enumerate(rows, 1) for c, value in enumerate(row)]
    subprocess.run([CLI, 'new', str(path), '--sheet', 'TextStyles'], check=True)
    with tempfile.TemporaryDirectory() as folder:
        data = Path(folder) / 'cells.json'
        data.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf-8')
        subprocess.run([CLI, 'write', str(path), '--sheet', 'TextStyles', '--data-file', str(data)], check=True)
    subprocess.run([CLI, 'style', str(path), '--sheet', 'TextStyles', '--range', 'A1:I1',
                    '--bold', '--bg', '17365D', '--font-color', 'FFFFFF'], check=True)
    subprocess.run([CLI, 'col-width', str(path), '--sheet', 'TextStyles', '--col', 'A', '--to', 'I', '--width', '24'], check=True)
    subprocess.run([CLI, 'col-width', str(path), '--sheet', 'TextStyles', '--col', 'C', '--to', 'D', '--width', '62'], check=True)
    actual = json.loads(subprocess.check_output([CLI, 'read', str(path), '--sheet', 'TextStyles', '--format', 'json'], encoding='utf-8'))['rows']
    assert actual == [[str(value) for value in row] for row in rows], 'Excel style readback mismatch'
    out = ROOT / 'Artifacts/RogueCards/SingleLineText/style-source.json'
    out.write_text(json.dumps({'success': True, 'workbook': str(path), 'sheet': 'TextStyles', 'rows': actual}, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('Created and read back the three Excel-owned text styles.')


if __name__ == '__main__':
    main()
