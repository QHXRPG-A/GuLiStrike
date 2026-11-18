"""Targeted material creation, card binding and readback for approved artwork 14."""
import copy
import importlib
import json
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
OUT=ROOT/'Artifacts/CardArtworkPipeline/EnhancedExplosive_20261117'
CARD_TABLE='/Game/GuLiStrike/Data/DT_GuLiStrikeRogueCards_Cards'
ICON_TABLE='/Game/GuLiStrike/Data/DT_GuLiStrikeIcons_Icons'


def rows(path):
    table=unreal.load_asset(path)
    assert table,path
    return json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))


def main(action):
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    registration=json.loads((OUT/'registration.json').read_text(encoding='utf-8'))
    if action=='material':
        import author_registered_six_material as author
        importlib.reload(author)
        return author.main(destination=registration['parent_material'],normal_draw_order=registration['draw_order'])
    if action=='bind':
        assert CARD_TABLE not in {p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
        table=unreal.load_asset(CARD_TABLE)
        before=rows(CARD_TABLE);expected=copy.deepcopy(before)
        source={r['Id']:r for r in json.loads((ROOT/'data/Json/DT_GuLiStrikeRogueCards_Cards.json').read_text(encoding='utf-8'))}
        wanted=source['12.01']
        assert wanted['FrontMaterial'].split('.')[0]==registration['material']
        assert isinstance(unreal.load_asset(wanted['FrontMaterial']),unreal.MaterialInstanceConstant)
        assert wanted['IconId']>0
        changes=[]
        for row in expected:
            if row['Id']=='12.01':
                changes.append(dict(id=row['Id'],before={k:row[k] for k in ['FrontMaterial','IconId']},after={k:wanted[k] for k in ['FrontMaterial','IconId']}))
                row.update(FrontMaterial=wanted['FrontMaterial'],IconId=wanted['IconId'])
        assert len(changes)==1
        if expected!=before:
            assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(expected,ensure_ascii=False),table.get_editor_property('row_struct'))
            assert unreal.EditorAssetLibrary.save_loaded_asset(table,False)
        actual=rows(CARD_TABLE)
        assert {r['Id']:r for r in actual}=={r['Id']:r for r in expected}
        return dict(success=True,changes=changes,before=before,after=actual,saved_readback=True,other_fields_preserved=True)
    if action=='readback':
        import card_artwork_pipeline as pipeline
        importlib.reload(pipeline)
        actual=pipeline.verify([registration['artwork_id']])
        parent=unreal.load_asset(registration['parent_material'])
        assert json.loads(unreal.EditorAssetLibrary.get_metadata_tag(parent,'GuLi.CardArtwork.DrawOrder'))==registration['draw_order']
        cards=rows(CARD_TABLE);icons={r['Id']:r for r in rows(ICON_TABLE)}
        texts={r['TextId']:r['Content'] for r in json.loads((ROOT/'data/Json/DT_GuLiStrikeGameTexts_Texts.json').read_text(encoding='utf-8'))}
        icon_rows=[]
        for card in cards:
            if card['Type']!=3:continue
            icon=icons.get(card['IconId'])
            resource=icon['ResourcePath'] if icon else ''
            loaded=unreal.load_asset(resource) if resource else None
            icon_rows.append(dict(card_id=card['Id'],title=texts[card['TextIds'][0]],icon_id=card['IconId'],icon_name=icon['Name'] if icon else '',resource=resource,exists=bool(loaded),status='missing' if not loaded else 'shared_fire_placeholder' if card['IconId']==5 else 'dedicated'))
        world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
        director=next((a for a in actors if a.get_actor_label()=='CardArtwork_CatalogDirector'),None)
        _,catalog,_=pipeline.load_catalog()
        bound={r['FrontMaterial'] for r in cards}
        unbound=[dict(artwork_id=ident,title=r['Title'],icon_id=None,reason='Artwork registered; no gameplay card references this material') for ident,r in catalog['artworks'].items() if r['Name'].startswith('GroundMech_') and r['Material'] not in bound]
        return dict(success=True,artwork=actual,draw_order=registration['draw_order'],material_diagnostics=str(unreal.MaterialNodeService.get_material_diagnostics(registration['parent_material'])),icons=icon_rows,unbound_artworks=unbound,map=world.get_path_name(),review_ids=unreal.EditorAssetLibrary.get_metadata_tag(world,'GuLi.CardArtwork.ReviewIds'),director=director.get_path_name() if director else '',dirty_content=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],pie_started=False,assistant_visual_test=False)
    raise ValueError(action)


def execute(action):
    try:report=main(action)
    except Exception:report=dict(success=False,error=traceback.format_exc())
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/(action+'-report.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(dict(success=report.get('success'),action=action,error=report.get('error'),report=str(OUT/(action+'-report.json'))),ensure_ascii=False))
    return report


if __name__=='__main__':execute(globals().get('GULI_ENHANCED_EXPLOSIVE_ACTION','readback'))
