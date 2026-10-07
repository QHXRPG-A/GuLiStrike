"""Read formal unit/component assets and export source geometry; no formal edits."""
import hashlib
import json
import sys
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0, str(ROOT / 'Scripts/CommanderLOD'))
from common import ART, selected_indices, unit_dir, units

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor.get_game_world(), 'Authoring requires an editor world without gameplay.'
registry = unreal.AssetRegistryHelpers.get_asset_registry()
sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
report = dict(success=False, editor_world=editor.get_editor_world().get_path_name(),
    actor_count=len(actors.get_all_level_actors()), units=[])


def info(mesh):
    count = sub.get_lod_count(mesh)
    return dict(asset=mesh.get_path_name(), lod_count=count,
        selected_indices=selected_indices(count),
        lods=[dict(index=i, triangles=mesh.get_num_triangles(i), sections=mesh.get_num_sections(i),
            vertices=mesh.get_num_vertices(i), uv_channels=sub.get_num_uv_channels(mesh,i)) for i in range(count)],
        screen_sizes=list(sub.get_lod_screen_sizes(mesh)),
        materials=[dict(index=i, path=slot.material_interface.get_path_name() if slot.material_interface else None,
            slot=str(slot.material_slot_name)) for i,slot in enumerate(mesh.static_materials)],
        sockets=[dict(name=str(socket.socket_name), location=list(socket.relative_location.to_tuple()),
            rotation=list(socket.relative_rotation.to_tuple()), scale=list(socket.relative_scale.to_tuple()))
            for socket in unreal.ObjectIterator(unreal.StaticMeshSocket) if socket.get_outer() == mesh])


def components(path):
    blueprint = unreal.load_asset(path.split('.')[0])
    entries = []
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    library = unreal.SubobjectDataBlueprintFunctionLibrary
    for handle in subsystem.k2_gather_subobject_data_for_blueprint(blueprint):
        data = library.get_data(handle)
        obj = library.get_object(data)
        if not isinstance(obj, unreal.StaticMeshComponent):
            continue
        mesh = obj.get_editor_property('static_mesh')
        if not mesh:
            continue
        entries.append(dict(name=obj.get_name(), mesh=mesh.get_path_name(),
            location=list(obj.get_editor_property('relative_location').to_tuple()),
            rotation=list(obj.get_editor_property('relative_rotation').to_tuple()),
            scale=list(obj.get_editor_property('relative_scale3d').to_tuple()),
            materials=[material.get_path_name() if material else None for material in obj.get_materials()]))
    return entries


for row in units():
    out = unit_dir(row['Name'])
    entry = dict(id=row['Id'], name=row['Name'], display_name=row['DisplayName'],
        presentation_scale=row['PresentationScale'], actor_class=row['ActorClass'],
        presentation_class=row['PresentationClass'], vat_definition=row['VATDefinition'],
        components=[], meshes=[])
    if row['ModelAsset']:
        paths = [row['ModelAsset']]
    else:
        entry['components'] = components(row['PresentationClass'])
        paths = sorted({component['mesh'] for component in entry['components']})
        assert paths, entry
    for path in paths:
        mesh = unreal.load_asset(path)
        assert isinstance(mesh, unreal.StaticMesh), path
        data = info(mesh)
        filename = out / 'Sources' / (mesh.get_name() + '_RenderLODs.fbx')
        options = unreal.FbxExportOption()
        options.ascii = False
        options.collision = False
        options.level_of_detail = True
        task = unreal.AssetExportTask()
        for key,value in dict(object=mesh, filename=str(filename), options=options, automated=True,
            replace_identical=True, prompt=False, exporter=unreal.StaticMeshExporterFBX()).items():
            task.set_editor_property(key,value)
        assert unreal.Exporter.run_asset_export_task(task), path
        data['fbx'] = str(filename)
        data['fbx_sha256'] = hashlib.sha256(filename.read_bytes()).hexdigest()
        entry['meshes'].append(data)
    if row['Name'] == 'BiZhiMao':
        mesh = unreal.load_asset('/Game/GuLiStrike/Commander/Units/BiZhiMao/Meshes/SM_BiZhiMao_Construction')
        entry['construction_mesh'] = info(mesh)
    (out / 'Reports/formal_before.json').write_text(json.dumps(entry,ensure_ascii=False,indent=2),encoding='utf8')
    report['units'].append(entry)
report['success'] = True
(ART/'Reports').mkdir(parents=True,exist_ok=True)
(ART/'Reports/formal_before.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True, world=report['editor_world'],
    units=[dict(id=row['id'], name=row['name'], components=len(row['components']),
        meshes=[dict(asset=mesh['asset'], triangles=[lod['triangles'] for lod in mesh['lods']]) for mesh in row['meshes']]) for row in report['units']])))
