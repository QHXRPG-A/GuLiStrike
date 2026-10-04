"""Stage A: read source assets and export a reference FBX; never save UE assets."""
import hashlib
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
PACKAGE = '/Game/Assets/RSG_UnderWater_Pack/FPS/Models/Mech/SK_FPS_Mech'


def vec(v):
    return [float(v.x), float(v.y), float(v.z)]


def transform(t):
    q = t.rotation
    return {'translation_cm': vec(t.translation),
            'rotation_xyzw': [float(q.x), float(q.y), float(q.z), float(q.w)],
            'scale': vec(t.scale3d)}


mesh = unreal.load_asset(PACKAGE)
assert isinstance(mesh, unreal.SkeletalMesh), 'Expected actual RSG skeletal mesh'
source = ROOT / 'Source'
source.mkdir(parents=True, exist_ok=True)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
data = next(a for a in registry.get_assets_by_path(PACKAGE.rsplit('/', 1)[0], False)
            if str(a.asset_name) == 'SK_FPS_Mech')
info = unreal.SkeletonService.get_skeletal_mesh_info(PACKAGE)
bones = unreal.SkeletonService.list_bones(PACKAGE)
report = {
    'stage': 'A_source_baseline', 'source_mesh': mesh.get_path_name(),
    'source_skeleton': mesh.skeleton.get_path_name(),
    'source_physics_asset': mesh.physics_asset.get_path_name(),
    'bounds_cm': {'min': vec(info.bounds_min), 'max': vec(info.bounds_max)},
    'source_tags': {k: data.get_tag_value(k) for k in ('Triangles', 'Vertices', 'LODs', 'Bones')},
    'bones': [{
        'name': str(b.bone_name), 'index': int(b.bone_index),
        'parent': str(b.parent_bone_name), 'parent_index': int(b.parent_bone_index),
        'children': [str(n) for n in b.children],
        'local': transform(b.local_transform), 'global': transform(b.global_transform),
        'retargeting_mode': str(b.retargeting_mode)
    } for b in bones],
    'material_slots': [], 'animations': [],
    'approvals': {'A': 'pending', 'B': 'not_started'},
    'ue_assets_saved': False,
}
for slot in mesh.materials:
    mat = slot.material_interface
    item = {'slot': str(slot.material_slot_name), 'material': mat.get_path_name() if mat else None,
            'texture_parameters': {}}
    if isinstance(mat, unreal.MaterialInstanceConstant):
        for name in unreal.MaterialEditingLibrary.get_texture_parameter_names(mat):
            tex = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mat, name)
            item['texture_parameters'][str(name)] = tex.get_path_name() if tex else None
    report['material_slots'].append(item)
subsystem = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
report['lod_vertex_counts'] = [int(subsystem.get_num_verts(mesh, i)) for i in range(subsystem.get_lod_count(mesh))]
for a in registry.get_assets_by_path('/Game/Assets/RSG_UnderWater_Pack/FPS/Animations/Mech', False):
    anim = a.get_asset()
    if isinstance(anim, unreal.AnimSequence):
        report['animations'].append({'path': anim.get_path_name(), 'duration_s': float(anim.get_play_length()),
                                     'skeleton': anim.get_editor_property('skeleton').get_path_name()})
assert len(report['bones']) == 44 and len(report['animations']) == 7
target = source / 'SK_FPS_Mech_Source_LOD0.fbx'
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
    options.level_of_detail = False
    task.options = options
    assert unreal.Exporter.run_asset_export_task(task), 'Reference FBX export failed'
assert target.is_file() and target.stat().st_size > 0
report['fbx'] = {'path': str(target), 'bytes': target.stat().st_size,
                 'sha256': hashlib.sha256(target.read_bytes()).hexdigest()}
(source / 'source_manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'fbx': str(target),
    'bytes': target.stat().st_size, 'bone_count': len(bones), 'animations': len(report['animations']),
    'source_triangles': report['source_tags']['Triangles'], 'ue_assets_saved': False}))
