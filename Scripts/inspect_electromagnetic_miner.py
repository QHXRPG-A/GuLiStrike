"""Inspect vehicle and candidate mining VFX through the live editor API."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/outputs/electromagnetic-miner')
OUT.mkdir(parents=True, exist_ok=True)
BP = '/Game/MC_Vehicle_Constructor/Blueprints/Wheeled_Transporter_Lvl2'
svc = unreal.BlueprintService
report = {'source': BP, 'components': [], 'fx_candidates': [], 'mesh_candidates': []}
info = svc.get_blueprint_info(BP)
report['parent'] = info.parent_class
for c in svc.list_components(BP):
    props = svc.get_all_component_properties(BP, c.component_name, True)
    report['components'].append(dict(name=c.component_name, type=c.component_class,
        parent=c.attach_parent, root=c.is_root_component, inherited=c.is_inherited,
        properties={p.property_name: p.value for p in props if any(k in p.property_name.lower()
            for k in ['mesh','relative','material','socket','animation','animclass'])}))
reg = unreal.AssetRegistryHelpers.get_asset_registry()
for kind in ['NiagaraSystem', 'ParticleSystem', 'Material', 'MaterialInstanceConstant']:
    module = 'Niagara' if kind == 'NiagaraSystem' else 'Engine'
    for a in reg.get_assets_by_class(unreal.TopLevelAssetPath('/Script/'+module, kind), True):
        name = str(a.asset_name)
        if any(word in name.lower() for word in ['laser','beam','mining']):
            report['fx_candidates'].append(dict(path=str(a.package_name), type=kind))
for a in reg.get_assets_by_path('/Game/MC_Vehicle_Constructor', True):
    if str(a.asset_class_path.asset_name) in ['StaticMesh','SkeletalMesh']:
        report['mesh_candidates'].append(str(a.package_name))
report['world'] = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name()
report['open_editors'] = [dict(asset=x.asset_path, title=x.tab_title) for x in unreal.ScreenshotService.get_open_editor_tabs()] if False else []
report['geometry'] = [x for x in dir(unreal) if 'GeometryScript' in x and ('Primitive' in x or 'StaticMesh' in x or 'Asset' in x)]
(OUT/'inspection.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'components':len(report['components']), 'fx':len(report['fx_candidates']), 'geometry':report['geometry']}))
