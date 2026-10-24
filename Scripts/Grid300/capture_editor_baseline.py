"""Read-only structured baseline for the 300 cm migration; never saves UE packages."""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'ArtSource/Navigation/Grid300/Baseline/editor.json'

def encode(v, depth=0):
    if v is None or isinstance(v, (bool, int, float, str)):
        return v
    if isinstance(v, unreal.Object):
        return {'path': v.get_path_name(), 'class': v.get_class().get_name()}
    if isinstance(v, unreal.Vector): return list(v.to_tuple())
    if isinstance(v, unreal.Vector2D): return [v.x, v.y]
    if isinstance(v, unreal.Rotator): return [v.pitch, v.yaw, v.roll]
    if isinstance(v, unreal.Transform):
        return {'location': encode(v.translation), 'rotation': encode(v.rotation.rotator()), 'scale': encode(v.scale3d)}
    if isinstance(v, (unreal.Array, list, tuple)): return [encode(x, depth + 1) for x in v]
    if hasattr(v, 'get_editor_property') and depth < 8: return properties(v, depth + 1)
    return str(v)

def properties(obj, depth=0):
    result = {}
    for name in dir(obj):
        if name.startswith('_'): continue
        try: result[name] = encode(obj.get_editor_property(name), depth)
        except Exception: pass
    return result

def main():
    if OUT.exists():
        return {'success': True, 'already_captured': True, 'path': str(OUT)}
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert not levels.is_in_play_in_editor(), 'Stop capture: PIE is active.'
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name() == '/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    rows = []
    for a in actors:
        cls = a.get_class().get_name()
        row = {'path': a.get_path_name(), 'name': a.get_name(), 'label': a.get_actor_label(),
               'class': cls, 'transform': encode(a.get_actor_transform()),
               'bounds': encode(a.get_actor_bounds(False)), 'tags': encode(a.tags)}
        if any(s in cls for s in ('Landscape', 'GuLiMap', 'Deployment', 'NavMesh', 'Stronghold', 'Resource')):
            row['properties'] = properties(a)
        if isinstance(a, unreal.Landscape):
            row['components'] = [properties(c) for c in a.get_components_by_class(unreal.LandscapeComponent)]
        rows.append(row)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = {}
    for item in registry.get_assets_by_path('/Game/GuLiStrike', True):
        if str(item.asset_class_path.asset_name) in ('GuLiResourceMapDefinition', 'GuLiResourceEconomyConfig'):
            assets[str(item.package_name)] = properties(item.get_asset())
    classes = {}
    for name in ('GuLiMiningVehiclePawn', 'GuLiConstructionVehiclePawn', 'GuLiResourceFactoryActor'):
        cls = getattr(unreal, name, None)
        if not cls: continue
        cdo = unreal.get_default_object(cls)
        classes[name] = {'properties': properties(cdo), 'components': {
            c.get_name(): properties(c) for c in cdo.get_components_by_class(unreal.SceneComponent)}}
    authoring = unreal.get_editor_subsystem(unreal.GuLiMapAuthoringSubsystem)
    snapshot = authoring.get_snapshot()
    density = authoring.get_density_snapshot()
    report = {'schema': 'guli-grid300/editor-baseline-v1', 'map': world.get_path_name(),
              'actors': rows, 'assets': assets, 'classes': classes,
              'authoring': properties(snapshot), 'density': properties(density),
              'dirty_maps': [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
              'dirty_assets': [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open('x', encoding='utf-8') as stream: json.dump(report, stream, ensure_ascii=False, indent=2)
    return {'success': True, 'path': str(OUT), 'actors': len(rows), 'assets': list(assets),
            'dirty_maps': report['dirty_maps'], 'dirty_assets': report['dirty_assets']}

unreal.MCPythonHelper.submit_result(json.dumps(main(), ensure_ascii=False))
