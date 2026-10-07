"""Finish the pre-existing ID6 portrait and construction label configuration."""
import contextlib
import io
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
ART = ROOT / 'ArtSource/CommanderLOD_20261005'
ready_path = ART / 'Reports/formal_prepared_BiZhiMao.json'
ready = json.loads(ready_path.read_text(encoding='utf8'))
switch = json.loads((ART / 'Reports/source_reference_switch.json').read_text(encoding='utf8'))
assert switch['state'] == 'native_references_verified' and ready['animation']['bones'] == 0
scope = dict(__name__='commander_lod_ui_text_import', GULI_TABLE_FILTER={'DT_GuLiStrikeGameTexts_Texts'})
with contextlib.redirect_stdout(io.StringIO()):
    exec(compile((ROOT / 'Scripts/import_data_to_engine.py').read_text(encoding='utf8'), 'import_data_to_engine.py', 'exec'), scope)
pipeline = json.loads((ROOT / 'Data/tmp_import_report.json').read_text(encoding='utf8'))
assert not pipeline['errors'] and len(pipeline['tables']) == 1 and pipeline['tables'][0]['imported'], pipeline
base = ready['formal_root']
path = base + '/UI/T_UI_BiZhiMao'
lib = unreal.EditorAssetLibrary
portrait = lib.load_asset(path)
owner = 'CommanderLOD.3Tier.v1.BiZhiMao'
if portrait:
    assert lib.get_metadata_tag(portrait, 'GuLi.Owner') == owner
else:
    task = unreal.AssetImportTask()
    for field, value in dict(filename=str(ART / 'BiZhiMao/Review/UE/LOD0_Hero.png'), destination_path=base+'/UI',
        destination_name='T_UI_BiZhiMao', automated=True, save=False).items():
        task.set_editor_property(field, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    portrait = lib.load_asset(path)
assert portrait
portrait.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
portrait.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
portrait.set_editor_property('srgb', True)
lib.set_metadata_tag(portrait, 'GuLi.Owner', owner)
lib.set_metadata_tag(portrait, 'GuLi.ApprovedVersion', 'CommanderLOD_3Tier_v1')
lib.set_metadata_tag(portrait, 'GuLi.ApprovedSHA256', ready['approved_content_sha256'])
assert lib.save_loaded_asset(portrait, False)
theme = lib.load_asset('/Game/Commander/UI/DA_CommanderUITheme')
portraits = dict(theme.get_editor_property('portraits'))
icons = dict(theme.get_editor_property('icons'))
snapshot = dict(portraits={str(k):v.get_path_name() if v else None for k,v in portraits.items()},
    icons={str(k):v.get_path_name() if v else None for k,v in icons.items()})
snapshot_path = ART / 'Reports/bizhimao_ui_before.json'
if not snapshot_path.exists():
    snapshot_path.write_text(json.dumps(snapshot, ensure_ascii=False, indent=2), encoding='utf8')
portraits[6] = portrait
icons[unreal.Name('Building.7')] = portrait
theme.set_editor_property('portraits', portraits)
theme.set_editor_property('icons', icons)
actual_portraits = dict(theme.get_editor_property('portraits'))
actual_icons = {str(k):v for k,v in theme.get_editor_property('icons').items()}
assert actual_portraits[6] == portrait and actual_icons['Building.7'] == portrait
assert all(actual_portraits[int(k)].get_path_name() == v for k,v in snapshot['portraits'].items() if k != '6' and v)
assert all(actual_icons[k].get_path_name() == v for k,v in snapshot['icons'].items() if k != 'Building.7' and v)
assert lib.save_loaded_asset(theme, False)
if not any(a['formal'] == portrait.get_path_name() for a in ready['assets']):
    ready['assets'].append(dict(source='BiZhiMao/Review/UE/LOD0_Hero.png', formal=portrait.get_path_name(), class_name='Texture2D', dependencies=[]))
ready_path.write_text(json.dumps(ready, ensure_ascii=False, indent=2), encoding='utf8')
report = dict(success=True, portrait=portrait.get_path_name(), theme=theme.get_path_name(), unit_type_id=6,
    building_icon='Building.7', other_entries_preserved=True, text_import=pipeline,
    gameplay='not_run', attack_events_installed=False)
(ART / 'Reports/bizhimao_ui_install.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True, portrait=portrait.get_path_name(), other_entries_preserved=True)))
