"""Read existing WarMachine production assets without changing editor packages."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT/'ArtSource/TacticalStyle_20260916/WarMachine_LevelNodes_v6/UEProduction'
OUT.mkdir(parents=True, exist_ok=True)
BASE = '/Game/Commander/Units/Tactical/Cel/WarMachine'
LIB = unreal.EditorAssetLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()
SUB = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)

def prop(o, name):
    try:
        value = o.get_editor_property(name)
        if isinstance(value, unreal.Object): return value.get_path_name()
        return str(value)
    except Exception as exc: return 'unavailable: '+str(exc)

report = {'success': True, 'world': unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name(),
          'pie': unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), 'assets': []}
for data in REG.get_assets_by_path(BASE, recursive=True):
    obj = data.get_asset()
    item = {'path': obj.get_path_name(), 'class': obj.get_class().get_name(),
            'owner': LIB.get_metadata_tag(obj, 'GuLi.ModelProduction.Owner'),
            'referencers': [str(x) for x in REG.get_referencers(data.package_name, unreal.AssetRegistryDependencyOptions())]}
    if isinstance(obj, unreal.StaticMesh):
        bounds = obj.get_bounds()
        item.update(dimensions_cm=list((bounds.box_extent*2).to_tuple()), bounds_origin=list(bounds.origin.to_tuple()),
                    lod_triangles=[obj.get_num_triangles(i) for i in range(SUB.get_lod_count(obj))],
                    lod_screens=list(SUB.get_lod_screen_sizes(obj)), uv_channels=SUB.get_num_uv_channels(obj, 0),
                    slots=[{'name': str(s.material_slot_name), 'material': s.material_interface.get_path_name() if s.material_interface else None} for s in obj.static_materials],
                    body_setup=prop(obj, 'body_setup'), import_data=prop(obj, 'asset_import_data'))
        item['sockets'] = []
        component = unreal.new_object(unreal.StaticMeshComponent)
        component.set_static_mesh(obj)
        for name in component.get_all_socket_names():
            s = obj.find_socket(name)
            item['sockets'].append({'name': str(s.socket_name), 'location': list(s.relative_location.to_tuple()), 'rotation': list(s.relative_rotation.to_tuple()), 'scale': list(s.relative_scale.to_tuple())})
        item['source_files'] = list(obj.get_editor_property('asset_import_data').extract_filenames())
    elif isinstance(obj, unreal.SkeletalMesh):
        bounds = obj.get_imported_bounds()
        item.update(dimensions_cm=list((bounds.box_extent*2).to_tuple()), skeleton=prop(obj, 'skeleton'),
                    slots=[{'name': str(s.material_slot_name), 'material': s.material_interface.get_path_name() if s.material_interface else None} for s in obj.materials],
                    bones=[str(b.bone_name) for b in unreal.SkeletonService.list_bones(obj.get_path_name())],
                    source_files=list(obj.get_editor_property('asset_import_data').extract_filenames()))
    elif isinstance(obj, unreal.Material):
        item.update(textures=[x.get_path_name() for x in unreal.MaterialEditingLibrary.get_used_textures(obj)], shading_model=prop(obj, 'shading_model'))
    elif isinstance(obj, unreal.Texture):
        item.update(srgb=prop(obj, 'srgb'), compression=prop(obj, 'compression_settings'), source_files=list(obj.get_editor_property('asset_import_data').extract_filenames()))
    report['assets'].append(item)
report['dirty_packages'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
(OUT/globals().get('WARMACHINE_INSPECT_FILENAME', 'before-ue.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report, ensure_ascii=False))
