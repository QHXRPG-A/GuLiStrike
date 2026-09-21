"""Read-only pre-migration inventory. Run with Scripts/ue_exec.py before rebuilding native fields."""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'outputs/vfx-registry-20260921'
OUT.mkdir(parents=True, exist_ok=True)
entries = []
assets = []

def path(value):
    return value.get_path_name() if value else ''

def capture(obj, prop, scale=1.0, suffix=''):
    value = obj.get_editor_property(prop)
    if value:
        entries.append(dict(location=obj.get_path_name()+'.'+prop+suffix,
                            resource=path(value), scale=[scale]*3))

def variant(v, location):
    if v.get_editor_property('system'):
        entries.append(dict(location=location, resource=path(v.get_editor_property('system')),
                            scale=[float(v.get_editor_property('scale'))]*3))
    for i, layer in enumerate(v.get_editor_property('additional_layers')):
        entries.append(dict(location=location+'.additional_layers.'+str(i),
                            resource=path(layer.get_editor_property('system')),
                            scale=[float(layer.get_editor_property('scale'))]*3))

registry = unreal.AssetRegistryHelpers.get_asset_registry()
for root in ['/Game/GuLiStrike', '/Game/Commander', '/Game/Ship']:
    for item in registry.get_assets_by_path(root, recursive=True):
        cls = str(item.asset_class_path.asset_name)
        asset_path = str(item.package_name)+'.'+str(item.asset_name)
        if cls == 'Blueprint':
            assets.append({'path':asset_path, 'class':cls})
        if cls not in ['GuLiCombatEffectCatalog','GuLiProjectileEffectDefinition','GuLiSpellFieldDefinition','GuLiGroundWarningStyle']:
            continue
        obj = unreal.load_asset(asset_path)
        record = {'path':asset_path,'class':cls}
        if cls == 'GuLiCombatEffectCatalog':
            capture(obj,'gunfire_system')
            capture(obj,'wingman_laser_system')
            variant(obj.get_editor_property('machine_gun_impact'),asset_path+'.machine_gun_impact')
            record['machine_gun_impact'] = obj.get_editor_property('machine_gun_impact').export_text()
        elif cls == 'GuLiProjectileEffectDefinition':
            capture(obj,'flight_system',float(obj.get_editor_property('visual_scale')))
        elif cls == 'GuLiSpellFieldDefinition':
            capture(obj,'waiting_system')
            capture(obj,'active_loop_system')
            record['activation_variants'] = []
            for i,v in enumerate(obj.get_editor_property('activation_variants')):
                variant(v,asset_path+'.activation_variants.'+str(i))
                record['activation_variants'].append(v.export_text())
        else:
            capture(obj,'material')
        assets.append(record)

report = dict(entries=entries, assets=assets,
    dirty_content=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
    dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
    blueprint_services=[n for n in dir(unreal) if 'Blueprint' in n and 'Service' in n])
(OUT/'asset-baseline.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'entries':len(entries),'assets':len(assets),'dirty_content':report['dirty_content'],
                  'dirty_maps':report['dirty_maps'],'blueprint_services':report['blueprint_services']}))
