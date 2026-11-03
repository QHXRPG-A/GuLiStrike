"""Promote the approved A_v4 PNGs through the existing Excel icon catalog.

Source pixels are never resized or recolored here. Texture build settings cap
runtime dimensions; the HUD fits the original aspect ratio in its icon slots.
"""
import hashlib
import json
from pathlib import Path
from PIL import Image
from prepare_ground_hud_revision import cli, read, write, cell

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'ArtSource/UI/GroundMech/IconStyle_Toon3D_20261103/SingleIcons_A_v4'
CATALOG = ROOT / 'ArtSource/UI/GroundMech/Icons/catalog.json'
OUT = ROOT / 'Artifacts/GroundToonIcons_20261103'
CONSUMERS = [
    ('GuLiStrikeRogueCards.xlsx', 'Cards'),
    ('GuLiStrikeMech.xlsx', '技能表'),
    ('GuLiStrikeBuffs.xlsx', 'Buffs'),
    ('GuLiStrikeGroundHUD.xlsx', 'Elements'),
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    manifest = json.loads((SOURCE / 'manifest.json').read_text(encoding='utf-8'))
    source = {item['id']: item for item in manifest['files']}
    assert set(source) == set(range(1, 9))
    assert source[1]['sha256'] == source[7]['sha256']
    icons = []
    for ident in (1, 2, 3, 4, 5, 6, 8):
        item = source[ident]
        png = SOURCE / item['filename']
        assert sha(png) == item['sha256'], str(png)
        with Image.open(png) as image:
            assert image.format == 'PNG' and image.mode == 'RGBA'
            low, high = image.getchannel('A').getextrema()
            assert low == 0 and high > 0
            size = list(image.size)
        name = item['name']
        resource = f'/Game/GuLiStrike/GroundMech/UI/Icons/T_GroundIcon_{name}'
        icons.append({'id': ident, 'name': name,
                      'note': item['label'] + '；3D三渲二三档描边 A_v4' + ('；喷气跳跃共用' if ident == 1 else ''),
                      'resource_path': resource + '.T_GroundIcon_' + name,
                      'source_png': png.relative_to(ROOT).as_posix(),
                      'sha256': item['sha256'], 'source_size': size, 'max_texture_size': 256})

    # Validate the existing IDs and all known business references before editing.
    book = ROOT / 'data/Excel/GuLiStrikeIcons.xlsx'
    rows = read(book, 'Icons')
    positions = {int(row[0]): (number, row) for number, row in enumerate(rows[3:], 4) if row and row[0]}
    for item in icons:
        assert positions[item['id']][1][1] == item['name']
    for filename, sheet in CONSUMERS:
        table = read(ROOT / 'data/Excel' / filename, sheet)
        index = table[0].index('IconId')
        for row in table[3:]:
            if len(row) > index and row[index] == '7':
                assert filename == 'GuLiStrikeMech.xlsx' and row[1] == 'RocketJump', (filename, row[1])
    files = [book, ROOT / 'data/Excel/GuLiStrikeMech.xlsx']
    OUT.mkdir(parents=True, exist_ok=True)
    before = {str(path.relative_to(ROOT)): sha(path) for path in files}
    generated_before = {path.name: sha(path) for path in (ROOT / 'Source/GuLiStrike/Gameplay/Data/Generated').glob('*.h')}
    baseline = OUT / 'source_baseline.json'
    if not baseline.exists():
        baseline.write_text(json.dumps({'workbooks': before, 'generated_headers': generated_before}, indent=2), encoding='utf-8')

    write(book, 'Icons', [entry for item in icons for entry in (
        cell(3, positions[item['id']][0], item['note']),
        cell(4, positions[item['id']][0], item['resource_path']))])
    mech = files[1]
    skills = read(mech, '技能表')
    column = skills[0].index('IconId') + 1
    number = next(n for n, row in enumerate(skills[3:], 4) if row[1] == 'RocketJump')
    write(mech, '技能表', [cell(column, number, 1, 'int')])
    if 7 in positions:
        number, row = positions[7]
        assert row[1] == 'Jump'
        cli('delete-rows', book, '--sheet', 'Icons', '--at', number, '--count', 1)

    catalog = {'version': 'A_v4', 'approval': '用户要求：通过数据管线实装在游戏里',
               'source_manifest': (SOURCE / 'manifest.json').relative_to(ROOT).as_posix(),
               'icons': icons,
               'slots': {'JetCapacity': 1, 'Ricochet': 2, 'Burning': 3, 'BurningBurst': 4,
                         'Fire': 5, 'Aim': 6, 'Jump': 1, 'Blink': 8},
               'retired_ids': [7],
               'retired_assets': ['/Game/GuLiStrike/GroundMech/UI/Icons/T_GroundIcon_Jump'],
               'pixel_processing': 'none; Unreal texture build caps dimensions; HUD fits aspect ratio'}
    CATALOG.parent.mkdir(parents=True, exist_ok=True)
    CATALOG.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    final_rows = read(book, 'Icons')
    assert 7 not in {int(row[0]) for row in final_rows[3:] if row and row[0]}
    final_skills = read(mech, '技能表')
    assert next(row for row in final_skills[3:] if row[1] == 'RocketJump')[column - 1] == '1'
    report = {'success': True, 'catalog': str(CATALOG), 'icons': len(icons),
              'source_files_verified': True, 'shared_jump_icon_id': 1,
              'workbooks_before': before, 'workbooks_after': {str(path.relative_to(ROOT)): sha(path) for path in files}}
    (OUT / 'data_preparation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'success': True, 'unique_icons': len(icons), 'jump_icon_id': 1}))


if __name__ == '__main__':
    main()
