"""Read-only supplemental baseline; no package saves or compilation."""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir())
exec((ROOT / 'Scripts/Scale020/capture_editor_baseline.py').read_text(encoding='utf-8').split('def main():')[0])
OUT = ROOT / 'TestResults/Scale020/fx-blueprint-baseline.json'

def capture():
    if OUT.exists():
        return {'already_captured': True}
    names = ['GuLiStrikeShip', 'GuLiStrikeProjectile', 'GuLiStrikeCharacter', 'GuLiStrikeNPC', 'GuLiStrikeAoEAttack', 'GuLiStrikePickup']
    classes = tuple(getattr(unreal, n) for n in names)
    bps = {}
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for data in registry.get_assets_by_path('/Game/GuLiStrike', True):
        if str(data.asset_class_path.asset_name) != 'Blueprint':
            continue
        bp = data.get_asset()
        cls = bp.generated_class()
        if not cls:
            continue
        cdo = unreal.get_default_object(cls)
        if not isinstance(cdo, classes):
            continue
        bps[str(data.package_name)] = {'kind': next(n for n in names if isinstance(cdo, getattr(unreal,n))),
            'properties': properties(cdo), 'components': {c.get_name(): {'class': c.get_class().get_name(),
            'properties': properties(c)} for c in cdo.get_components_by_class(unreal.ActorComponent)}}
    systems = {}
    ns, em, sp = unreal.NiagaraService, unreal.NiagaraEmitterService, unreal.NiagaraScratchPadService
    for path in ['/Game/GuLiStrike/FX/WingmanFlight/NS_WingmanFlightTrail',
                 '/Game/GuLiStrike/FX/CommanderWeapons/NS_WM01_MissileFlight',
                 '/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanGroundMissileFlight']:
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            systems[path] = {'missing': True}
            continue
        row = {'summary': str(ns.summarize(path)), 'parameters': [str(p) for p in ns.list_parameters(path)], 'emitters': {}}
        for emitter in ns.list_emitters(path):
            name = str(emitter.emitter_name)
            modules = {}
            for module in sp.list_scratch_modules(path, name):
                module = str(module)
                modules[module] = [{'id': str(n.node_id), 'code': sp.get_custom_hlsl_code(path, name, module, str(n.node_id)),
                    'pins': [str(p) for p in sp.get_node_pins(path,name,module,str(n.node_id))]}
                    for n in sp.list_nodes(path, name, module) if 'hlsl' in str(n.node_type).lower()]
            row['emitters'][name] = {'info': str(emitter), 'scratch': modules,
                'renderers': [str(r) for r in em.list_renderers(path,name)]}
        systems[path] = row
    OUT.write_text(json.dumps({'blueprints': bps, 'systems': systems},ensure_ascii=False,indent=2),encoding='utf-8')
    return {'blueprints': list(bps), 'systems': list(systems), 'path': str(OUT)}

unreal.MCPythonHelper.submit_result(json.dumps(capture()))
