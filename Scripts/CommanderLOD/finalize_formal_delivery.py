"""Consolidate the approved saved formal groups without altering frozen review evidence."""
import ast
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / 'ArtSource/CommanderLOD_20261005'
OUT = ART / 'Reports'
read = lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
approval = read(ART / 'approval_B.json')
manifest = read(ART / 'review_manifest.json')
assert approval['approval_B'] == 'approved' and approval['content_sha256'] == manifest['content_sha256']
payload = dict(manifest)
payload.pop('content_sha256')
assert hashlib.sha256(json.dumps(payload,ensure_ascii=False,indent=2).encode()).hexdigest() == manifest['content_sha256']
for unit in manifest['units']:
    for item in unit['artifacts']:
        file = ART / item['path']
        assert file.suffix.lower() not in {'.uasset','.umap','.uexp','.ubulk','.pak'}
        assert hashlib.sha256(file.read_bytes()).hexdigest() == item['sha256'],file
for item in manifest['evidence']:
    assert hashlib.sha256((ROOT / item['path']).read_bytes()).hexdigest() == item['sha256'],item['path']
frozen = read(OUT / 'blender_sources.json')
for item in frozen.values():
    assert hashlib.sha256(Path(item['path']).read_bytes()).hexdigest() == item['sha256']
switch = read(OUT / 'source_reference_switch.json')
assert switch['state'] == 'native_references_verified' and not switch['pending_groups']
build = read(OUT / 'native_build.json')
assert build['state'] == 'build_succeeded' and build['exit_code'] == 0
assert len({r['build_id'] for r in build['module_build_ids']}) == 1
scene = read(OUT / 'formal_review_scene.json')
assert scene['saved'] and scene['formal_review_groups'] == 18 and len(scene['formal_mesh_components']) == 60
assert read(OUT / 'bizhimao_ui_install.json')['success']
groups = []
for unit in approval['units']:
    name = unit['name']
    report_file = OUT / ('formal_prepared_' + name + '.json')
    report = read(report_file)
    verified = read(OUT / ('formal_readback_' + name + '.json'))
    assert verified['success'] and all(m['lod_count'] == 3 for m in verified['models'])
    report.update(switched=True, state='formal_switched_verified')
    report_file.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    groups.append(dict(id=unit['id'],name=name,formal_root=report['formal_root'],asset_count=len(report['assets']),
        model_asset=report['model_asset'],presentation_class=report.get('presentation_class',''),vat_definition=report['vat_definition'],
        readback='Reports/formal_readback_' + name + '.json'))
sources = []
for folder in ('Scripts/CommanderLOD','Scripts/BiZhiMao','Scripts/Pioneer'):
    for file in (ROOT / folder).glob('*.py'):
        ast.parse(file.read_text(encoding='utf8'),filename=str(file))
        sources.append(str(file.relative_to(ROOT)))
static = dict(success=True,python_syntax_files=sources,native_compile='passed',formal_groups=6,
    approval_B='approved',formal_switched=True,frozen_sources_unchanged=True,approved_artifacts_and_evidence_unchanged=True,
    pie='not_run',network='not_run',fps='not_run')
(OUT / 'formal_static_review.json').write_text(json.dumps(static,ensure_ascii=False,indent=2),encoding='utf8')
delivery = dict(success=True,version=approval['version'],content_sha256=approval['content_sha256'],art_revision='1.3',
    approval_B='approved',formal_switched=True,lod_count=3,groups=groups,total_assets=sum(g['asset_count'] for g in groups),
    construction=switch['construction'],native_compile='passed',engine=build['engine'],editor_reopened=True,
    review_map=scene['map'],review_groups=18,feature_fixture='BiZhiMaoQA_Sample',feature_gameplay_review='pending',
    source_gameplay_values_unchanged=True,original_groups_retained=True,frozen_sources_unchanged=True,
    budgets='Existing differences remain recorded; B is not budget/FPS acceptance.',pie='not_run',network='not_run',fps='not_run',attack_logic='not_implemented')
if (OUT / 'final_native_status.json').exists():
    final_native = read(OUT / 'final_native_status.json')
    assert final_native['success'] and final_native['map_saved'] and not final_native['dirty_content'] and not final_native['dirty_maps']
    delivery['final_native_readback'] = 'Reports/final_native_status.json'
(ART / 'formal_delivery.json').write_text(json.dumps(delivery,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(dict(success=True,version=approval['version'],formal_groups=6,total_assets=delivery['total_assets'],lod_count=3),ensure_ascii=False))
