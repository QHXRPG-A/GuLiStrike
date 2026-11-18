"""Apply exported icon rows in place and verify the five formal texture bindings."""
import hashlib
import json
import traceback
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'Artifacts/GroundRogueCardIcons_20261118/PipelineIntegration'
BASE=ROOT/'ArtSource/UI/GroundMech/RogueCardIcons_20261118_v1'
ICON_TABLE='/Game/GuLiStrike/Data/DT_GuLiStrikeIcons_Icons'
CARD_TABLE='/Game/GuLiStrike/Data/DT_GuLiStrikeRogueCards_Cards'


def load(path):
    return json.loads(path.read_text(encoding='utf-8'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rows(asset):
    return json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(asset))


def main():
    assert Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()==ROOT
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    dirty={p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    assert ICON_TABLE not in dirty
    baseline=load(OUT/'source-before.json')
    export=load(OUT/'export-verification.json')
    assert export['success']
    assert sha(ROOT/'data/Json/DT_GuLiStrikeIcons_Icons.json')==export['icons_json_sha256']
    assert sha(ROOT/'data/Json/DT_GuLiStrikeRogueCards_Cards.json')==export['cards_json_sha256']
    manifest=load(BASE/'manifest.json')
    icon_source=load(ROOT/'data/Json/DT_GuLiStrikeIcons_Icons.json')
    card_source=load(ROOT/'data/Json/DT_GuLiStrikeRogueCards_Cards.json')
    icon_table=unreal.load_asset(ICON_TABLE)
    card_table=unreal.load_asset(CARD_TABLE)
    before_icons,before_cards=rows(icon_table),rows(card_table)
    expected={r['Id']:r for r in icon_source}
    assert {r['Id'] for r in before_icons}==set(expected)
    for row in before_icons:
        wanted=expected[row['Id']]
        assert all(row[k]==wanted[k] for k in row if row['Id']!=11 or k!='Note')
    card_json_by_id={row['Id']:row for row in card_source}
    for row in before_cards:
        source=card_json_by_id[row['Id']]
        for key in ('Name','Type','IconId','FrontMaterial','ImplementationClass','TextIds','EffectIds'):
            assert row[key]==source[key], (row['Id'],key)
    settings_class=unreal.load_class(None,'/Script/GuLiStrike.GuLiRogueCardSettings')
    assert settings_class
    settings=unreal.get_default_object(settings_class)
    configured_cards=settings.get_editor_property('cards')
    assert configured_cards.get_path_name()==card_table.get_path_name()
    textures=[]
    for entry in manifest['icons']:
        row=expected[entry['id']]
        assert row['ResourcePath']==entry['resource_path']
        texture=unreal.load_asset(row['ResourcePath'])
        assert isinstance(texture,unreal.Texture2D)
        sources=list(texture.get_editor_property('asset_import_data').extract_filenames())
        assert len(sources)==1 and Path(sources[0]).resolve()==(ROOT/entry['source_png']).resolve()
        assert sha(Path(sources[0]))==entry['sha256']==baseline['icon_hashes'][str(entry['id'])]
        assert unreal.EditorAssetLibrary.get_metadata_tag(texture,'GroundIconSourceSHA256')==entry['sha256']
        assert unreal.EditorAssetLibrary.get_metadata_tag(texture,'GroundIconVersion')==entry['version']
        assert texture.get_editor_property('compression_settings')==unreal.TextureCompressionSettings.TC_EDITOR_ICON
        assert texture.get_editor_property('lod_group')==unreal.TextureGroup.TEXTUREGROUP_UI
        assert texture.get_editor_property('mip_gen_settings')==unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS
        assert texture.get_editor_property('srgb') and not texture.get_editor_property('compression_no_alpha')
        assert [texture.blueprint_get_size_x(),texture.blueprint_get_size_y()]==[256,256]
        textures.append(dict(id=entry['id'],label=entry['label'],path=texture.get_path_name(),
            source=entry['source_png'],sha256=entry['sha256'],version=entry['version'],size=[256,256],alpha_preserved=True))
    (OUT/'ue-before.json').write_text(json.dumps(dict(icons=before_icons,cards=before_cards),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(icon_table,
        json.dumps(icon_source,ensure_ascii=False),icon_table.get_editor_property('row_struct'))
    assert unreal.EditorAssetLibrary.save_loaded_asset(icon_table,False)
    after_icons=rows(unreal.load_asset(ICON_TABLE))
    assert {r['Id']:r for r in after_icons}==expected
    assert rows(card_table)==before_cards
    assert ICON_TABLE not in {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    ground=[dict(card_id=row['Id'],name=row['Name'],icon_id=row['IconId'],
                 texture=expected[row['IconId']]['ResourcePath']) for row in before_cards if row['Type']==3]
    assert len(ground)==7
    assert not any(row['IconId']==9 for row in before_cards)
    result=dict(success=True,source_workbook='data/Excel/GuLiStrikeIcons.xlsx',
        icon_table=icon_table.get_path_name(),runtime_cards_setting=configured_cards.get_path_name(),
        changed_table_fields=[dict(icon_id=11,field='Note',value=expected[11]['Note'])],
        textures=textures,ground_card_bindings=ground,registered_unbound_icon_ids=[9],
        card_rows_unchanged=True,source_pixels_unchanged=True,saved_readback=True,
        assistant_visual_test=False,pie_started=False,visual_acceptance='pending_user')
    manifest['data_pipeline'].update(status='exported_imported_saved',table_count=export['table_count'],
        icon_table=icon_table.get_path_name(),cards_table=card_table.get_path_name(),saved_readback=True)
    (BASE/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return result


try:
    result=main()
except Exception:
    result=dict(success=False,error=traceback.format_exc())
path=OUT/'install-report.json'
path.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=result['success'],error=result.get('error'),report=str(path))))
