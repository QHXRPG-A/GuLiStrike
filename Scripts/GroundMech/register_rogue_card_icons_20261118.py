"""Register the five requested card icons using excelize and the shared catalog.

Only image metadata is inspected. Source pixels are not transformed. Gameplay
mechanics stay unchanged; NoSurvivors has artwork only, so no card is invented.
"""
import copy
import hashlib
import json
import shutil
import sys
from pathlib import Path

sys.path.insert(0, 'C:/Users/a/.codex/tmp/enhanced-explosive-card-runtime')
from PIL import Image
from prepare_ground_hud_revision import read, write, cell

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'ArtSource/UI/GroundMech/RogueCardIcons_20261118_v1'
OUT = ROOT / 'Artifacts/GroundRogueCardIcons_20261118'
CATALOG = ROOT / 'ArtSource/UI/GroundMech/Icons/catalog.json'
BINDINGS = {'09.01': 12, '10.01': 10, '11.01': 11, '12.01': 13}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dump(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def main():
    plan = json.loads((BASE / 'generation-plan.json').read_text(encoding='utf-8'))
    catalog = json.loads(CATALOG.read_text(encoding='utf-8'))
    icons_book = ROOT / 'data/Excel/GuLiStrikeIcons.xlsx'
    cards_book = ROOT / 'data/Excel/GuLiStrikeRogueCards.xlsx'
    old_icons = read(icons_book, 'Icons')
    old_cards = read(cards_book, 'Cards')
    ids = {int(row[0]) for row in old_icons[3:] if row and row[0]}
    new_ids = {entry['id'] for entry in plan['icons']}
    assert new_ids == {9, 10, 11, 12, 13}
    assert not ids.intersection(new_ids), 'Do not overwrite registered icons'
    assert ids == {entry['id'] for entry in catalog['icons']}
    assert not new_ids.intersection(catalog['retired_ids'])
    positions = {row[0]: (number, row) for number, row in enumerate(old_cards[3:], 4) if row and row[0]}
    header = old_cards[0]
    icon_col, art_col = header.index('IconId'), header.index('ArtworkId')
    assert all(positions[key][1][icon_col] == ('0' if key == '12.01' else '5') for key in BINDINGS)
    assert positions['12.01'][1][art_col] == '9'
    assert not any('NoSurvivors' in row[1] for row in old_cards[3:])

    entries = []
    for item in plan['icons']:
        png = BASE / 'Icons' / f"{item['id']:02d}_{item['name']}.png"
        with Image.open(png) as image:
            assert image.mode == 'RGBA' and image.format == 'PNG'
            alpha = image.getchannel('A')
            assert alpha.getextrema() == (0, 255), 'Real transparent RGBA required'
            size = list(image.size)
            bounds = alpha.getbbox()
            histogram = alpha.histogram()
            alpha_stats = dict(extrema=[0, 255], bounds=bounds,
                               transparent_pixels=histogram[0], opaque_pixels=histogram[255])
        name = item['name']
        asset = f'/Game/GuLiStrike/GroundMech/UI/Icons/T_GroundIcon_{name}'
        entries.append(dict(id=item['id'], name=name, label=item['label'],
                            note=item['label'] + '；独立简画图标；项目色库；20261118_v1',
                            resource_path=asset + '.T_GroundIcon_' + name,
                            source_png=png.relative_to(ROOT).as_posix(), sha256=sha(png),
                            source_size=size, max_texture_size=256, version=plan['version'],
                            source_manifest=(BASE/'manifest.json').relative_to(ROOT).as_posix(),
                            palette=item['palette'], alpha=alpha_stats,
                            visual_review='pending_user'))

    OUT.mkdir(parents=True, exist_ok=True)
    backup = OUT / 'Before'
    backup.mkdir(exist_ok=True)
    files = [icons_book, cards_book, CATALOG]
    before_hashes = {p.relative_to(ROOT).as_posix(): sha(p) for p in files}
    for path in files:
        target = backup / path.name
        assert not target.exists(), 'Preserve prior registration backup'
        shutil.copy2(path, target)
    dump(OUT/'source-before.json', dict(hashes=before_hashes, icons=old_icons, cards=old_cards,
         generated_headers={p.name: sha(p) for p in (ROOT/'Source/GuLiStrike/Gameplay/Data/Generated').glob('*.h')},
         exported_json={p.name: sha(p) for p in (ROOT/'data/Json').glob('*.json')}))

    new_icons = copy.deepcopy(old_icons)
    for item in entries:
        new_icons.append([str(item['id']), item['name'], item['note'], item['resource_path']])
    icon_cells = [cell(c, r, int(value) if c == 1 else value, 'int' if c == 1 else 'string')
                  for r, row in enumerate(new_icons[len(old_icons):], len(old_icons)+1)
                  for c, value in enumerate(row, 1)]
    new_cards = copy.deepcopy(old_cards)
    card_cells, changes = [], []
    for card_id, icon_id in BINDINGS.items():
        number, row = positions[card_id]
        card_cells.append(cell(icon_col+1, number, icon_id, 'int'))
        new_cards[number-1][icon_col] = str(icon_id)
        change = dict(card_id=card_id, icon_before=int(row[icon_col]), icon_after=icon_id)
        if card_id == '12.01':
            card_cells.append(cell(art_col+1, number, 14, 'int'))
            new_cards[number-1][art_col] = '14'
            change.update(artwork_before=9, artwork_after=14)
        changes.append(change)
    assert all(sha(p) == before_hashes[p.relative_to(ROOT).as_posix()] for p in files)
    write(icons_book, 'Icons', icon_cells)
    write(cards_book, 'Cards', card_cells)
    assert read(icons_book, 'Icons') == new_icons
    assert read(cards_book, 'Cards') == new_cards

    for entry in catalog['icons']:
        entry.setdefault('version', catalog['version'])
        entry.setdefault('source_manifest', catalog['source_manifest'])
    catalog['version'] = 'A_v4+' + plan['version']
    catalog['icons'].extend(entries)
    catalog['slots'].update({entry['name']: entry['id'] for entry in entries})
    catalog['source_manifests'] = [catalog['source_manifest'], entries[0]['source_manifest']]
    catalog['latest_authorization'] = '用户要求按五张参考制作独立图标，沿用本会话正式资源接入范围；视觉验收待用户。'
    dump(CATALOG, catalog)
    manifest = dict(version=plan['version'], generator='built-in image_gen', icons=entries,
                    source_pixels='unmodified original generated PNGs',
                    gameplay_bindings=changes, unbound_icon_ids=[9],
                    no_survivors='Artwork13 exists; no gameplay card row; icon registered only',
                    visual_review='pending_user', assistant_result_visual_test=False)
    dump(BASE/'manifest.json', manifest)
    dump(OUT/'registration.json', dict(success=True, changes=changes, icon_ids=sorted(new_ids),
         unbound_icon_ids=[9], preserved_other_source_cells=True,
         source_files_after={p.relative_to(ROOT).as_posix(): sha(p) for p in files}))
    print(json.dumps(dict(success=True, icon_ids=sorted(new_ids), cards=list(BINDINGS), artwork_id=14)))


if __name__ == '__main__':
    main()
