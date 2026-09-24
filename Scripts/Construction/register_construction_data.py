"""Register construction materials and the rotation hint through the project's Excel CLI."""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = Path('D:/UE5.7/excelize-cli/bin/xlsx.exe')
OUT = ROOT / 'outputs/construction'
OUT.mkdir(parents=True, exist_ok=True)


def command(*args):
    result = subprocess.run([str(CLI), *map(str, args)], capture_output=True, text=True, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(result.stderr or result.stdout)
    return json.loads(result.stdout) if result.stdout.strip().startswith('{') else result.stdout


def register():
    book = ROOT / 'Data/Excel/GuLiStrikeVfx.xlsx'
    rows = command('read', book, '--sheet', 'Effects', '--format', 'json', '--raw')['rows']
    current = {r[1]: (index + 1, r) for index, r in enumerate(rows) if index >= 3 and r[0]}
    next_id = max(int(r[0]) for _, r in current.values()) + 1
    edits, ids = [], {}
    definitions = [
        ('BuildingConstructionHologram', 'M_ConstructionHologram', '未完工建筑蓝色透明虚影'),
        ('BuildingConstructionFinish', 'M_ConstructionFinishGlow', '建筑完工蓝白色边缘发光'),
        ('BuildingPlacementGrid', 'M_BuildingPlacementGrid', '建筑占地区域世界坐标一米网格'),
    ]
    last_row = len(rows)
    for name, asset, note in definitions:
        path = f'/Game/GuLiStrike/Buildings/Construction/{asset}.{asset}'
        if name in current:
            index, old = current[name]
            assert old[3] == path, (name, old[3])
            ids[name] = int(old[0])
            continue
        ids[name] = next_id
        last_row += 1
        values = [next_id, name, note, path, 1, 1, 1]
        edits.extend({'cell': f'{chr(65 + col)}{last_row}', 'value': value} for col, value in enumerate(values))
        next_id += 1
    if edits:
        data = OUT / 'vfx-cells.json'
        data.write_text(json.dumps(edits, ensure_ascii=False), encoding='utf-8')
        command('write', book, '--sheet', 'Effects', '--data-file', data)
    text_book = ROOT / 'Data/Excel/GuLiStrikeGameTexts.xlsx'
    texts = command('read', text_book, '--sheet', 'Texts', '--format', 'json', '--raw')['rows']
    key = 'UI.BuildingPlacementComponent.RotateHint'
    if not any(r and r[0] == key for r in texts[3:]):
        number = len(texts) + 1
        data = OUT / 'text-cells.json'
        data.write_text(json.dumps([
            {'cell': f'A{number}', 'value': key},
            {'cell': f'B{number}', 'value': '建筑放置操作提示'},
            {'cell': f'C{number}', 'value': 'R：旋转90°'},
        ], ensure_ascii=False), encoding='utf-8')
        command('write', text_book, '--sheet', 'Texts', '--data-file', data)
    config = ROOT / 'Config/DefaultGame.ini'
    content = config.read_text(encoding='utf-8-sig')
    section = '[/Script/GuLiStrike.GuLiBuildingConstructionSettings]'
    desired = '\n'.join([
        section, '; Factory candidate; other definitions remain opt-in until visual approval.',
        '+EnabledDefinitionIds=6',
        f'HologramVfxId={ids["BuildingConstructionHologram"]}',
        f'FinishGlowVfxId={ids["BuildingConstructionFinish"]}',
        f'GridVfxId={ids["BuildingPlacementGrid"]}', 'FinishSeconds=0.65', '',
    ])
    if section not in content:
        config.write_text(content.rstrip() + '\n\n' + desired, encoding='utf-8')
    (OUT / 'registered-data.json').write_text(json.dumps(ids, indent=2), encoding='utf-8')
    return ids


if __name__ == '__main__':
    print(json.dumps(register()))
