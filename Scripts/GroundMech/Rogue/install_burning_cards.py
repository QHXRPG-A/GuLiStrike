"""Import only this feature's tables into the rebuilt editor; never starts PIE."""
import json
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'Artifacts/BurningRogue_20261029'
TABLES=['DT_GuLiStrikeRogueCards_Cards','DT_GuLiStrikeRogueCards_Effects',
        'DT_GuLiStrikeRogueCards_GroundRicochet','DT_GuLiStrikeBuffs_Buffs',
        'DT_GuLiStrikeBuffs_Burning','DT_GuLiStrikeBuffs_BurningBurst',
        'DT_GuLiStrikeVfx_Effects','DT_GuLiStrikeGameTexts_Texts','DT_GuLiStrikeMech_Skills']

def require(value,message):
    if not value:raise RuntimeError(message)
    return value

def main():
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),'Preserve active PIE')
    checks=[(unreal.GuLiStrikeRogueCardsCardsRow(),'effect_ids'),
            (unreal.GuLiStrikeBuffsBuffsRow(),'implementation_class'),
            (unreal.GuLiStrikeCardArtworkLayersRow(),'motion_owner_layer')]
    for row,field in checks:row.get_editor_property(field)
    for name in ['GuLiRogueCardGroundEffect','GuLiBurningBuffHandler','GuLiBurningBurstBuffHandler']:
        require(unreal.load_class(None,'/Script/GuLiStrike.'+name),'Missing rebuilt class '+name)
    dirty={str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(not any('/Game/GuLiStrike/Data/'+n in dirty for n in TABLES),'Preserve unsaved table work')
    scope={'__name__':'burning_card_import','GULI_TABLE_FILTER':set(TABLES),
           'GULI_IMPORT_REPORT':str(OUT/'table_import.json'),
           'GULI_IMPORT_PROGRESS':str(OUT/'table_import.log'),
           'GULI_SKIP_MECH_POST_IMPORT':True,'GULI_SKIP_GROUND_FIRE_POST_IMPORT':True}
    source=ROOT/'Scripts/import_data_to_engine.py'
    exec(compile(source.read_text(encoding='utf8'),str(source),'exec'),scope)
    result=json.loads((OUT/'table_import.json').read_text(encoding='utf8'))
    require(not result.get('errors'),'Table import errors: '+str(result.get('errors')))
    tables=[]
    for name in TABLES:
        table=require(unreal.load_asset('/Game/GuLiStrike/Data/'+name),'Missing formal table '+name)
        rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        expected=json.loads((ROOT/'data/Json'/f'{name}.json').read_text(encoding='utf-8-sig'))
        require({r['Name'] for r in rows}=={r['Name'] for r in expected},'Row identity mismatch '+name)
        require(unreal.EditorAssetLibrary.save_loaded_asset(table,False),'Save '+name)
        tables.append({'asset':table.get_path_name(),'rows':rows})
    strings=require(unreal.load_asset('/Game/GuLiStrike/Data/ST_GuLiStrikeGameTexts'),'Missing StringTable')
    texts=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeGameTexts_Texts')
    require(unreal.GuLiRogueCardPresentationLibrary.rebuild_game_text_string_table(strings,texts),'Rebuild public texts')
    require(unreal.EditorAssetLibrary.save_loaded_asset(strings,False),'Save public texts')
    keys=['Card.GroundMech.BurningBullet.Title','Card.GroundMech.BurningBullet.Description',
          'Card.GroundMech.BurningBurst.Title','Card.GroundMech.BurningBurst.Description',
          'Buff.Burning.Description','Buff.BurningBurst.Description']
    bound={}
    for key in keys:
        value=unreal.TextLibrary.text_from_string_table(unreal.Name(strings.get_path_name()),key)
        _,actual=unreal.TextLibrary.string_table_id_and_key_from_text(value)
        require(actual==key and unreal.TextLibrary.text_is_from_string_table(value),'Public text binding '+key)
        bound[key]=str(value)
    return {'applied':True,'tables':tables,'texts':bound,'PIE':False,
            'assistant_visual_test':False,'visual_acceptance':'pending_user'}

try:
    report=main()
except Exception:
    report={'applied':False,'error':traceback.format_exc()}
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'asset_install.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'applied':report['applied'],'error':report.get('error'),'report':str(OUT/'asset_install.json')},ensure_ascii=False))
