"""Read original mecha assets through the UE registry; never saves packages."""
import json
from pathlib import Path
from collections import Counter
import unreal

OUT = Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/Source')
OUT.mkdir(parents=True, exist_ok=True)
REG = unreal.AssetRegistryHelpers.get_asset_registry()

def path(o):
    return o.get_path_name() if o else None

def dirty():
    return {'content': [path(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
            'maps': [path(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]}

report = {'roots': {}, 'meshes': [], 'materials': {}, 'dirty_before': dirty()}
mat_paths = set()
for root in ['/Game/Assets/Mech_Project', '/Game/Assets/MechaController']:
    assets = REG.get_assets_by_path(root, recursive=True)
    report['roots'][root] = {'counts': dict(Counter(str(a.asset_class_path.asset_name) for a in assets)),
        'assets': [{'path': str(a.package_name), 'class': str(a.asset_class_path.asset_name)} for a in assets]}
    for data in assets:
        cls = str(data.asset_class_path.asset_name)
        if cls not in ('StaticMesh', 'SkeletalMesh'):
            continue
        mesh = data.get_asset()
        sk = cls == 'SkeletalMesh'
        bounds = mesh.get_imported_bounds() if sk else mesh.get_bounds()
        slots = mesh.materials if sk else mesh.static_materials
        row = {'path': path(mesh), 'class': cls, 'size_cm': list((bounds.box_extent * 2).to_tuple()),
               'bounds_origin': list(bounds.origin.to_tuple()),
               'materials': [{'slot': str(s.material_slot_name), 'path': path(s.material_interface)} for s in slots]}
        if sk:
            row['skeleton'] = path(mesh.skeleton)
        report['meshes'].append(row)
        mat_paths.update(path(s.material_interface) for s in slots if s.material_interface)
for p in sorted(mat_paths):
    m = unreal.load_asset(p)
    rec = {'class': m.get_class().get_name()}
    if isinstance(m, unreal.MaterialInstanceConstant):
        rec['parent'] = path(m.parent)
        rec['scalar_overrides'] = {str(v.parameter_info.name): v.parameter_value for v in m.get_editor_property('scalar_parameter_values')}
        rec['vector_overrides'] = {str(v.parameter_info.name): [v.parameter_value.r,v.parameter_value.g,v.parameter_value.b,v.parameter_value.a] for v in m.get_editor_property('vector_parameter_values')}
        rec['texture_overrides'] = {str(v.parameter_info.name): path(v.parameter_value) for v in m.get_editor_property('texture_parameter_values')}
        parent = m.parent
        while isinstance(parent, unreal.MaterialInstanceConstant):
            parent = parent.parent
    else:
        parent = m
    if isinstance(parent, unreal.Material):
        rec['base_material'] = path(parent)
        rec['shading_model'] = str(parent.get_editor_property('shading_model'))
        rec['blend_mode'] = str(parent.get_editor_property('blend_mode'))
        rec['two_sided'] = parent.get_editor_property('two_sided')
    report['materials'][p] = rec
report['api'] = {name: [n for n in dir(getattr(unreal,name)) if any(w in n.lower() for w in ('thumbnail','render','export','capture','preview'))]
                 for name in ['AssetService','ViewportService','EditorAssetLibrary','ThumbnailTools'] if hasattr(unreal,name)}
report['dirty_after'] = dirty()
(OUT/'source_inventory.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'roots': {p: r['counts'] for p,r in report['roots'].items()}, 'meshes': report['meshes'], 'api': report['api']},ensure_ascii=False))
