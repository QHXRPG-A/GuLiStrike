"""User-authorized live PIE readback; no persistent gameplay data is changed."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'Artifacts/ModelRegistryRuntime20261008'


def color(value):
    return list(value.to_tuple()) if value is not None else None


def run():
    catalog = json.loads((ROOT / 'Data/Json/DT_GuLiStrikeModels_Models.json').read_text(encoding='utf8'))
    by_path = {r['ResourcePath']: r for r in catalog}
    worlds = [w for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE' in w.get_path_name()]
    report = {'worlds': [], 'errors': [], 'source': 'actual running worlds and actual component parameter values'}
    for world in worlds:
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        snapshot = json.loads(unreal.GuLiTeleportQALibrary.snapshot(world))
        registry = next(s for s in unreal.ObjectIterator(unreal.GuLiModelRegistrySubsystem) if s.get_world() == world)
        identity = pc.player_state if pc else None
        entry = {'world': world.get_path_name(), 'net_mode': snapshot['net_mode'],
                 'team': str(identity.get_team()) if identity else None,
                 'role': str(identity.get_battle_role()) if identity else None,
                 'units': snapshot['units'], 'local_subsystems': [], 'mass_batches': [], 'models': []}
        for sub in unreal.ObjectIterator(unreal.LocalPlayerSubsystem):
            if sub.get_world() != world or not sub.get_class().get_name().startswith('GuLi'):
                continue
            detail = {'class': sub.get_class().get_name(), 'path': sub.get_path_name()}
            if detail['class'] == 'GuLiSceneUISubsystem':
                widget = next((u for u in unreal.ObjectIterator(unreal.GuLiSceneUIWidget) if u.get_owning_player() == pc), None)
                detail['widget'] = widget.get_path_name() if widget else None
                detail['visible'] = bool(widget.is_visible()) if widget else False
                detail['focusable'] = bool(widget.is_focusable) if widget else None
            entry['local_subsystems'].append(detail)
        for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GuLiCommanderPresentationActor):
            for comp in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
                mesh = comp.static_mesh
                row = by_path.get(mesh.get_path_name() if mesh else '')
                if not row:
                    continue
                mid = row['Id']
                batch = {'component': comp.get_name(), 'model_id': mid, 'instances': comp.get_instance_count(),
                         'resource': mesh.get_path_name(), 'materials': [comp.get_material(i).get_path_name() if comp.get_material(i) else None for i in range(comp.get_num_materials())],
                         'primary': color(registry.get_vector_parameter(comp, mid, 'Root', '*', 'TeamPrimary')),
                         'secondary': color(registry.get_vector_parameter(comp, mid, 'Root', '*', 'TeamSecondary')),
                         'enabled': registry.get_scalar_parameter(comp, mid, 'Root', '*', 'TeamEnabled'),
                         'lamp': registry.get_scalar_parameter(comp, mid, 'Root', '*', 'TeamLightStrength')}
                entry['mass_batches'].append(batch)
        for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
            if actor.get_class().get_name() not in ('GuLiBuildingActor', 'GuLiResourceFactoryActor', 'GuLiConstructionVehiclePawn', 'GuLiMiningVehiclePawn', 'GuLiGroundMechPawn'):
                continue
            meshes = []
            for comp in actor.get_components_by_class(unreal.MeshComponent):
                asset = comp.get_skeletal_mesh_asset() if isinstance(comp, unreal.SkeletalMeshComponent) else comp.static_mesh if isinstance(comp, unreal.StaticMeshComponent) else None
                if asset:
                    meshes.append({'component': comp.get_name(), 'resource': asset.get_path_name(),
                                   'materials': [comp.get_material(i).get_path_name() if comp.get_material(i) else None for i in range(comp.get_num_materials())]})
            entry['models'].append({'actor': actor.get_name(), 'class': actor.get_class().get_name(), 'meshes': meshes})
        report['worlds'].append(entry)
    report['success'] = len(worlds) >= 1 and not report['errors']
    (OUT / 'runtime-readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    def unique_batches(entry):
        found = {}
        for b in entry['mass_batches']:
            if '_Team_' not in b['component'] or not b['instances']:
                continue
            detail = {k: b[k] for k in ('component', 'model_id', 'instances', 'primary', 'secondary', 'enabled')}
            found[json.dumps(detail, sort_keys=True)] = detail
        return list(found.values())
    return {'success': report['success'], 'worlds': [{'world': w['world'], 'team': w['team'], 'role': w['role'],
                                                   'subsystems': w['local_subsystems'], 'batches': unique_batches(w),
                                                   'unit_count': len(w['units']), 'other_models': len(w['models'])} for w in report['worlds']]}


unreal.MCPythonHelper.submit_result(json.dumps(run()))
