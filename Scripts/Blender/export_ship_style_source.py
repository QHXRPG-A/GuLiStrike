"""Read the live Ship references and export its existing hull for a Blender study."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1/ArtSource/Ships/ShipStylizedStudy_20260916')
ROOT.mkdir(parents=True, exist_ok=True)
report = {'blueprints': [], 'actors': [], 'materials': []}

def path(obj):
    return obj.get_path_name() if obj else None

def describe_component(comp):
    return {'name': comp.get_name(), 'mesh': path(comp.static_mesh),
            'materials': [path(comp.get_material(i)) for i in range(comp.get_num_materials())]}

for bp_path in ['/Game/GuLiStrike/Ship/BP_GuLiStrikeShip', '/Game/GuLiStrike/Ship/BP_CombatAvatarFly01']:
    bp = unreal.load_asset(bp_path)
    if not bp:
        continue
    cdo = unreal.get_default_object(bp.generated_class())
    components = [describe_component(c) for c in cdo.get_components_by_class(unreal.StaticMeshComponent)]
    report['blueprints'].append({'path': bp_path, 'components': components})

for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if any(t in actor.get_class().get_name().lower() for t in ('ship', 'combatavatarfly', 'dreadnought')):
        report['actors'].append({'name': actor.get_name(), 'class': actor.get_class().get_name(),
                                 'components': [describe_component(c) for c in actor.get_components_by_class(unreal.StaticMeshComponent)]})

hulls = [c for bp in report['blueprints'] for c in bp['components'] if c['mesh'] and 'Dreadnought' in c['mesh']]
assert hulls, 'The inspected Ship Blueprints do not reference a Dreadnought hull.'
mesh_path = hulls[-1]['mesh']
mesh = unreal.load_asset(mesh_path)
report['source_mesh'] = mesh_path
report['bounds'] = str(mesh.get_bounding_box())
try:
    report['import_sources'] = list(mesh.get_editor_property('asset_import_data').extract_filenames())
except Exception as exc:
    report['import_sources_error'] = str(exc)
for slot in mesh.get_editor_property('static_materials'):
    mat = slot.material_interface
    item = {'slot': str(slot.material_slot_name), 'material': path(mat), 'textures': {}}
    if isinstance(mat, unreal.MaterialInstanceConstant):
        for name in unreal.MaterialEditingLibrary.get_texture_parameter_names(mat):
            tex = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mat, name)
            item['textures'][str(name)] = path(tex)
    report['materials'].append(item)

target = ROOT / 'Source' / 'SM_Dreadnought_Hull.fbx'
target.parent.mkdir(parents=True, exist_ok=True)
if not target.exists():
    task = unreal.AssetExportTask()
    task.object = mesh
    task.filename = str(target)
    task.automated = True
    task.prompt = False
    task.replace_identical = False
    task.exporter = unreal.StaticMeshExporterFBX()
    options = unreal.FbxExportOption()
    options.set_editor_property('ascii', False)
    options.set_editor_property('collision', False)
    options.set_editor_property('level_of_detail', False)
    task.options = options
    assert unreal.Exporter.run_asset_export_task(task), 'Hull FBX export failed'
report['fbx'] = str(target)
report['exported_textures'] = {}
for item in report['materials']:
    for param, texture_path in item['textures'].items():
        if param not in ('BaseColorTexture', 'NormalTexture', 'MetallicRoughnessTexture', 'EmissiveTexture') or not texture_path:
            continue
        tex_target = target.parent / (param + '.tga')
        if not tex_target.exists():
            tex_task = unreal.AssetExportTask()
            tex_task.object = unreal.load_asset(texture_path)
            tex_task.filename = str(tex_target)
            tex_task.automated = True
            tex_task.prompt = False
            tex_task.replace_identical = False
            tex_task.exporter = unreal.TextureExporterTGA()
            assert unreal.Exporter.run_asset_export_task(tex_task), 'Texture export failed: ' + param
        report['exported_textures'][param] = str(tex_target)
report['success'] = target.is_file() and target.stat().st_size > 0
(ROOT / 'source_manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=False))
