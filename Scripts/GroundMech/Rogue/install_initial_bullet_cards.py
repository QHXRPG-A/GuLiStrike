"""Targeted in-place import after native reload; preserve unrelated editor work."""
import json
import math
import re
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'Artifacts/GroundAmmo_20261108'
TABLES=['DT_GuLiStrikeRogueCards_Cards','DT_GuLiStrikeRogueCards_Effects',
        'DT_GuLiStrikeRogueCards_GroundWeaponEffects','DT_GuLiStrikeCardArtwork_Templates',
        'DT_GuLiStrikeCardArtwork_Artworks','DT_GuLiStrikeVfx_Effects',
        'DT_GuLiStrikeMech_Skills','DT_GuLiStrikeGameTexts_Texts']

def require(v,message):
    if not v:raise RuntimeError(message)
    return v

def normalized(v):
    if v=='None':return ''
    if isinstance(v,str) and "'" in v and v.endswith("'"):return v.split("'",1)[1][:-1]
    return v

def main():
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),'Preserve active play')
    # Fail before any import when a stale module would silently ignore new fields.
    unreal.GuLiStrikeMechSkillsRow().get_editor_property('initial_bullet_vfx_id')
    unreal.GuLiStrikeRogueCardsGroundWeaponEffectsRow().get_editor_property('explosive_intervals')
    require(json.loads((OUT/'asset_authoring.json').read_text(encoding='utf8'))['success'],'Complete and save project VFX first')
    require(unreal.load_asset('/Game/GuLiStrike/Cards/Shared/Placeholder/MI_CardBlank'),'Valid blank face')
    dirty={p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    require(not any('/Game/GuLiStrike/Data/'+n in dirty for n in TABLES),'Preserve unsaved DataTable edits')
    metadata=json.loads((ROOT/'data/Json/manifest.json').read_text(encoding='utf8'))['tables']
    tools=unreal.AssetToolsHelpers.get_asset_tools()
    rows_by_name={};saved=[]
    for name in TABLES:
        source=(ROOT/'data/Json'/f'{name}.json').read_text(encoding='utf-8-sig')
        struct=require(unreal.find_object(None,metadata[name]['struct']),'Loaded row schema '+name)
        path='/Game/GuLiStrike/Data/'+name;table=unreal.load_asset(path)
        if table is None:
            factory=unreal.DataTableFactory();factory.set_editor_property('struct',struct)
            table=require(tools.create_asset(name,'/Game/GuLiStrike/Data',unreal.DataTable,factory),'Create '+name)
        require(table.get_editor_property('row_struct')==struct,'Preserve row type '+name)
        require(unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table,source),'Import '+name)
        require(unreal.EditorAssetLibrary.save_loaded_asset(table,False),'Save '+name)
        actual=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        rows_by_name[name]=actual;by_key={r['Name']:r for r in actual}
        expected=json.loads(source)
        require(set(by_key)=={r['Name'] for r in expected},'Saved row identities '+name)
        for row in expected:
            for key,value in row.items():
                got=normalized(by_key[row['Name']].get(key))
                if isinstance(value,dict) and isinstance(got,str) and got.startswith('('):
                    parts=dict(re.findall(r'(\w+)=([^,)]+)',got))
                    require(set(parts)==set(value) and all(math.isclose(float(parts[k]),v,rel_tol=1e-6,abs_tol=1e-5) for k,v in value.items()),'Saved struct '+name+'.'+key)
                elif isinstance(value,str) and value.startswith('('):
                    expected_parts=dict(re.findall(r'(\w+)=([^,)]+)',value));actual_parts=dict(re.findall(r'(\w+)=([^,)]+)',str(got)))
                    require(set(expected_parts)==set(actual_parts) and all(math.isclose(float(actual_parts[k]),float(v),rel_tol=1e-6,abs_tol=1e-5) for k,v in expected_parts.items()),'Saved struct '+name+'.'+key)
                elif isinstance(value,float):require(isinstance(got,(int,float)) and math.isclose(got,value,rel_tol=1e-6,abs_tol=1e-5),'Saved number '+name+'.'+key)
                else:require(got==normalized(value),'Saved field '+name+'.'+row['Name']+'.'+key+': '+repr(got))
        saved.append({'asset':path,'rows':len(actual),'saved':True,'readback':True})
    for card in rows_by_name['DT_GuLiStrikeRogueCards_Cards']:
        if card['Id'] in ('09.01','10.01','11.01'):
            require(unreal.load_asset(normalized(card['FrontMaterial'])),'Card material '+card['Id'])
            require(card['IconId']==0 and card['Type']==3,'Blank personal Ground card')
    for row in rows_by_name['DT_GuLiStrikeVfx_Effects']:
        if row['Id'] in (62,63,64):require(isinstance(unreal.load_asset(normalized(row['ResourcePath'])),unreal.NiagaraSystem),'Loaded VFX '+str(row['Id']))
    texts=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeGameTexts_Texts')
    strings=require(unreal.load_asset('/Game/GuLiStrike/Data/ST_GuLiStrikeGameTexts'),'Public StringTable')
    require(unreal.GuLiRogueCardPresentationLibrary.rebuild_game_text_string_table(strings,texts),'Rebuild StringTable')
    require(unreal.EditorAssetLibrary.save_loaded_asset(strings,False),'Save StringTable')
    bound={}
    for name in ('FireRate','EnhancedBullet','ExplosivePackage'):
        for suffix in ('Title','Description'):
            key='Card.GroundMech.'+name+'.'+suffix
            value=unreal.TextLibrary.text_from_string_table(unreal.Name(strings.get_path_name()),key)
            _,actual=unreal.TextLibrary.string_table_id_and_key_from_text(value)
            require(actual==key and unreal.TextLibrary.text_is_from_string_table(value),'Text binding '+key)
            require('\n' not in str(value),'Single-line text '+key);bound[key]=str(value)
    return {'success':True,'tables':saved,'rows':rows_by_name,'texts':bound,'PIE':False,'assistant_visual_test':False}

try:report=main()
except Exception:report={'success':False,'error':traceback.format_exc()}
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'table_import.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'error':report.get('error'),'report':str(OUT/'table_import.json')},ensure_ascii=False))
