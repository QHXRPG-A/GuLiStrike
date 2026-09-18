"""Freeze the three support-component originals without saving UE assets."""
import importlib.util
import json
import traceback
from datetime import datetime, timezone
from pathlib import Path
import unreal

PROJECT = Path('D:/UE5.7/test1')
ROOT = PROJECT / 'ArtSource/Ships/ShipComponentStyle_20260917/Batch03'
spec = importlib.util.spec_from_file_location('source_capture', PROJECT / 'Scripts/Art/freeze_ship_component_style_sources.py')
source = importlib.util.module_from_spec(spec)
spec.loader.exec_module(source)
source.ROOT = ROOT
source.SOURCE = ROOT / 'Source'
source.REPORT = source.SOURCE / 'source_snapshot_v1.json'
source.FBX_DIR = source.SOURCE / 'FBX_static_v1'
source.KEYS = ('Drone_LaunchBay', 'Electronic_JammingDevice', 'Shield_Generator')


def main():
    if source.REPORT.exists():
        raise RuntimeError('Snapshot already frozen')
    source.SOURCE.mkdir(parents=True, exist_ok=True)
    source.FBX_DIR.mkdir(exist_ok=True)
    reader_spec = importlib.util.spec_from_file_location('original_reader', PROJECT / 'Scripts/author_ship_component_rigs.py')
    reader = importlib.util.module_from_spec(reader_spec)
    reader_spec.loader.exec_module(reader)
    protected = [source.ASSETS + 'SM_SC_' + key for key in source.KEYS]
    protected += ['/Game/GuLiStrike/Ship/Parts/BP_SC_' + key for key in source.KEYS]
    before = {path: source.package_stat(path) for path in protected}
    report = dict(success=False, captured_at=datetime.now(timezone.utc).isoformat(),
        engine=unreal.SystemLibrary.get_engine_version(), engine_path='D:/UnrealEngine-5.7', art_revision='1.0',
        coordinate_system='UE centimeters; +Z up; original mesh axes retained',
        purpose='Read-only static support-component capture for reference A; not production materials',
        parts={}, protected_before=before)
    for key in source.KEYS:
        bp_path = '/Game/GuLiStrike/Ship/Parts/BP_SC_' + key
        bp = unreal.load_asset(bp_path)
        cdo = unreal.get_default_object(bp.generated_class())
        assert cdo.get_editor_property('skeletal_mesh') is None, key
        mesh = cdo.get_editor_property('static_mesh')
        expected_mesh = unreal.load_asset(source.ASSETS + 'SM_SC_' + key)
        assert mesh == expected_mesh, key + ': current visual differs from catalogue original'
        geometry = reader.inspect_source(mesh)
        source.dump(source.SOURCE / (key + '_original_geometry.json'), geometry)
        component = unreal.new_object(unreal.StaticMeshComponent)
        component.set_static_mesh(mesh)
        sockets = [dict(name=str(s.socket_name), location=source.xyz(s.relative_location),
            rotation=[s.relative_rotation.pitch, s.relative_rotation.yaw, s.relative_rotation.roll],
            scale=source.xyz(s.relative_scale)) for s in (mesh.find_socket(name) for name in component.get_all_socket_names()) if s]
        report['parts'][key] = dict(blueprint=bp_path, visual_mesh=mesh.get_path_name(), original_mesh=mesh.get_path_name(),
            blueprint_parent_class=cdo.get_class().get_super_class().get_name() if hasattr(cdo.get_class(), 'get_super_class') else 'GuLiStrikeShipPartComponent',
            visual_type=str(cdo.get_editor_property('visual_type')), mechanical_type='static',
            part_relative_transform=source.transform(cdo.get_editor_property('part_relative_transform')),
            compatible_sockets=[str(n) for n in cdo.get_editor_property('compatible_sockets')],
            sockets=sockets, bones=[], muzzle_socket_name=None, weapon_properties='not_applicable: support part',
            original_bounds_cm=geometry['bounds'], original_geometry_sha256=geometry['geometry_sha256'],
            original_triangles=geometry['triangles'], original_materials=geometry['materials'],
            part_mass=float(cdo.get_editor_property('part_mass')),
            exports=[source.export_reference(mesh, source.FBX_DIR / ('SM_SC_' + key + '.fbx'))])
    report['protected_after'] = {path: source.package_stat(path) for path in protected}
    assert before == report['protected_after'], 'Protected UE package changed'
    report['source_packages_unchanged'] = True
    report['success'] = True
    source.dump(source.REPORT, report)
    unreal.log('SHIP_BATCH03_SOURCE_SNAPSHOT_SUCCESS ' + str(source.REPORT))


if __name__ == '__main__':
    try:
        main()
    except Exception:
        ROOT.mkdir(parents=True, exist_ok=True)
        (ROOT / 'source_capture_error.json').write_text(json.dumps({'success': False, 'traceback': traceback.format_exc()}, indent=2), encoding='utf-8')
        raise
