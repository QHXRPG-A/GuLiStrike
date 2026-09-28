"""Import the human-readable card text sheet without changing artwork/Blueprints.

Run in UE: python Scripts/ue_exec.py Scripts/Cards/import_card_text.py
Stable CardId determines the FText namespace/key, not the current source wording.
"""
import csv
import io
import json
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/Data')
TABLE='/Game/GuLiStrike/CardSystem/WarMachineTarot/Data/DT_CardText'
assert not unreal.WidgetService.is_pie_running(), 'Stop the card preview before importing its text'
with (ROOT/'CardTextSource.csv').open(encoding='utf-8-sig',newline='') as stream:
    rows=list(csv.DictReader(stream))
ids=[r['CardId'] for r in rows]
assert len(ids)==len(set(ids)) and all(ids),'CardId must be nonempty and unique'
out=io.StringIO(newline='')
writer=csv.writer(out)
writer.writerow(['Name','Title','Description'])
for row in rows:
    def localized(field):
        return 'NSLOCTEXT('+','.join(json.dumps(s,ensure_ascii=False) for s in
            ['GuLiStrike.Cards',row['CardId']+'.'+field,row[field]])+')'
    writer.writerow([row['CardId'],localized('Title'),localized('Description')])
source=ROOT/'CardText.csv'
source.write_text(out.getvalue(),encoding='utf-8-sig')
table=unreal.load_asset(TABLE)
assert table,'Create FCardTextRow/DT_CardText before importing'
assert unreal.DataTableFunctionLibrary.fill_data_table_from_csv_string(table,out.getvalue(),table.get_editor_property('row_struct'))
import_data=table.get_editor_property('asset_import_data')
if import_data:import_data.scripted_add_filename(str(source),0,'Card text source')
assert unreal.EditorAssetLibrary.save_loaded_asset(table,False)
export=unreal.DataTableFunctionLibrary.export_data_table_to_csv_string(table)
(ROOT/'CardText_UE_Export.csv').write_text(export,encoding='utf-8-sig')
assert export.count('NSLOCTEXT')==2*len(rows),'Localized FText identities must survive import/export'
report={'table':TABLE,'source':str(ROOT/'CardTextSource.csv'),'row_ids':ids,
        'localized_text_count':2*len(rows),'namespace':'GuLiStrike.Cards','success':True}
(ROOT.parent/'Inspection'/'text-import.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
