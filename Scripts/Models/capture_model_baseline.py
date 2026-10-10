"""Read-only UE editor inventory. Run with Scripts/ue_exec.py; never starts a game."""
import json
from pathlib import Path
import unreal

ROOT = Path(r'D:/UE5.7/test1')
OUTPUT = ROOT / 'Data/Models/migration-baseline-20261008.json'
if OUTPUT.exists():
    raise RuntimeError('Published migration baseline is immutable; use a separate readback report')

def mesh_info(mesh):
    if not mesh:
        return None
    skeletal = isinstance(mesh, unreal.SkeletalMesh)
    slots = mesh.get_editor_property('materials' if skeletal else 'static_materials')
    return {'path': mesh.get_path_name(), 'type': 'SkeletalMesh' if skeletal else 'StaticMesh',
            'slots': [{'name': str(s.material_slot_name), 'material': s.material_interface.get_path_name() if s.material_interface else '',
                       'vectors': [str(n) for n in unreal.MaterialEditingLibrary.get_vector_parameter_names(s.material_interface)] if s.material_interface else [],
                       'scalars': [str(n) for n in unreal.MaterialEditingLibrary.get_scalar_parameter_names(s.material_interface)] if s.material_interface else []}
                      for s in slots]}

def component_info(c):
    if isinstance(c, unreal.StaticMeshComponent):
        m = c.get_editor_property('static_mesh')
    elif isinstance(c, unreal.SkeletalMeshComponent):
        m = c.get_skeletal_mesh_asset()
    else:
        return None
    return {'component': c.get_name(), 'mesh': mesh_info(m),
            'parent': c.get_attach_parent().get_name() if c.get_attach_parent() else '',
            'socket': str(c.get_attach_socket_name()),
            'location': str(c.get_editor_property('relative_location')),
            'rotation': str(c.get_editor_property('relative_rotation')),
            'scale': str(c.get_editor_property('relative_scale3d')),
            'overrides': [m.get_path_name() if m else '' for m in c.get_editor_property('override_materials')]}

report = {'models': [], 'ships': [], 'ground': [], 'assemblies': [], 'legacy_parts': [], 'errors': []}
seen = set()
def add_mesh(path):
    if path and path not in seen:
        seen.add(path)
        m = unreal.load_object(None, path)
        if m:
            if isinstance(m, (unreal.StaticMesh, unreal.SkeletalMesh)):
                report['models'].append(mesh_info(m))
            elif isinstance(m, unreal.Class):
                report['models'].append({'path': path, 'type': 'PresentationClass', 'slots': []})
        else:
            report['errors'].append('Missing resource: ' + path)

for table, fields in [('Commander_Soldiers', ['ModelAsset', 'PresentationClass']), ('Buildings_Buildings', ['Mesh'])]:
    for row in json.loads((ROOT / ('Data/Json/DT_GuLiStrike' + table + '.json')).read_text(encoding='utf-8-sig')):
        for field in fields:
            add_mesh(row.get(field, ''))
old = json.loads((ROOT / 'ArtSource/LocalTeamColorReview_20261008/source-readback.json').read_text(encoding='utf-8-sig'))
for model in old['models']:
    add_mesh(model['mesh'])

for path in ['/Game/GuLiStrike/Ship/BP_GuLiStrikeShip.BP_GuLiStrikeShip_C', '/Game/GuLiStrike/Ship/BP_CombatAvatarFly01.BP_CombatAvatarFly01_C']:
    cls = unreal.load_class(None, path)
    if not cls:
        report['errors'].append('Missing ship: ' + path); continue
    cdo = unreal.get_default_object(cls)
    hull = component_info(cdo.get_editor_property('hull_mesh'))
    ship = {'class': path, 'hull': hull, 'parts': []}
    if hull and hull['mesh']:
        add_mesh(hull['mesh']['path'])
    for pc in cdo.get_editor_property('part_catalogue'):
        part = unreal.get_default_object(pc)
        pm = part.get_editor_property('skeletal_mesh') or part.get_editor_property('static_mesh')
        stats = {}
        for key in ['part_mass', 'thrust', 'muzzle_offset']:
            try:
                value = part.get_editor_property(key)
                stats[key] = value.get_path_name() if hasattr(value, 'get_path_name') else str(value)
            except Exception:
                pass  # Non-weapon parts do not expose weapon fields.
        ship['parts'].append({'class': pc.get_path_name(), 'part_id': str(part.get_editor_property('part_id')),
                              'mesh': mesh_info(pm), 'stats': stats})
        if pm: add_mesh(pm.get_path_name())
    report['ships'].append(ship)

for key in ['Engine_Heavy', 'Engine_Standard', 'Weapon_Laser', 'Weapon_RocketPod']:
    cls = unreal.load_class(None, '/Game/GuLiStrike/Ship/BP_' + key + '.BP_' + key + '_C')
    if cls:
        cdo = unreal.get_default_object(cls)
        pm = cdo.get_editor_property('skeletal_mesh') or cdo.get_editor_property('static_mesh')
        report['legacy_parts'].append({'part_id': key, 'class': cls.get_path_name(), 'mesh': mesh_info(pm)})
        if pm: add_mesh(pm.get_path_name())

classes = [x['path'] for x in report['models'] if x['type'] == 'PresentationClass']
classes.append('/Game/GuLiStrike/Buildings/ResourceProcessingFactory/Blueprints/BP_ResourceProcessingFactory.BP_ResourceProcessingFactory_C')
ss = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
lib = unreal.SubobjectDataBlueprintFunctionLibrary
for path in classes:
    bp = unreal.load_asset(path.rsplit('.', 1)[0])
    if not bp: continue
    parts = []
    for handle in ss.k2_gather_subobject_data_for_blueprint(bp):
        obj = lib.get_object(lib.get_data(handle))
        if isinstance(obj, unreal.MeshComponent):
            info = component_info(obj)
            if info and info['mesh']:
                parts.append(info); add_mesh(info['mesh']['path'])
    add_mesh(path)
    report['assemblies'].append({'class': path, 'parts': parts})

ground = unreal.get_default_object(unreal.load_class(None, '/Game/GuLiStrike/GroundMech/BP_GroundMech_Light.BP_GroundMech_Light_C'))
for c in ground.get_components_by_class(unreal.MeshComponent):
    info = component_info(c)
    if info and info['mesh']:
        report['ground'].append(info); add_mesh(info['mesh']['path'])

for folder in ['/Game/GuLiStrike/Buildings/ResourceProcessingFactory', '/Game/GuLiStrike/Resources/Ores', '/Game/GuLiStrike/Wingman']:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for asset in registry.get_assets_by_path(folder, recursive=True):
        if str(asset.asset_class_path.asset_name) in ('StaticMesh', 'SkeletalMesh'):
            add_mesh(str(asset.package_name) + '.' + str(asset.asset_name))
OUTPUT.parent.mkdir(parents=True, exist_ok=True)
OUTPUT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'output': str(OUTPUT), 'models': len(report['models']), 'ground_components': len(report['ground']),
                  'ship_parts': [len(s['parts']) for s in report['ships']], 'errors': report['errors']}, ensure_ascii=False))
