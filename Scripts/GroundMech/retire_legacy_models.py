"""Retire explicitly audited old Ground assets; never delete an external referencer."""
import json
from pathlib import Path
import zipfile
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Artifacts/GroundMechLegacyCleanup_20261028'
CURRENT = '/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Meshes/SK_GroundMech_A4B2'
MAIN_MAP = '/Game/Maps/LVL_CommanderMassPrototype'
SHOWCASE = '/Game/GuLiStrike/Mechs/StyleShowcase/LVL_MechAsset_Showcase'
ROLLBACKS = ['/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Rollback/BP_FireReview_BeforeA4B2',
             '/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Rollback/BP_GroundMech_BeforeA4B2']
TABLES = ['DT_GuLiStrikeModels_Models', 'DT_GuLiStrikeModels_Parts', 'DT_GuLiStrikeModels_MaterialParameters']
ACCEPTANCE = '/Game/Development/HybridAcceptance/DT_AcceptanceModels'
RETIRED = {4002, 4003, 4004}

def proof():
    result = []
    for path in ['/Game/GuLiStrike/GroundMech/BP_GroundMech_Light',
                 '/Game/GuLiStrike/GroundMech/Review/BP_GroundMech_FireReview']:
        bp = unreal.load_asset(path)
        mesh = unreal.get_default_object(bp.generated_class()).get_editor_property('mesh')
        value = mesh.get_skeletal_mesh_asset()
        assert value and value.get_path_name().split('.')[0] == CURRENT
        result.append({'blueprint': path, 'mesh': value.get_path_name(), 'anim': str(mesh.get_editor_property('anim_class'))})
    return result

def package_file(package):
    assert package.startswith('/Game/')
    relative = Path('Content') / package[len('/Game/'):]
    for suffix in ('.uasset', '.umap'):
        file = ROOT / relative.with_suffix(suffix)
        assert file.resolve().is_relative_to(ROOT / 'Content')
        if file.is_file():
            return file
    raise RuntimeError('Missing package file: ' + package)

def save_asset(asset):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        raise RuntimeError('Could not save: ' + asset.get_path_name())

def main():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert not level.is_in_play_in_editor()
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    before = proof()
    preflight = json.loads((OUT / 'preflight.json').read_text(encoding='utf-8'))
    candidates = sorted({a['path'] for a in preflight['legacy']} | set(ROLLBACKS))
    assert CURRENT not in candidates
    for path in candidates:
        assert path.startswith('/Game/GuLiStrike/GroundMech/')
        assert '/MechaDestroyer_A4B2/' not in path or path in ROLLBACKS
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
    allowed = set(candidates) | {ACCEPTANCE, SHOWCASE, '/Game/GuLiStrike/Data/DT_GuLiStrikeModels_Models'}
    for path in candidates:
        unexpected = set(map(str, registry.get_referencers(unreal.Name(path), options))) - allowed
        if unexpected:
            raise RuntimeError(f'Unaudited referencers of {path}: {sorted(unexpected)}')

    archive = OUT / 'Retired_GroundModels_SourceRestoreOnly.zip'
    packages = candidates + [ACCEPTANCE, SHOWCASE] + ['/Game/GuLiStrike/Data/' + name for name in TABLES]
    if archive.exists():
        raise RuntimeError('Preserve the existing pre-change archive before rerunning this destructive step')
    # Opaque file backup outside Content; no binary asset interpretation or active asset route.
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED) as backup:
        for package in packages:
            file = package_file(package)
            backup.write(file, file.relative_to(ROOT).as_posix())
    report = {'backup': str(archive), 'before': before, 'candidates': candidates, 'tables': [], 'removed_showcase_actors': []}
    (OUT / 'deletion_plan.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')

    for name in TABLES:
        table = unreal.load_asset('/Game/GuLiStrike/Data/' + name)
        text = (ROOT / 'data/Json' / (name + '.json')).read_text(encoding='utf-8-sig')
        wanted = json.loads(text)
        assert not any(int(row.get('ModelId', row.get('Id', 0))) in RETIRED for row in wanted)
        assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, text)
        actual = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        assert {r['Name'] for r in actual} == {r['Name'] for r in wanted}
        save_asset(table)
        report['tables'].append({'asset': table.get_path_name(), 'rows': len(actual)})

    acceptance = unreal.load_asset(ACCEPTANCE)
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(acceptance))
    rows = [r for r in rows if int(r['Id']) not in RETIRED]
    for row in rows:
        if int(row['Id']) == 4001:
            row['ResourcePath'] = CURRENT + '.SK_GroundMech_A4B2'
            row['CandidateResourcePath'] = ''
            row['Note'] = 'Ground current runtime model; legacy split models retired'
    assert not any('Style_v9' in str(row) for row in rows)
    assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(acceptance, json.dumps(rows, ensure_ascii=False))
    save_asset(acceptance)

    assert unreal.EditorLoadingAndSavingUtils.load_map(SHOWCASE)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for actor in actors.get_all_level_actors():
        found = False
        for component in actor.get_components_by_class(unreal.SkeletalMeshComponent):
            mesh = component.get_skeletal_mesh_asset()
            found |= bool(mesh and mesh.get_path_name().split('.')[0] in candidates)
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            mesh = component.get_editor_property('static_mesh')
            found |= bool(mesh and mesh.get_path_name().split('.')[0] in candidates)
        if found:
            assert actor.get_class().get_name() in ('SkeletalMeshActor', 'StaticMeshActor')
            report['removed_showcase_actors'].append(actor.get_actor_label())
            assert actors.destroy_actor(actor)
    assert level.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.load_map(MAIN_MAP)

    modified_files = [str(package_file(p)) for p in [ACCEPTANCE, SHOWCASE] + ['/Game/GuLiStrike/Data/' + name for name in TABLES]]
    registry.scan_modified_asset_files(modified_files)
    for path in candidates:
        external = set(map(str, registry.get_referencers(unreal.Name(path), options))) - set(candidates)
        if external:
            raise RuntimeError(f'Referencers remain; nothing force-deleted: {path}: {sorted(external)}')
    objects = [unreal.load_asset(path) for path in candidates]
    assert all(objects)
    assert unreal.EditorAssetLibrary.delete_loaded_assets(objects)
    remaining = [path for path in candidates if unreal.EditorAssetLibrary.does_asset_exist(path)]
    assert not remaining, remaining
    report.update(deleted=len(candidates), remaining=remaining, after=proof(), pie_started=False, runtime_test=False)
    report['manifests'] = []
    for population in (200, 600):
        path = ROOT / f'Config/Hybrid/RuntimeManifest-{population}.json'
        result = json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population, str(path)))
        assert result['success'], result
        report['manifests'].append(result)
    report['dirty_maps'] = len(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    report['dirty_content'] = len(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    assert report['dirty_maps'] == 0 and report['dirty_content'] == 0
    (OUT / 'cleanup_result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    return {'deleted_assets': report['deleted'], 'mesh_assets': len([a for a in preflight['legacy'] if a['class'] in ('StaticMesh', 'SkeletalMesh')]),
            'removed_showcase_actors': report['removed_showcase_actors'], 'current_model': CURRENT, 'dirty_maps': report['dirty_maps'], 'dirty_content': report['dirty_content'], 'backup': str(archive)}

unreal.MCPythonHelper.submit_result(json.dumps(main(), ensure_ascii=False))
