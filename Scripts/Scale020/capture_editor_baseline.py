"""Read-only UE inspection; creates only the local evidence JSON, never saves packages."""
import json
from pathlib import Path
import unreal

OUT = Path(unreal.Paths.project_dir()) / 'TestResults/Scale020/editor-baseline.json'
CLASSES = {'GuLiGroundWarningStyle', 'GuLiPointSkillConfiguration', 'GuLiProjectileEffectDefinition',
           'GuLiSpellFieldDefinition', 'GuLiCombatEffectCatalog', 'GuLiResourceEconomyConfig',
           'GuLiResourceMapDefinition', 'GuLiWingmanFormationDefinition', 'GuLiWingmanWeaponDefinition',
           'GuLiShipAbilitySet', 'GuLiShipHangarDefinition'}

def value(v, depth=0):
    if v is None or isinstance(v, (bool, int, float, str)):
        return v
    if isinstance(v, unreal.Object):
        return {'object_path': v.get_path_name(), 'class': v.get_class().get_name()}
    if isinstance(v, unreal.Vector):
        return [v.x, v.y, v.z]
    if isinstance(v, unreal.Vector2D):
        return [v.x, v.y]
    if isinstance(v, unreal.Rotator):
        return [v.pitch, v.yaw, v.roll]
    if isinstance(v, unreal.Transform):
        return {'location': value(v.translation), 'rotation': value(v.rotation.rotator()), 'scale': value(v.scale3d)}
    if isinstance(v, (unreal.Array, list, tuple)):
        return [value(x, depth+1) for x in v]
    if hasattr(v, 'get_editor_property') and depth < 5:
        return properties(v, depth+1)
    return str(v)

def properties(obj, depth=0):
    result = {}
    for name in dir(obj):
        if name.startswith('_'):
            continue
        try:
            result[name] = value(obj.get_editor_property(name), depth)
        except Exception:
            pass
    return result

def main():
    if OUT.exists():
        return {'already_captured': True, 'path': str(OUT)}
    assets = {}
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for a in registry.get_assets_by_path('/Game/GuLiStrike', True):
        if str(a.asset_class_path.asset_name) in CLASSES:
            obj = a.get_asset()
            assets[str(a.package_name)] = {'class': str(a.asset_class_path.asset_name), 'properties': properties(obj)}
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actor_rows = [{'name': a.get_name(), 'label': a.get_actor_label(), 'class': a.get_class().get_path_name(),
                   'transform': value(a.get_actor_transform())} for a in actors]
    blueprints = {}
    for path in ['/Game/GuLiStrike/Ship/BP_GuLiStrikeShip', '/Game/GuLiStrike/Ship/BP_CombatAvatarFly01',
                 '/Game/GuLiStrike/Ship/BP_ShipProjectile',
                 '/Game/GuLiStrike/Vehicles/ElectromagneticMiner/BP_MiningVehicle_TransporterLvl2',
                 '/Game/GuLiStrike/Vehicles/ConstructionVehicle/BP_ConstructionVehicle',
                 '/Game/GuLiStrike/Buildings/ResourceProcessingFactory/Blueprints/BP_ResourceProcessingFactory']:
        cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
        if cls:
            cdo = unreal.get_default_object(cls)
            blueprints[path] = {'properties': properties(cdo), 'components': {
                c.get_name(): {'class': c.get_class().get_name(), 'properties': properties(c)}
                for c in cdo.get_components_by_class(unreal.SceneComponent)}}
    result = {'schema': 'guli-scale020/editor-baseline-v1', 'map': world.get_path_name(), 'assets': assets,
              'actors': actor_rows, 'blueprints': blueprints,
              'dirty_packages': [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    return {'path': str(OUT), 'assets': len(assets), 'actors': len(actor_rows), 'blueprints': len(blueprints), 'dirty_packages': result['dirty_packages']}

unreal.MCPythonHelper.submit_result(json.dumps(main()))
