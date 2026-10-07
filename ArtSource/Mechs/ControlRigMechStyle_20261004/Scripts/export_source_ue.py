"""Stage A source query/export worker. Never edit or save any Unreal package."""
import hashlib
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004')
SOURCE_ROOT = '/Game/Assets/ControlRig/Characters/Mech'
MESH_PATH = SOURCE_ROOT + '/Meshes/SKM_Mech'
WORKER_FLAG = '-ControlRigMechSourceReadWorker'


def vec(v):
    return [float(v.x), float(v.y), float(v.z)]


def transform(t):
    q = t.rotation
    return {'translation_cm': vec(t.translation),
            'rotation_xyzw': [float(q.x), float(q.y), float(q.z), float(q.w)],
            'scale': vec(t.scale3d)}


def path(obj):
    return obj.get_path_name() if obj else None


def digest(p):
    return {'path': str(p), 'bytes': p.stat().st_size,
            'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}


def export_fbx(mesh, target, all_lods=False):
    if not target.exists():
        task = unreal.AssetExportTask()
        task.object = mesh
        task.filename = str(target)
        task.automated = True
        task.prompt = False
        task.replace_identical = False
        task.exporter = unreal.SkeletalMeshExporterFBX()
        options = unreal.FbxExportOption()
        options.ascii = False
        options.collision = False
        options.level_of_detail = all_lods
        options.bake_material_inputs = unreal.FbxMaterialBakeMode.DISABLED
        task.options = options
        assert unreal.Exporter.run_asset_export_task(task), 'Source FBX export failed'
    assert target.is_file() and target.stat().st_size > 0
    return digest(target)


def main():
    assert WORKER_FLAG in unreal.SystemLibrary.get_command_line(), 'Dedicated worker flag required'
    out = ROOT / 'Source'
    out.mkdir(parents=True, exist_ok=True)
    mesh = unreal.load_asset(MESH_PATH)
    assert isinstance(mesh, unreal.SkeletalMesh), 'SKM_Mech is not a skeletal mesh'
    skeleton = mesh.get_editor_property('skeleton')
    physics = mesh.get_editor_property('physics_asset')
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    records = registry.get_assets_by_path(SOURCE_ROOT, recursive=True)
    data = next(a for a in records if str(a.asset_name) == 'SKM_Mech')
    info = unreal.SkeletonService.get_skeletal_mesh_info(MESH_PATH)
    bones = unreal.SkeletonService.list_bones(MESH_PATH)
    subsystem = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    report = {
        'stage': 'A_source_baseline', 'source_root': SOURCE_ROOT,
        'source_mesh': path(mesh), 'mesh_class': mesh.get_class().get_name(),
        'source_skeleton': path(skeleton), 'skeleton_class': skeleton.get_class().get_name(),
        'source_physics_asset': path(physics),
        'bounds_cm': {'min': vec(info.bounds_min), 'max': vec(info.bounds_max)},
        'source_tags': {k: data.get_tag_value(k) for k in ('Triangles', 'Vertices', 'LODs', 'Bones')},
        'bones': [{'name': str(b.bone_name), 'index': int(b.bone_index),
                   'parent': str(b.parent_bone_name), 'parent_index': int(b.parent_bone_index),
                   'children': [str(n) for n in b.children],
                   'local': transform(b.local_transform), 'global': transform(b.global_transform),
                   'retargeting_mode': str(b.retargeting_mode)} for b in bones],
        'source_inventory': [{'path': str(a.package_name), 'name': str(a.asset_name),
                              'class': str(a.asset_class_path.asset_name)} for a in records],
        'material_slots': [], 'lods': [], 'animations': [], 'sockets': [],
        'query_limitations': [], 'approvals': {'A': 'pending', 'B': 'not_started'},
        'ue_assets_saved': False, 'production_geometry_modified': False,
    }
    for i in range(subsystem.get_lod_count(mesh)):
        sections = subsystem.get_num_sections(mesh, i)
        report['lods'].append({'index': i, 'ue_vertices': int(subsystem.get_num_verts(mesh, i)),
            'ue_sections': int(sections),
            'section_material_slots': [int(subsystem.get_lod_material_slot(mesh, i, j)) for j in range(sections)],
            'triangles': None, 'triangle_query': 'count exported source FBX per LOD in Blender'})
    for slot in mesh.get_editor_property('materials'):
        mat = slot.material_interface
        item = {'slot': str(slot.material_slot_name), 'material': path(mat), 'texture_parameters': {}}
        if isinstance(mat, unreal.MaterialInstanceConstant):
            for name in unreal.MaterialEditingLibrary.get_texture_parameter_names(mat):
                tex = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mat, name)
                item['texture_parameters'][str(name)] = path(tex)
            try:
                item['parent'] = path(mat.get_editor_property('parent'))
                item['vector_parameters'] = {str(n): str(unreal.MaterialEditingLibrary.get_material_instance_vector_parameter_value(mat, n))
                    for n in unreal.MaterialEditingLibrary.get_vector_parameter_names(mat)}
            except Exception as e:
                item['parameter_note'] = str(e)
        report['material_slots'].append(item)
    for name in ('Mech_Deploy', 'Mech_Idle', 'Mech_Walk'):
        anim = unreal.load_asset(SOURCE_ROOT + '/Animations/' + name)
        assert isinstance(anim, unreal.AnimSequence), name + ' is not an AnimSequence'
        item = {'path': path(anim), 'class': anim.get_class().get_name(),
                'duration_s': float(anim.get_play_length()),
                'skeleton': path(anim.get_editor_property('skeleton'))}
        item['animation_read_api'] = [n for n in dir(anim) if any(k in n for k in ('frame', 'data_model', 'bone_track'))]
        try:
            model = anim.get_editor_property('data_model')
            item['data_model_methods'] = [n for n in dir(model) if any(k in n for k in ('frame','bone_track'))]
            for method in ('get_number_of_frames', 'get_number_of_keys', 'get_frame_rate', 'get_bone_track_names'):
                try:
                    value = getattr(model, method)()
                    item[method] = [str(v) for v in value] if method == 'get_bone_track_names' else str(value)
                except Exception as e:
                    item[method + '_unavailable'] = str(e)
        except Exception as e:
            item['data_model_note'] = str(e)
        report['animations'].append(item)
    cr = unreal.load_asset(SOURCE_ROOT + '/Rigs/CR_Mech')
    assert cr is not None, 'Missing CR_Mech'
    rig = {'path': path(cr), 'class': cr.get_class().get_name(),
           'read_api': [n for n in dir(cr) if any(k in n for k in ('preview','hierarchy','model','controller'))]}
    for method in ('get_preview_mesh', 'get_hierarchy', 'get_all_models'):
        try:
            value = getattr(cr, method)()
            if method == 'get_preview_mesh':
                rig['preview_mesh'] = path(value)
            elif method == 'get_hierarchy':
                rig['hierarchy_path'] = path(value)
                rig['hierarchy_read_api'] = [n for n in dir(value) if any(k in n for k in ('key','control','bone','parent'))]
                try:
                    rig['elements'] = [{'name': str(k.name), 'type': str(k.type)} for k in value.get_all_keys()]
                except Exception as e:
                    rig['hierarchy_note'] = str(e)
            else:
                rig['models'] = [path(v) for v in value]
        except Exception as e:
            rig[method + '_unavailable'] = str(e)
    opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,
        include_hard_package_references=True, include_searchable_names=False,
        include_soft_management_references=False, include_hard_management_references=False)
    rig['package_dependencies'] = [str(n) for n in registry.get_dependencies(SOURCE_ROOT + '/Rigs/CR_Mech', opts)]
    report['control_rig'] = rig
    report['socket_count'] = int(info.socket_count)
    if info.socket_count:
        report['query_limitations'].append('Socket details require additional API inspection')
    (out / 'source_query.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    report['fbx_lod0'] = export_fbx(mesh, out / 'SKM_Mech_Source_LOD0.fbx')
    report['fbx_all_lods'] = export_fbx(mesh, out / 'SKM_Mech_Source_AllLODs.fbx', True)
    (out / 'source_manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    result = {'success': True, 'source_mesh': report['source_mesh'], 'bone_count': len(bones),
              'source_tags': report['source_tags'], 'lods': report['lods'],
              'control_rig_preview': rig.get('preview_mesh'), 'ue_assets_saved': False}
    (ROOT / 'source_export_result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    unreal.log('CONTROLRIG_SOURCE_EXPORT_OK ' + json.dumps(result))


if __name__ == '__main__':
    try:
        main()
    except Exception:
        ROOT.mkdir(parents=True, exist_ok=True)
        (ROOT / 'source_export_error.txt').write_text(traceback.format_exc(), encoding='utf-8')
        unreal.log_error(traceback.format_exc())
    finally:
        if WORKER_FLAG in unreal.SystemLibrary.get_command_line():
            unreal.SystemLibrary.quit_editor()
