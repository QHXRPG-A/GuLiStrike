"""Bind the three registered artworks to their existing cards, preserving every other field."""
import copy
import json
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
OUT=ROOT/'Artifacts/CardArtworkPipeline/GroundMechFourCards_20261117'
PATH='/Game/GuLiStrike/Data/DT_GuLiStrikeRogueCards_Cards'


def main():
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert PATH not in {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    table=unreal.load_asset(PATH);assert table
    before=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    expected=copy.deepcopy(before)
    source={r['Id']:r for r in json.loads((ROOT/'data/Json/DT_GuLiStrikeRogueCards_Cards.json').read_text(encoding='utf-8'))}
    changed=[]
    for row in expected:
        if row['Id'] not in ('09.01','10.01','11.01'):continue
        target=source[row['Id']]
        assert target['Type']==3 and target['IconId']==5
        assert isinstance(unreal.load_asset(target['FrontMaterial']),unreal.MaterialInstanceConstant)
        row.update(FrontMaterial=target['FrontMaterial'],IconId=target['IconId'])
        changed.append({'id':row['Id'],'material':row['FrontMaterial'],'icon_id':row['IconId']})
    assert len(changed)==3
    if expected!=before:
        assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(expected,ensure_ascii=False),table.get_editor_property('row_struct'))
        assert unreal.EditorAssetLibrary.save_loaded_asset(table,False)
    actual=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    assert {r['Id']:r for r in actual}=={r['Id']:r for r in expected},'Saved gameplay rows differ'
    assert not any('/NoSurvivors/' in r['FrontMaterial'] for r in actual)
    return {'success':True,'table':PATH,'changed_bindings':changed,'other_fields_preserved':True,
        'no_survivors_bound':False,'saved_readback':True,'before':before,'after':actual,'pie_started':False}


if __name__=='__main__':
    try: report=main()
    except Exception: report={'success':False,'error':traceback.format_exc()}
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'gameplay-binding.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps({k:v for k,v in report.items() if k not in ('before','after')},ensure_ascii=False))
