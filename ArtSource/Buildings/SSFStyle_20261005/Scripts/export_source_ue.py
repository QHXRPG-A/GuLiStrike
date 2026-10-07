"""Extract Stage A baselines through UE APIs; never save or edit UE packages."""
from pathlib import Path
from collections import Counter
import hashlib
import json
import unreal

ROOT = Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
SOURCE_ROOT = '/Game/Assets/SSF_Buildings/Buildings'
OUT = ROOT / 'Source'
OUT.mkdir(parents=True, exist_ok=True)
REG = unreal.AssetRegistryHelpers.get_asset_registry()

def objpath(o):
    return o.get_path_name() if o else None

def vector(v):
    return [float(v.x), float(v.y), float(v.z)]

def transform(t):
    q = t.rotation
    return {'translation_cm': vector(t.translation), 'rotation_xyzw': [q.x, q.y, q.z, q.w],
            'scale': vector(t.scale3d)}

def dirty():
    return {'content': sorted(objpath(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()),
            'maps': sorted(objpath(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())}

def extract(asset, target, exporter, options=None):
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.exists():
        task = unreal.AssetExportTask()
        task.object, task.filename = asset, str(target)
        task.automated, task.prompt, task.replace_identical = True, False, False
        task.exporter = exporter
        if options:
            task.options = options
        assert unreal.Exporter.run_asset_export_task(task), objpath(asset)
    assert target.is_file() and target.stat().st_size, str(target)
    return {'file': str(target.relative_to(ROOT)).replace('\\', '/'), 'bytes': target.stat().st_size,
            'sha256': hashlib.sha256(target.read_bytes()).hexdigest()}

before = dirty()
assets = REG.get_assets_by_path(SOURCE_ROOT, recursive=True)
report = {'stage': 'A_source_baseline', 'engine': unreal.SystemLibrary.get_engine_version(),
          'source_root': SOURCE_ROOT, 'counts': dict(Counter(str(a.asset_class_path.asset_name) for a in assets)),
          'meshes': [], 'animations': [], 'textures': [], 'blueprints': [],
          'dirty_before': before, 'ue_assets_saved': False, 'source_modified': False,
          'approvals': {'A': 'pending', 'B': 'not_started'}}
options = unreal.FbxExportOption()
options.ascii, options.collision, options.level_of_detail = False, False, False
options.export_preview_mesh = False
options.map_skeletal_motion_to_root = False
options.export_morph_targets = False
for data in sorted(assets, key=lambda a: str(a.package_name)):
    cls = str(data.asset_class_path.asset_name)
    package = str(data.package_name)
    name = str(data.asset_name)
    if cls in ('SkeletalMesh', 'StaticMesh'):
        mesh = data.get_asset()
        sk = cls == 'SkeletalMesh'
        bounds = mesh.get_imported_bounds() if sk else mesh.get_bounds()
        slots = mesh.materials if sk else mesh.static_materials
        row = {'key': name.replace('SK_TB1_', ''), 'path': objpath(mesh), 'class': cls,
               'dimensions_cm': vector(bounds.box_extent * 2), 'origin_cm': vector(bounds.origin),
               'lod0_triangles': int(data.get_tag_value('Triangles')),
               'source_lods': data.get_tag_value('LODs'), 'materials': []}
        for slot in slots:
            m = slot.material_interface
            parent = m
            rec = {'slot': str(slot.material_slot_name), 'path': objpath(m)}
            if isinstance(m, unreal.MaterialInstanceConstant):
                rec['parent'] = objpath(m.parent)
                rec['vector_parameters'] = [str(n) for n in unreal.MaterialEditingLibrary.get_vector_parameter_names(m)]
                while isinstance(parent, unreal.MaterialInstanceConstant):
                    parent = parent.parent
            if isinstance(parent, unreal.Material):
                rec.update({'blend_mode': str(parent.get_editor_property('blend_mode')),
                            'two_sided': bool(parent.get_editor_property('two_sided'))})
            row['materials'].append(rec)
        if sk:
            row['skeleton'] = objpath(mesh.skeleton)
            row['physics_asset'] = objpath(mesh.physics_asset)
            row['bones'] = [{'name': str(b.bone_name), 'index': b.bone_index,
                             'parent': str(b.parent_bone_name), 'parent_index': b.parent_bone_index,
                             'local': transform(b.local_transform), 'global': transform(b.global_transform)}
                            for b in unreal.SkeletonService.list_bones(package)]
        row.update(extract(mesh, OUT / 'Meshes' / (name + '_Source_LOD0.fbx'),
                           unreal.SkeletalMeshExporterFBX() if sk else unreal.StaticMeshExporterFBX(), options))
        report['meshes'].append(row)
    elif cls == 'AnimSequence':
        anim = data.get_asset()
        row = {'path': objpath(anim), 'name': name, 'duration_s': float(anim.get_play_length()),
               'frames': int(unreal.AnimationLibrary.get_num_frames(anim)),
               'skeleton': objpath(anim.get_editor_property('skeleton'))}
        row.update(extract(anim, OUT / 'Animations' / (name + '.fbx'), unreal.AnimSequenceExporterFBX(), options))
        report['animations'].append(row)
    elif cls == 'Texture2D' and '/Mobile_textures/' not in package and any(
            term in name for term in ('BaseColor', 'ColorMask', 'ColorTeam', 'Emissive', 'Emission', 'Logo')):
        tex = data.get_asset()
        row = {'path': objpath(tex), 'name': name, 'srgb': bool(tex.get_editor_property('srgb'))}
        row.update(extract(tex, OUT / 'Textures' / (name + '.png'), unreal.TextureExporterPNG()))
        report['textures'].append(row)
    elif cls == 'Blueprint':
        dep_options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,
                                                           include_hard_package_references=True)
        report['blueprints'].append({'path': package,
                                     'dependencies': [str(x) for x in REG.get_dependencies(package, dep_options)]})
assert len(report['meshes']) == 10 and len(report['animations']) == 22
report['dirty_after'] = dirty()
report['new_dirty_content'] = sorted(set(report['dirty_after']['content']) - set(before['content']))
report['new_dirty_maps'] = sorted(set(report['dirty_after']['maps']) - set(before['maps']))
(OUT / 'source_manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'meshes': len(report['meshes']),
    'animations': len(report['animations']), 'textures': len(report['textures']),
    'new_dirty_content': report['new_dirty_content'], 'new_dirty_maps': report['new_dirty_maps'],
    'ue_assets_saved': False, 'manifest': str(OUT / 'source_manifest.json')}))
