"""Update only the approved WarMachine assets through the current UE editor.

Explicit legacy FBX importer; retain production paths, materials, skeleton and
gameplay data. Original package files are copied, never parsed, for rollback.
"""
import hashlib
import json
import shutil
import sys
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT/'ArtSource/TacticalStyle_20260916/WarMachine_LevelNodes_v6/UEProduction'
BASE = '/Game/Commander/Units/Tactical/Cel/WarMachine'
LIB = unreal.EditorAssetLibrary
SUB = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
REG = unreal.AssetRegistryHelpers.get_asset_registry()
WORLD = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
VAR = 'Interchange.FeatureFlags.Import.Enable'
OLD_VAR = unreal.SystemLibrary.get_console_variable_int_value(VAR)
BEFORE = json.loads((OUT/'before-ue.json').read_text(encoding='utf-8'))
EXPORT = json.loads((OUT/'export-manifest.json').read_text(encoding='utf-8'))
REPORT = {'success': False, 'version': 'WarMachine_LevelNodes_v6',
          'authorization': '重防号模型导出至UE成为正式资产',
          'saved_assets': [], 'engine': unreal.SystemLibrary.get_engine_version()}
sys.path.insert(0, str(ROOT/'Scripts'))
import import_tactical_handbuilt_models as legacy

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def save(obj):
    assert obj.get_path_name().startswith(BASE+'/')
    LIB.set_metadata_tag(obj, 'GuLi.ModelProduction.Revision', 'WarMachine_LevelNodes_v6')
    assert LIB.save_loaded_asset(obj, False), obj.get_path_name()
    if obj.get_path_name() not in REPORT['saved_assets']: REPORT['saved_assets'].append(obj.get_path_name())

def import_one(name, kind, folder, skeleton=None):
    source = OUT/name
    stem = source.stem
    path = BASE+'/'+folder+'/'+stem
    obj = unreal.load_asset(path)
    assert obj and LIB.get_metadata_tag(obj, 'GuLi.ModelProduction.Owner') == 'GuLi.CelPass.20260917', path
    task = unreal.AssetImportTask()
    for k, v in dict(filename=str(source), destination_path=BASE+'/'+folder,
                     destination_name=stem, automated=True, async_=False,
                     replace_existing=True, replace_existing_settings=True, save=False).items():
        task.set_editor_property(k, v)
    if kind is not None:
        ui = legacy.options(kind)
        ui.set_editor_property('reset_to_fbx_on_material_conflict', False)
        data = ui.get_editor_property('skeletal_mesh_import_data' if kind else 'static_mesh_import_data')
        data.set_editor_property('reorder_material_to_fbx_order', False)
        if skeleton: ui.set_editor_property('skeleton', skeleton)
        task.set_editor_property('options', ui)
        task.set_editor_property('factory', unreal.FbxFactory())
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    obj = unreal.load_asset(path)
    assert obj, path
    assert task.imported_object_paths, 'Import did not produce objects: '+path
    LIB.set_metadata_tag(obj, 'GuLi.ModelProduction.Owner', 'GuLi.CelPass.20260917')
    LIB.set_metadata_tag(obj, 'GuLi.ModelProduction.SourceSHA256', digest(source))
    LIB.set_metadata_tag(obj, 'GuLi.ModelProduction.SourceFile', str(source.relative_to(ROOT)))
    return obj

def table_json():
    table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers')
    return unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)

try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert EXPORT['success'] and EXPORT['protected_sources_unchanged']
    assert digest(Path(EXPORT['source_review'])) == EXPORT['source_sha256']
    for item in EXPORT['files']: assert digest(OUT/item['file']) == item['sha256'], item['file']
    assert not any(p.get_path_name().startswith(BASE) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    table_before = table_json()
    backup_manifest = OUT/'backup-before.json'
    if not backup_manifest.exists():
        copies = []
        for item in BEFORE['assets']:
            pkg = item['path'].split('.')[0]
            assert pkg.startswith(BASE+'/')
            relative = Path('Content')/pkg.removeprefix('/Game/')
            for suffix in ('.uasset', '.uexp', '.ubulk'):
                source = ROOT/relative.with_suffix(suffix)
                if source.is_file():
                    dest = OUT/'BackupBefore'/relative.with_suffix(suffix)
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    assert not dest.exists()
                    shutil.copy2(source, dest)
                    copies.append({'source': str(source), 'backup': str(dest), 'bytes': source.stat().st_size})
        backup_manifest.write_text(json.dumps({'files': copies, 'method': 'file copies only; no package contents parsed'}, indent=2), encoding='utf-8')
    REPORT['backup'] = str(backup_manifest)
    sm_before = next(x for x in BEFORE['assets'] if x['class'] == 'StaticMesh')
    sm = unreal.load_asset(sm_before['path'])
    old_setup = sm.get_editor_property('body_setup')
    old_collision = old_setup.get_editor_property('agg_geom')
    old_trace = old_setup.get_editor_property('collision_trace_flag')
    old_count = SUB.get_convex_collision_count(sm)
    old_simple = SUB.get_simple_collision_count(sm)
    sk_path = BASE+'/Meshes/SK_WarMachine_Cel'
    sk = unreal.load_asset(sk_path)
    skeleton = sk.get_editor_property('skeleton')
    old_physics = sk.get_editor_property('physics_asset')
    bones_before = [str(b.bone_name) for b in unreal.SkeletonService.list_bones(sk_path)]
    unreal.SystemLibrary.execute_console_command(WORLD, VAR+' 0')

    # Import both UV-compatible meshes before replacing their shared textures.
    sm = import_one('SM_WarMachine_Cel.fbx', False, 'Meshes')
    initial_bounds = list((sm.get_bounds().box_extent*2).to_tuple())
    build = SUB.get_lod_build_settings(sm, 0)
    for k, v in dict(use_full_precision_u_vs=True, recompute_normals=False,
                     recompute_tangents=False, build_scale3d=unreal.Vector(1,1,1)).items(): build.set_editor_property(k, v)
    SUB.set_lod_build_settings(sm, 0, build)
    sk = import_one('SK_WarMachine_Cel.fbx', True, 'Meshes', skeleton)
    assert sk.get_editor_property('skeleton') == skeleton
    assert sk.get_editor_property('physics_asset') == old_physics
    assert [str(b.bone_name) for b in unreal.SkeletonService.list_bones(sk_path)] == bones_before
    atlas = import_one('T_WarMachine_BaseColor.png', None, 'Textures')
    atlas.set_editor_property('srgb', True)
    mask = import_one('T_WarMachine_LineMask.png', None, 'Textures')
    mask.set_editor_property('srgb', False)
    mask.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
    save(atlas); save(mask)

    mats = {False: unreal.load_asset(BASE+'/Materials/M_WarMachine_Cel'),
            True: unreal.load_asset(BASE+'/Materials/M_WarMachine_Contour')}
    for i, slot in enumerate(sm.static_materials): sm.set_material(i, mats['Contour' in str(slot.material_slot_name)])
    slots = list(sk.materials)
    for slot in slots: slot.set_editor_property('material_interface', mats['Contour' in str(slot.material_slot_name)])
    sk.set_editor_property('materials', slots)
    assert len(sm.static_materials) == len(slots) == 2

    n = sm.get_num_triangles(0)
    screens = sm_before['lod_screens']
    reduction = unreal.StaticMeshReductionOptions(auto_compute_lod_screen_size=False,
        reduction_settings=[unreal.StaticMeshReductionSettings(percent_triangles=p, screen_size=s)
                            for p, s in zip((1., .25, 300/n, 70/n), screens)])
    assert SUB.set_lods(sm, reduction) == 4
    for lod in range(SUB.get_lod_count(sm)):
        for section in range(sm.get_num_sections(lod)):
            slot_index = SUB.get_lod_material_slot(sm, lod, section)
            contour = 'Contour' in str(sm.static_materials[slot_index].material_slot_name)
            SUB.enable_section_cast_shadow(sm, not contour, lod, section)
            if contour: SUB.enable_section_collision(sm, False, lod, section)
    setup = sm.get_editor_property('body_setup')
    setup.set_editor_property('agg_geom', old_collision)
    setup.set_editor_property('collision_trace_flag', old_trace)
    assert SUB.get_convex_collision_count(sm) == old_count
    assert SUB.get_simple_collision_count(sm) == old_simple

    authored_sockets = EXPORT['sockets_m']
    names = {'FX_Muzzle_Basic_01': 'Muzzle_Cannon_L', 'FX_Missile_01': 'Muzzle_Missile_L', 'FX_Missile_02': 'Muzzle_Missile_R'}
    old_muzzle = next(x for x in sm_before['sockets'] if x['name'] == 'FX_Muzzle_Basic_01')['location'][2]
    lift_cm = authored_sockets['Muzzle_Cannon_L'][2]*100 - old_muzzle
    for previous in sm_before['sockets']:
        name = previous['name']
        socket = sm.find_socket(name)
        if not socket:
            socket = unreal.new_object(unreal.StaticMeshSocket, outer=sm)
            socket.set_editor_property('socket_name', name)
            sm.add_socket(socket)
        location = [v*100 for v in authored_sockets[names[name]]] if name in names else list(previous['location'])
        if name == 'FX_AimTarget': location[2] += lift_cm
        socket.set_editor_property('relative_location', unreal.Vector(*location))
        # Reimport retains rotations; repair from Unreal tuple ordering if absent.
        socket.set_editor_property('relative_rotation', unreal.Rotator(roll=previous['rotation'][0], pitch=previous['rotation'][1], yaw=previous['rotation'][2]))
        socket.set_editor_property('relative_scale', unreal.Vector(*previous['scale']))

    expected = [(EXPORT['bounds_with_contour_m'][1][i]-EXPORT['bounds_with_contour_m'][0][i])*100 for i in range(3)]
    actual = list((sk.get_imported_bounds().box_extent*2).to_tuple())
    assert max(abs(a-b) for a,b in zip(actual, expected)) < .2, (actual, expected)
    assert SUB.get_num_uv_channels(sm, 0) == 3
    assert table_before == table_json(), 'Gameplay table changed'
    for obj in (sm, sk, skeleton): save(obj)
    REPORT.update(success=True, dimensions_expected_cm=expected, skeletal_dimensions_cm=actual,
        static_bounds_initial_cm=initial_bounds, static_bounds_final_cm=list((sm.get_bounds().box_extent*2).to_tuple()),
        lod_triangles=[sm.get_num_triangles(i) for i in range(4)], lod_screens=list(SUB.get_lod_screen_sizes(sm)),
        uv_channels=SUB.get_num_uv_channels(sm,0), bones=bones_before, skeleton=skeleton.get_path_name(),
        collision_counts={'simple': old_simple, 'convex': old_count}, collision_preserved=True,
        socket_lift_cm=lift_cm, gameplay_table_unchanged=True,
        wm01_row=next(r for r in json.loads(table_before) if r['Name']=='WM01'),
        materials_reused=[m.get_path_name() for m in mats.values()], material_graphs_unchanged=True,
        assistant_visual_review='not_performed_per_user_instruction', pie='not_run', cpp_build='not_run')
except Exception:
    REPORT['error'] = traceback.format_exc()
    unreal.log_error(REPORT['error'])
finally:
    unreal.SystemLibrary.execute_console_command(WORLD, VAR+' '+str(OLD_VAR))
    REPORT['interchange_restored'] = unreal.SystemLibrary.get_console_variable_int_value(VAR) == OLD_VAR
    (OUT/'import-manifest.json').write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(REPORT, ensure_ascii=False))
