"""Promote the requested v2 artwork; preserve IDs, bindings and original pixels."""
import copy
import hashlib
import json
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT/'ArtSource/UI/GroundMech/RogueCardIcons_20261118_v1'
OUT = Path(__file__).resolve().parent
sys.path.insert(0, 'C:/Users/a/.codex/tmp/enhanced-explosive-card-runtime')
sys.path.insert(0, str(ROOT/'Scripts/GroundMech'))
from PIL import Image
from prepare_ground_hud_revision import read, write, cell


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dump(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def main():
    baseline = json.loads((OUT/'source-before.json').read_text(encoding='utf-8'))
    for relative, digest in baseline['files'].items():
        assert sha(ROOT/relative) == digest, 'Preserve changed source: '+relative
    previous = json.loads((BASE/'Revisions/v1/manifest.json').read_text(encoding='utf-8'))
    current = json.loads((BASE/'manifest.json').read_text(encoding='utf-8'))
    assert current == previous
    plan = json.loads((BASE/'generation-plan-v2.json').read_text(encoding='utf-8'))
    generated = json.loads((BASE/'generation-results-v2.json').read_text(encoding='utf-8'))
    assert len(generated['adopted']) == 5
    outputs = {item['id']: item for item in generated['adopted']}
    assert set(outputs) == {9, 10, 11, 12, 13}
    assert outputs[13]['axis_correction'] is True
    catalog_file = ROOT/'ArtSource/UI/GroundMech/Icons/catalog.json'
    catalog = json.loads(catalog_file.read_text(encoding='utf-8'))
    old_entries = {item['id']: item for item in catalog['icons']}
    new_entries = {}
    for item in plan['icons']:
        ident = item['id']
        output = outputs[ident]
        source = Path(output['source_path'])
        old = old_entries[ident]
        assert sha(ROOT/old['source_png']) == old['sha256']
        assert sha(BASE/'Revisions/v1/Icons'/Path(old['source_png']).name) == old['sha256']
        with Image.open(source) as image:
            assert image.mode == 'RGBA' and image.format == 'PNG'
            alpha = image.getchannel('A')
            assert alpha.getextrema() == (0, 255)
            histogram = alpha.histogram()
            stats = dict(extrema=[0, 255], bounds=alpha.getbbox(),
                         transparent_pixels=histogram[0], opaque_pixels=histogram[255])
            size = list(image.size)
        entry = copy.deepcopy(old)
        entry.update(note=item['label']+'；独立简画图标；构图与体积修订；20261118_v2',
                     version=plan['version'], source_size=size, sha256=sha(source), alpha=stats,
                     visual_review='pending_user', palette=item['palette'],
                     prompt_file=output['prompt_file'])
        new_entries[ident] = entry
    book = ROOT/'data/Excel/GuLiStrikeIcons.xlsx'
    old_rows = read(book, 'Icons')
    expected = copy.deepcopy(old_rows)
    note_index = old_rows[0].index('Note')
    cells = []
    for number, row in enumerate(old_rows[3:], 4):
        if row and row[0] and int(row[0]) in new_entries:
            entry = new_entries[int(row[0])]
            assert row[1] == entry['name'] and row[3] == entry['resource_path']
            cells.append(cell(note_index+1, number, entry['note']))
            expected[number-1][note_index] = entry['note']
    assert len(cells) == 5
    for ident, entry in new_entries.items():
        shutil.copy2(outputs[ident]['source_path'], ROOT/entry['source_png'])
        assert sha(ROOT/entry['source_png']) == entry['sha256']
    write(book, 'Icons', cells)
    assert read(book, 'Icons') == expected
    for index, entry in enumerate(catalog['icons']):
        if entry['id'] in new_entries:
            catalog['icons'][index] = new_entries[entry['id']]
    catalog['version'] = 'A_v4+'+plan['version']
    catalog['latest_authorization'] = '用户要求检索GitHub提示词并提升五图设计，随后要求修正强化爆炸弹导弹错轴；延续正式替换范围，视觉验收待用户。'
    dump(catalog_file, catalog)
    current.update(version=plan['version'], icons=[new_entries[i] for i in sorted(new_entries)],
                   previous_version='Revisions/v1/manifest.json',
                   generation_plan='generation-plan-v2.json', generation_results='generation-results-v2.json',
                   research='Research/prompt_research_20261118.json',
                   user_feedback='Improve icon design; additionally straighten the explosive rocket axis',
                   visual_review='pending_user', assistant_result_visual_test=False,
                   ue_import=dict(status='pending_texture_revision', target_ids=sorted(new_entries)))
    dump(BASE/'manifest.json', current)
    cards = ROOT/'data/Excel/GuLiStrikeRogueCards.xlsx'
    assert sha(cards) == baseline['files'][cards.relative_to(ROOT).as_posix()]
    report = dict(success=True, icon_ids=sorted(new_entries), icon_note_cells=cells,
                  cards_workbook_unchanged=True, stable_asset_paths=True,
                  source_hashes={str(i): x['sha256'] for i, x in new_entries.items()},
                  files_after={relative: sha(ROOT/relative) for relative in baseline['files']})
    dump(OUT/'registration.json', report)
    print(json.dumps(dict(success=True, icon_ids=sorted(new_entries), cards_workbook_unchanged=True)))


if __name__ == '__main__':
    main()
