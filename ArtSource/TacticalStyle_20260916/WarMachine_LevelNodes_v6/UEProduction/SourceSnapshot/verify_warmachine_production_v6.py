"""Narrow saved-asset readback for the approved WarMachine production handoff."""
import hashlib
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT/'ArtSource/TacticalStyle_20260916/WarMachine_LevelNodes_v6/UEProduction'
BASE = '/Game/Commander/Units/Tactical/Cel/WarMachine'
LIB = unreal.EditorAssetLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()
SUB = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
imported = json.loads((OUT/'import-manifest.json').read_text(encoding='utf-8'))
before = json.loads((OUT/'before-ue.json').read_text(encoding='utf-8'))
export = json.loads((OUT/'export-manifest.json').read_text(encoding='utf-8'))
assert imported['success']
sm = unreal.load_asset(BASE+'/Meshes/SM_WarMachine_Cel')
sk = unreal.load_asset(BASE+'/Meshes/SK_WarMachine_Cel')
assert sm and sk
checks = {'success': True, 'saved_packages': [], 'sections': [], 'sockets': {}, 'reference_preservation': {}}
for record in before['assets']:
    obj = unreal.load_asset(record['path'])
    assert obj
    pkg = record['path'].split('.')[0]
    after_refs = {str(x) for x in REG.get_referencers(pkg, unreal.AssetRegistryDependencyOptions())}
    assert set(record['referencers']).issubset(after_refs), pkg
    checks['reference_preservation'][pkg] = sorted(after_refs)
    if record['class'] == 'Material':
        textures = [x.get_path_name() for x in unreal.MaterialEditingLibrary.get_used_textures(obj)]
        assert sorted(textures) == sorted(record['textures'])
    if record['class'] in ('Texture2D', 'StaticMesh', 'SkeletalMesh'):
        source = Path(obj.get_editor_property('asset_import_data').extract_filenames()[0])
        assert source.parent.resolve() == OUT.resolve()
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        assert LIB.get_metadata_tag(obj, 'GuLi.ModelProduction.SourceSHA256') == digest
        checks['saved_packages'].append({'asset': obj.get_path_name(), 'source': str(source), 'source_sha256': digest})
for lod in range(SUB.get_lod_count(sm)):
    for section in range(sm.get_num_sections(lod)):
        slot = SUB.get_lod_material_slot(sm, lod, section)
        material = sm.static_materials[slot].material_interface
        assert material and material.get_path_name().startswith(BASE+'/Materials/')
        checks['sections'].append({'lod': lod, 'section': section, 'slot': slot, 'material': material.get_path_name()})
for name, source in {'FX_Muzzle_Basic_01': 'Muzzle_Cannon_L', 'FX_Missile_01': 'Muzzle_Missile_L', 'FX_Missile_02': 'Muzzle_Missile_R'}.items():
    socket = sm.find_socket(name)
    expected = [v*100 for v in export['sockets_m'][source]]
    assert socket and max(abs(a-b) for a,b in zip(socket.relative_location.to_tuple(), expected)) < .01
    assert abs(socket.relative_rotation.pitch-(45 if 'Missile' in name else 0)) < .01
    checks['sockets'][name] = {'location_cm': list(socket.relative_location.to_tuple()), 'pitch': socket.relative_rotation.pitch}
aim = sm.find_socket('FX_AimTarget')
assert aim
checks['sockets']['FX_AimTarget'] = {'location_cm': list(aim.relative_location.to_tuple())}
assert [str(s.material_slot_name) for s in sm.static_materials] == [s['name'] for s in next(a for a in before['assets'] if a['class']=='StaticMesh')['slots']]
assert SUB.get_num_uv_channels(sm, 0) == 3
assert list(SUB.get_lod_screen_sizes(sm)) == imported['lod_screens']
table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers')
row = next(r for r in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)) if r['Name']=='WM01')
assert row == imported['wm01_row']
assert not any(p.get_path_name().startswith(BASE) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
assert hashlib.sha256(Path(export['source_review']).read_bytes()).hexdigest() == export['source_sha256']
checks.update(dimensions_cm=list((sm.get_bounds().box_extent*2).to_tuple()),
              lod_triangles=[sm.get_num_triangles(i) for i in range(4)], uv_channels=3,
              original_material_slot_order_preserved=True, gameplay_row_unchanged=True,
              no_unsaved_production_packages=True, approved_source_unchanged=True,
              assistant_visual_review='not_performed_per_user_instruction', user_ue_visual_review='pending')
(OUT/'final-readback.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2), encoding='utf-8')
LIB.sync_browser_to_objects([sm.get_path_name()])
checks['asset_editor_opened'] = unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).open_editor_for_assets([sm])
(OUT/'final-readback.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(checks, ensure_ascii=False))
