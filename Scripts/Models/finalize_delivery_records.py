"""Refresh the authorized delivery evidence; does not mutate UE assets or run tests."""
import ast
import hashlib
import json
import subprocess
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / 'Artifacts/ModelRegistryRuntime20261008'
stamp = datetime.now().astimezone().isoformat()


def read(relative):
    return json.loads((ROOT / relative).read_text(encoding='utf-8-sig'))


def write(relative, value):
    path = ROOT / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf8')


auth = read('ArtSource/ModelInterface_B_20261008/formal-import-authorization.json')
source_hash = hashlib.sha256(Path(auth['source']).read_bytes()).hexdigest()
if source_hash != auth['source_sha256']:
    raise RuntimeError('Original B source hash changed')
engine_id = json.loads(Path('D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealEditor.modules').read_text())['BuildId']
project_id = read('Binaries/Win64/UnrealEditor.modules')['BuildId']
if engine_id != project_id:
    raise RuntimeError('Engine/project native BuildId mismatch')
build = read('Data/Models/native-build.json')
build.update(engine_build_id=engine_id, project_build_id=project_id,
             latest_log='Artifacts/ModelRegistryRuntime20261008/NativeBuildBoundsFinal.log',
             final_duration_seconds=20.04, final_refresh=stamp,
             latest_guards='EditorOnly presenter exclusion, local selection readback, CPD recovery, scaled display bounds cache repair')
write('Data/Models/native-build.json', build)
manifest = read('Data/Models/migration-manifest.json')
manifest.update(native_compile=True, ue_import=True, formal_asset_import=True,
                model_descriptions=84, final_refresh=stamp,
                deferred_region_masks=[1005, 2004, 2007])
write('Data/Models/migration-manifest.json', manifest)
retired = read('Artifacts/ModelRegistryRuntime20261008/retired-old-paint-readback.json')
if retired['errors'] or not retired['success']:
    raise RuntimeError('Old paint cleanup did not pass')
promotion = read('ArtSource/ModelInterface_B_20261008/formal-promotion.json')
promotion.update(tables_imported=True, additional_old_paint_cleanup={
    'report': 'Artifacts/ModelRegistryRuntime20261008/retired-old-paint-readback.json',
    'deleted_count': len(retired['deleted']), 'success': True}, final_refresh=stamp)
write('ArtSource/ModelInterface_B_20261008/formal-promotion.json', promotion)
auth.update(technical_validation='partial_runtime_validated',
            latest_revision='formal dark surface and ore readable shadow fill',
            user_final_visual_acceptance='pending', final_refresh=stamp)
write('ArtSource/ModelInterface_B_20261008/formal-import-authorization.json', auth)

review = read('Data/Models/static-source-review.json')
paths = set(review['python_syntax_paths'])
paths.update(p.relative_to(ROOT).as_posix() for p in (ROOT / 'Scripts/Models').glob('*.py'))
errors = []
for relative in sorted(paths):
    try:
        ast.parse((ROOT / relative).read_text(encoding='utf-8-sig'), filename=relative)
    except Exception as exc:
        errors.append({'file': relative, 'error': str(exc)})
diff = subprocess.run(['git', 'diff', '--check', '--', 'Source', 'Scripts', 'Tools/DataPipeline',
                       'Progress', 'Data/Models', '.agents/skills/guli-model-production'],
                      cwd=ROOT, capture_output=True, text=True, encoding='utf8', errors='replace')
if diff.returncode:
    errors.append({'diff_whitespace': diff.stdout + diff.stderr})
review.update(python_syntax_files=len(paths), python_syntax_paths=sorted(paths), errors=errors,
              diff_whitespace='passed' if not diff.returncode else 'failed', native_compile=True,
              new_datatable_import=True, runtime_verified='partial; see session-validation-summary.json',
              cplusplus_review='Latest source built successfully using UE5.7 source engine; not a stability/performance pass',
              excel_sha256=hashlib.sha256((ROOT / 'Data/Excel/GuLiStrikeModels.xlsx').read_bytes()).hexdigest(),
              B_source_sha256=source_hash, final_refresh=stamp,
              editor_validation='84 resources / 2782 ordinary parameters / 68 expanded CPD bindings / 0 errors / 3 deferred masks')
write('Data/Models/static-source-review.json', review)

evidence = {
    'datatable_import': 'Data/Models/import-live-20261008.json',
    'binding_migration': 'Data/Models/native-binding-migration.json',
    'editor_catalog': 'Data/Models/editor-static-validation.json',
    'parameter_roundtrip': 'Artifacts/ModelRegistryRuntime20261008/runtime-parameter-roundtrip.json',
    'cpd_recovery': 'Artifacts/ModelRegistryRuntime20261008/final-cpd-recovery.json',
    'ownership': 'Artifacts/ModelRegistryRuntime20261008/ownership-color-readback.json',
    'environment_ring_roi': 'Artifacts/ModelRegistryRuntime20261008/scene-ui-color-readback.json',
    'selection': 'Artifacts/ModelRegistryRuntime20261008/final-selection-blue-readback.json',
    'deselection': 'Artifacts/ModelRegistryRuntime20261008/final-selection-clear.json',
    'aa_render_bounds': 'Artifacts/ModelRegistryRuntime20261008/aa-original-scale-restored.json',
    'ore_saved_materials': 'Artifacts/ModelRegistryRuntime20261008/ore-rock-lighter-revision.json',
    'ore_actual_comparison': 'Artifacts/ModelRegistryRuntime20261008/ore-rock-comparison.json',
    'saved_scenes': 'Data/Models/acceptance-scenes-readback.json',
    'latest_saved_scenes': 'Artifacts/ModelRegistryRuntime20261008/final-scenes-after-ore.json',
}
missing = [p for p in evidence.values() if not (ROOT / p).is_file()]
if missing:
    raise RuntimeError('Missing evidence files: ' + str(missing))
summary = {
    'updated': stamp, 'status': 'partial', 'authorizations': [
        '编译，然后模型表需要加个“描述”字段，说明这模型是干啥的，然后导入新 DataTable 并验证运行效果',
        auth['user_message'], '这些黑块太深了，再浅一些'],
    'native_build': {'status': 'passed', 'seconds': 20.04, 'build_id': project_id, 'log': build['latest_log']},
    'evidence': evidence, 'runtime_scope': {
        'commander_clients': 2, 'late_join_ground_client': True,
        'relative_team_colors': 'verified in each local client',
        'ownership_and_unknown_cases': 6, 'actual_parameter_roundtrip': 'verified',
        'external_cpd_reset_recovery': 'verified', 'selected_and_cleared_rpc_ack': 'verified',
        'scene_ui': '6 profiles, opaque enemy ring ROI RGB(255,56,48); not all UI pixels',
        'ring_width': '20 cm source contract; no physical screen measurement',
        'latest_ore_crystal_fill': 'saved and actual editor rendering; full multi-client PIE not rerun'},
    'long_session': {'status': 'failed', 'reason': 'Windows available committed memory exhausted',
        'log': 'Artifacts/ModelRegistryRuntime20261008/EditorVerifiedFinal.log',
        'time_local': '2026-10-08 21:54:51 +08:00',
        'scope': 'server plus 3 clients and accumulated model preview/capture work',
        'recovery': 'Editor reopened; focused verification completed; resources and maps saved',
        'stability_or_performance_pass_claimed': False},
    'saved_map_entities': {'Mass': 12, 'Ground': 3, 'Ship': 17},
    'not_run': ['actual ship gameplay verification', 'browser local gallery interaction'],
    'pending': {'region_masks': [1005, 2004, 2007], 'user_visual_acceptance': True},
    'automated_tests_added_or_run': False, 'source_hash_unchanged': True}
write('Artifacts/ModelRegistryRuntime20261008/session-validation-summary.json', summary)

captures = read('Artifacts/ModelRegistryRuntime20261008/formal-render-captures.json')
ore = read('Artifacts/ModelRegistryRuntime20261008/ore-rock-comparison.json')
qa = {
    'updated': stamp, 'reviewer': 'assistant', 'user_visual_acceptance': 'pending',
    'source': 'actual UE captures, no image repainting',
    'models': sorted(set(r['model_id'] for r in captures['renders'])),
    'views': ['hero', 'front', 'left', 'back'], 'palettes': ['blue', 'red'],
    'model_capture_count': len(captures['renders']), 'ore_comparison_count': len(ore['files']),
    'checks': ['shield top three lamps use team colors', 'outpost colors are assigned per whole component',
               'air base top panels remain mutable', 'AA display bounds reflect unchanged 12x import settings',
               'fixed cream, sand and mechanical areas retain the approved palette',
               'blue/red ore shadow faces readable in same-camera comparison'],
    'geometry_reference': 'ArtSource/ModelInterface_B_20261008/blender-saved-readback.json',
    'source_B_sha256': source_hash, 'method': 'Visual read-through and separate geometry/data evidence; no automated image-difference test',
    'browser_interaction': 'not verified; local access restrictions not bypassed'}
write('ArtSource/LocalTeamColorUE_B_v2_20261008/visual_qa.json', qa)
print(json.dumps({'python_syntax_files': len(paths), 'errors': errors,
                  'source_hash_unchanged': True, 'build_id': project_id,
                  'retired_extra_paint': len(retired['deleted']), 'saved_maps': 3,
                  'model_views': len(captures['renders']), 'ore_views': len(ore['files'])}, ensure_ascii=False))
if errors:
    raise SystemExit(1)
