"""Delete only unreferenced old project paint superseded by the authorized formal import."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'Artifacts/ModelRegistryRuntime20261008'
promotion = json.loads((ROOT / 'ArtSource/ModelInterface_B_20261008/formal-promotion.json').read_text(encoding='utf8'))
baseline = json.loads((ROOT / 'Data/Models/migration-baseline-20261008.json').read_text(encoding='utf8'))
replaced = {r['source'] for r in promotion['replacements']}
sources = {s['material'] for m in baseline['models'] if m['path'] in replaced for s in m['slots'] if s.get('material')}
protected = ('outline', 'contour', 'glass', 'display', 'vat', 'animation')
report = {'authorization': promotion['authorization'], 'deleted': [], 'retained': [], 'errors': []}
parents = set()
for path in sorted(sources):
    if not path.startswith('/Game/GuLiStrike/') or any(s in path.rsplit('/', 1)[-1].lower() for s in protected):
        report['retained'].append({'path': path, 'reason': 'shared/protected original role'})
        continue
    material = unreal.load_object(None, path)
    if not material: continue
    package = path.split('.')[0]
    refs = [str(p) for p in unreal.EditorAssetLibrary.find_package_referencers_for_asset(package, False) if str(p) != package]
    if refs:
        report['retained'].append({'path': path, 'reason': 'still referenced', 'referencers': refs})
        continue
    if isinstance(material, unreal.MaterialInstanceConstant):
        parent = material.get_editor_property('parent')
        if parent and parent.get_path_name().startswith('/Game/GuLiStrike/'): parents.add(parent.get_path_name())
    elif not isinstance(material, unreal.Material):
        report['errors'].append('Unexpected old resource type: ' + path); continue
    if not unreal.EditorAssetLibrary.delete_asset(package):
        report['errors'].append('Delete failed: ' + path); continue
    report['deleted'].append(path)
for path in sorted(parents):
    if '/Models/TeamColor_v1/' in path or any(s in path.rsplit('/', 1)[-1].lower() for s in protected): continue
    refs = [str(p) for p in unreal.EditorAssetLibrary.find_package_referencers_for_asset(path.split('.')[0], False) if str(p) != path.split('.')[0]]
    if refs:
        report['retained'].append({'path': path, 'reason': 'original parent still referenced', 'referencers': refs}); continue
    if unreal.EditorAssetLibrary.delete_asset(path.split('.')[0]): report['deleted'].append(path)
    else: report['errors'].append('Parent delete failed: ' + path)
for name in ('MF_GuLiTeamRegion', 'MF_GuLiTeamRegion_v2'):
    path = '/Game/GuLiStrike/Models/TeamColor_v1/Shared/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path) and not unreal.EditorAssetLibrary.find_package_referencers_for_asset(path, False):
        if unreal.EditorAssetLibrary.delete_asset(path): report['deleted'].append(path)
        else: report['errors'].append('Unused function delete failed: ' + path)
for path in report['deleted']:
    if unreal.EditorAssetLibrary.does_asset_exist(path.split('.')[0]): report['errors'].append('Deleted resource still exists: ' + path)
report['success'] = not report['errors']
(OUT / 'retired-old-paint-readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': report['success'], 'deleted': len(report['deleted']), 'retained_shared': len(report['retained']), 'errors': report['errors']}))
