"""Look for intact same-name assets using Asset Registry metadata only."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/TestResults/ExplosionMaterialRepair')
inventory = json.loads((OUT / 'inventory.json').read_text(encoding='utf-8'))
registry = unreal.AssetRegistryHelpers.get_asset_registry()
names = {a['path'].rsplit('/', 1)[1] for a in inventory['assets']}
report = {'same_name_assets': [], 'material_nodes': {}, 'function_graphs': {}}
for cls, module in [('Material', 'Engine'), ('MaterialFunction', 'Engine'), ('Texture2D', 'Engine'), ('MaterialInstanceConstant', 'Engine')]:
    for asset in registry.get_assets_by_class(unreal.TopLevelAssetPath('/Script/' + module, cls)):
        if str(asset.asset_name) in names and not str(asset.package_name).startswith('/Game/Assets/VFX/Explosions/'):
            report['same_name_assets'].append({'path': str(asset.package_name), 'class': cls})
for a in inventory['assets']:
    name = a['path'].rsplit('/', 1)[1]
    if a['class'] == 'Material':
        g = json.loads((OUT / (name + '_graph_before.json')).read_text(encoding='utf-8'))
        report['material_nodes'][name] = [e for e in g['expressions'] if any(n in e['class'] for n in ('Texture', 'SubUV', 'Function'))]
    if a['class'] == 'MaterialFunction':
        report['function_graphs'][name] = json.loads(unreal.MaterialNodeService.export_function_graph(a['path']))
(OUT / 'sources.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps({'same_name_assets': report['same_name_assets']}))
