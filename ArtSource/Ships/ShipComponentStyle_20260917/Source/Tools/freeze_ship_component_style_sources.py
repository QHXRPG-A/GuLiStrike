"""Read three existing Ship components and export reference inputs only.

Run with the source UE5.7 PythonScript commandlet. Never save a UE package.
Output is immutable after a successful source snapshot; use a new revision for
subsequent snapshots instead of overwriting the baseline.
"""
import hashlib
import importlib.util
import json
import shutil
import traceback
from datetime import datetime, timezone
from pathlib import Path

import unreal

PROJECT = Path('D:/UE5.7/test1')
ROOT = PROJECT / 'ArtSource/Ships/ShipComponentStyle_20260917'
SOURCE = ROOT / 'Source'
REPORT = SOURCE / 'source_snapshot_v1.json'
FBX_DIR = SOURCE / 'FBX_static_v1'
KEYS = ('Twin_Barrel_Turret', 'CIWS', 'Thor_MissilePod')
ASSETS = '/Game/Assets/Ships/ShipComponent/'


def dump(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def xyz(v):
    return [float(v.x), float(v.y), float(v.z)]


def transform(t):
    return dict(location=xyz(t.translation),
                rotation=[t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w],
                scale=xyz(t.scale3d))


def package_stat(asset_path):
    package = asset_path.split('.')[0]
    path = PROJECT / 'Content' / (package.removeprefix('/Game/') + '.uasset')
    stat = path.stat()
    return dict(size=stat.st_size, mtime_ns=stat.st_mtime_ns)


def export_reference(asset, target):
    if target.exists():
        raise RuntimeError('Reference output already exists: ' + str(target))
    task = unreal.AssetExportTask()
    task.object = asset
    task.filename = str(target)
    task.automated = True
    task.prompt = False
    task.replace_identical = False
    task.exporter = (unreal.SkeletalMeshExporterFBX()
                     if isinstance(asset, unreal.SkeletalMesh)
                     else unreal.StaticMeshExporterFBX())
    options = unreal.FbxExportOption()
    options.set_editor_property('ascii', False)
    options.set_editor_property('collision', False)
    options.set_editor_property('level_of_detail', False)
    # Commandlets with NullRHI cannot render material baking targets. This
    # baseline needs geometry/interfaces only; preserve material references in JSON.
    options.set_editor_property('bake_material_inputs', unreal.FbxMaterialBakeMode.DISABLED)
    task.options = options
    if not unreal.Exporter.run_asset_export_task(task) or not target.is_file():
        raise RuntimeError('Reference export failed: ' + asset.get_path_name())
    return dict(path=str(target.relative_to(ROOT)), size=target.stat().st_size,
                sha256=hashlib.sha256(target.read_bytes()).hexdigest())


def main():
    if REPORT.exists():
        raise RuntimeError('Snapshot already frozen; do not overwrite ' + str(REPORT))
    SOURCE.mkdir(parents=True, exist_ok=True)
    FBX_DIR.mkdir(exist_ok=True)
    spec = importlib.util.spec_from_file_location(
        'ship_component_source_reader', PROJECT / 'Scripts/author_ship_component_rigs.py')
    reader = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reader)
    protected = [ASSETS + 'SM_SC_' + k for k in KEYS]
    protected += ['/Game/GuLiStrike/Ship/Parts/BP_SC_' + k for k in KEYS]
    protected += [ASSETS + 'Rigged/' + prefix + k
                  for k in KEYS[:2] for prefix in ('SKM_SC_', 'SK_SC_')]
    before = {p: package_stat(p) for p in protected}
    report = dict(success=False, captured_at=datetime.now(timezone.utc).isoformat(),
                  engine=unreal.SystemLibrary.get_engine_version(),
                  engine_path='D:/UnrealEngine-5.7', art_revision='1.0',
                  coordinate_system='UE centimeters; +Z up; original mesh axes retained',
                  purpose='Read-only source capture for reference design A; not new model production',
                  parts={}, protected_before=before)
    for key in KEYS:
        bp_path = '/Game/GuLiStrike/Ship/Parts/BP_SC_' + key
        bp = unreal.load_asset(bp_path)
        cdo = unreal.get_default_object(bp.generated_class())
        sk = cdo.get_editor_property('skeletal_mesh')
        mesh = sk or cdo.get_editor_property('static_mesh')
        source_mesh = unreal.load_asset(ASSETS + 'SM_SC_' + key)
        source_data = reader.inspect_source(source_mesh)
        dump(SOURCE / (key + '_original_geometry.json'), source_data)
        sockets = []
        for index in range(1, 65):
            socket = mesh.find_socket('Socket_' + str(index))
            if not socket:
                continue
            row = dict(name='Socket_' + str(index), location=xyz(socket.relative_location),
                       rotation=[socket.relative_rotation.pitch, socket.relative_rotation.yaw,
                                 socket.relative_rotation.roll], scale=xyz(socket.relative_scale))
            if sk:
                row['bone'] = str(socket.bone_name)
            sockets.append(row)
        part = dict(blueprint=bp_path, visual_mesh=mesh.get_path_name(),
                    original_mesh=source_mesh.get_path_name(),
                    visual_type=str(cdo.get_editor_property('visual_type')),
                    part_relative_transform=transform(cdo.get_editor_property('part_relative_transform')),
                    compatible_sockets=[str(n) for n in cdo.get_editor_property('compatible_sockets')],
                    muzzle_socket_name=str(cdo.get_editor_property('muzzle_socket_name')),
                    sockets=sockets, original_bounds_cm=source_data['bounds'],
                    original_geometry_sha256=source_data['geometry_sha256'],
                    original_triangles=source_data['triangles'],
                    original_materials=source_data['materials'],
                    exports=[export_reference(source_mesh, FBX_DIR / ('SM_SC_' + key + '.fbx'))])
        if sk:
            bones = list(unreal.SkeletonService.list_bones(mesh.get_path_name()))
            part['skeleton'] = sk.skeleton.get_path_name()
            part['bones'] = [dict(name=str(b.bone_name), parent=str(b.parent_bone_name),
                                  local=transform(b.local_transform), global_transform=transform(b.global_transform))
                             for b in bones]
            # UE 5.7 unconditionally builds FFbxMaterialBakingMeshData during
            # skeletal FBX export, even when baking is disabled; NullRHI has no
            # MeshObject. Preserve the existing authoring sources instead and
            # capture current skeleton/socket data directly from the loaded asset.
            part['skeletal_fbx_export'] = 'not_run: NullRHI unsupported; preserved existing authoring FBX'
            part['preserved_authoring_sources'] = []
            for subdir, filename in [('Exports', 'SKM_SC_' + key + '.fbx'),
                                     ('Blender', 'SC_' + key + '.blend')]:
                original = PROJECT / 'ArtSource/Ships/ShipComponentRigs' / subdir / filename
                target = SOURCE / 'ExistingAuthoring' / filename
                target.parent.mkdir(parents=True, exist_ok=True)
                if target.exists():
                    raise RuntimeError('Preserved source already exists: ' + str(target))
                shutil.copy2(original, target)
                part['preserved_authoring_sources'].append(dict(
                    source=str(original.relative_to(PROJECT)), path=str(target.relative_to(ROOT)),
                    size=target.stat().st_size,
                    sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
            by_name = {str(b.bone_name): b.global_transform for b in bones}
            for row in sockets:
                s = mesh.find_socket(row['name'])
                t = unreal.Transform(location=s.relative_location, rotation=s.relative_rotation,
                                     scale=s.relative_scale).multiply(by_name[row['bone']])
                row['mesh_space'] = transform(t)
        part['visual_bounds'] = str(mesh.get_bounds())
        for name in ('part_mass', 'damage', 'fire_rate'):
            part[name] = float(cdo.get_editor_property(name))
        report['parts'][key] = part
    report['protected_after'] = {p: package_stat(p) for p in protected}
    if before != report['protected_after']:
        raise RuntimeError('A protected UE package changed during source capture')
    report['source_packages_unchanged'] = True
    report['success'] = True
    dump(REPORT, report)
    unreal.log('SHIP_COMPONENT_STYLE_SNAPSHOT_SUCCESS ' + str(REPORT))


if __name__ == '__main__':
    try:
        main()
    except Exception:
        dump(ROOT / 'Source/source_capture_error.json', dict(success=False, traceback=traceback.format_exc()))
        raise
