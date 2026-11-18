"""Synchronize the approved Icon 11 revision through the Excel-owned pipeline."""
import copy
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
BASE = ROOT/'ArtSource/UI/GroundMech/RogueCardIcons_20261118_v1'
XLSX = 'D:/UE5.7/excelize-cli/bin/xlsx.exe'
BOOK = ROOT/'data/Excel/GuLiStrikeIcons.xlsx'
CARDS = ROOT/'data/Excel/GuLiStrikeRogueCards.xlsx'
CATALOG = ROOT/'ArtSource/UI/GroundMech/Icons/catalog.json'
NOTE = '爆破套餐；独立简画图标；三枚导弹轴线修订；20261118_v2_1'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path):
    return json.loads(path.read_text(encoding='utf-8'))


def dump(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def read(book, sheet):
    return json.loads(subprocess.check_output([XLSX, 'read', str(book), '--sheet', sheet,
                       '--format', 'json'], encoding='utf-8'))['rows']


def register():
    assert not (OUT/'source-before.json').exists(), 'Keep the original integration baseline'
    icons, cards = read(BOOK, 'Icons'), read(CARDS, 'Cards')
    manifest, catalog = load(BASE/'manifest.json'), load(CATALOG)
    assert manifest['version'] == 'RogueCardIcons_20261118_v2_1'
    for entry in manifest['icons']:
        assert sha(ROOT/entry['source_png']) == entry['sha256']
    wanted = {'09.01':12, '10.01':10, '11.01':11, '12.01':13}
    header = cards[0]
    card_rows = {row[0]: dict(zip(header,row)) for row in cards[3:] if row and row[0]}
    assert all(int(card_rows[key]['IconId']) == value for key,value in wanted.items())
    assert not any('NoSurvivors' in row.get('name','') for row in card_rows.values())
    exported = {p.relative_to(ROOT).as_posix():sha(p) for p in (ROOT/'data/Json').iterdir()
                if p.is_file() and p.suffix in ('.json','.csv')}
    headers = {p.relative_to(ROOT).as_posix():sha(p) for p in (ROOT/'Source/GuLiStrike/Gameplay/Data/Generated').glob('*.h')}
    baseline = dict(icons=icons, cards=cards, icon_book_sha256=sha(BOOK), cards_book_sha256=sha(CARDS),
                    exported=exported, headers=headers, catalog=catalog, manifest=manifest,
                    icon_hashes={str(e['id']):e['sha256'] for e in manifest['icons']})
    dump(OUT/'source-before.json', baseline)
    expected = copy.deepcopy(icons)
    number = next(n for n,row in enumerate(icons,1) if row and row[0]=='11')
    assert icons[0] == ['id','name','Note','ResourcePath']
    expected[number-1][2] = NOTE
    subprocess.run([XLSX,'write',str(BOOK),'--sheet','Icons','--cell',f'C{number}',
                    '--value',NOTE,'--type','string'],check=True,capture_output=True,text=True)
    assert read(BOOK,'Icons') == expected
    assert sha(CARDS) == baseline['cards_book_sha256']
    for document in (manifest,catalog):
        next(entry for entry in document['icons'] if entry['id']==11)['note'] = NOTE
    catalog['latest_authorization'] = '用户要求进UE并接入数据管线；同步爆破套餐V2.1源表记录，核对五张正式图标及已有玩法卡绑定。'
    manifest['data_pipeline'] = dict(status='source_registered',
        report='Artifacts/GroundRogueCardIcons_20261118/PipelineIntegration/install-report.json',
        icon_workbook='data/Excel/GuLiStrikeIcons.xlsx', icon_sheet='Icons',
        card_workbook='data/Excel/GuLiStrikeRogueCards.xlsx', card_sheet='Cards',
        registered_icon_ids=[9,10,11,12,13], unchanged_card_bindings=wanted, unbound_icon_ids=[9])
    dump(CATALOG,catalog)
    dump(BASE/'manifest.json',manifest)
    dump(OUT/'registration.json',dict(success=True,changed_cell=f'Icons!C{number}',
        before=icons[number-1][2],after=NOTE,icon_book_sha256=sha(BOOK),
        cards_workbook_unchanged=True,existing_card_bindings=wanted))
    print(json.dumps(dict(success=True,changed_cell=f'Icons!C{number}',card_bindings_unchanged=True)))


def verify_export():
    before=load(OUT/'source-before.json')
    changed=[relative for relative,digest in before['exported'].items() if sha(ROOT/relative)!=digest]
    assert set(changed)=={'data/Json/DT_GuLiStrikeIcons_Icons.json','data/Json/DT_GuLiStrikeIcons_Icons.csv'},changed
    assert all(sha(ROOT/relative)==digest for relative,digest in before['headers'].items())
    assert sha(CARDS)==before['cards_book_sha256']
    rows=load(ROOT/'data/Json/DT_GuLiStrikeIcons_Icons.json')
    for entry in load(BASE/'manifest.json')['icons']:
        row=next(row for row in rows if row['Id']==entry['id'])
        assert row['Name']==entry['name'] and row['ResourcePath']==entry['resource_path'] and row['Note']==entry['note']
    result=dict(success=True,changed_exports=changed,generated_headers_unchanged=True,
        cards_workbook_and_export_unchanged=True,
        table_count=len(load(ROOT/'data/Json/manifest.json')['tables']),
        icons_json_sha256=sha(ROOT/'data/Json/DT_GuLiStrikeIcons_Icons.json'),
        cards_json_sha256=sha(ROOT/'data/Json/DT_GuLiStrikeRogueCards_Cards.json'))
    dump(OUT/'export-verification.json',result)
    print(json.dumps(result,ensure_ascii=False))


if __name__=='__main__':
    {'register':register,'verify-export':verify_export}[sys.argv[1]]()
