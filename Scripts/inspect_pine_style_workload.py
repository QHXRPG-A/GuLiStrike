"""Read-only inventory of the pine pack for an art-work estimate; no asset saves."""
import json
from collections import Counter
from pathlib import Path
import unreal

BASE = '/Game/StylizedPineEnvironment/Assets'
OUT = Path('D:/UE5.7/test1/ArtSource/Environment/PineStyleComparison_20260917')
REG = unreal.AssetRegistryHelpers.get_asset_registry()
MESH_EDITOR = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)

def path(o):
    return o.get_path_name() if o else None

def dirty():
    return {'content': [path(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
            'maps': [path(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]}

def collect():
    report = {'scope': 'read-only inventory, not implementation approval', 'dirty_before': dirty(),
              'meshes': [], 'material_instances': [], 'base_materials': {}, 'textures': {}, 'errors': []}
    assets = REG.get_assets_by_path(BASE, recursive=True)
    report['pack_counts'] = dict(Counter(str(a.asset_class_path.asset_name) for a in assets))
    mats = set()
    for folder in ('Trees', 'Grass', 'Bush', 'Fern', 'Flowers'):
        for data in REG.get_assets_by_path(BASE + '/Meshes/' + folder, recursive=True):
            if str(data.asset_class_path.asset_name) != 'StaticMesh':
                continue
            mesh = data.get_asset()
            bounds = mesh.get_bounds()
            row = {'category': folder, 'path': path(mesh), 'size_cm': list((bounds.box_extent * 2).to_tuple()),
                   'triangles': [mesh.get_num_triangles(i) for i in range(mesh.get_num_lods())],
                   'uv_channels': [MESH_EDITOR.get_num_uv_channels(mesh, i) for i in range(mesh.get_num_lods())],
                   'has_vertex_colors': MESH_EDITOR.has_vertex_colors(mesh),
                   'materials': [path(s.material_interface) for s in mesh.static_materials]}
            import_data = mesh.get_editor_property('asset_import_data')
            if import_data:
                sources = list(import_data.extract_filenames())
                row['source_filenames'] = sources
                row['source_files_present'] = [Path(s).is_file() for s in sources]
            report['meshes'].append(row)
            if folder in ('Trees', 'Grass'):
                mats.update(p for p in row['materials'] if p)
    # Include unused sibling instances too: batch scope differs from visible meshes.
    for folder in ('Bark', 'Leaves', 'Grass'):
        for data in REG.get_assets_by_path(BASE + '/Materials/' + folder, recursive=True):
            if str(data.asset_class_path.asset_name) == 'MaterialInstanceConstant':
                mats.add(path(data.get_asset()))
    tex_paths = set()
    for mat_path in sorted(mats):
        m = unreal.load_asset(mat_path)
        if not isinstance(m, unreal.MaterialInstanceConstant):
            continue
        rec = {'path': path(m), 'parent': path(m.parent), 'scalar_overrides': {}, 'vector_overrides': {}, 'texture_overrides': {}}
        for param in m.get_editor_property('scalar_parameter_values'):
            rec['scalar_overrides'][str(param.parameter_info.name)] = param.parameter_value
        for param in m.get_editor_property('vector_parameter_values'):
            v = param.parameter_value
            rec['vector_overrides'][str(param.parameter_info.name)] = [v.r, v.g, v.b, v.a]
        for param in m.get_editor_property('texture_parameter_values'):
            p = path(param.parameter_value)
            rec['texture_overrides'][str(param.parameter_info.name)] = p
            if p:
                tex_paths.add(p)
        report['material_instances'].append(rec)
    for name, folder in (('M_Bark', 'Bark'), ('M_Leaf', 'Leaves'), ('M_Grass', 'Grass')):
        m = unreal.load_asset(BASE + '/Materials/' + folder + '/' + name)
        graph_raw = unreal.MaterialNodeService.export_material_graph(path(m))
        graph = json.loads(graph_raw) if graph_raw else None
        report['base_materials'][name] = {'path': path(m), 'blend': str(m.blend_mode),
            'shading': unreal.MaterialService.get_property(path(m), 'ShadingModel'),
            'two_sided': m.get_editor_property('two_sided'), 'graph': graph}
        for tex in unreal.MaterialEditingLibrary.get_used_textures(m):
            tex_paths.add(path(tex))
    for name in sorted(tex_paths):
        tex = unreal.load_asset(name)
        rec = {'path': name, 'class': tex.get_class().get_name()}
        for prop in ('compression_settings', 'srgb', 'mip_gen_settings', 'lod_group', 'alpha_coverage_thresholds', 'do_scale_mips_for_alpha_coverage'):
            try:
                rec[prop] = str(tex.get_editor_property(prop))
            except Exception:
                pass
        if isinstance(tex, unreal.Texture2D):
            rec['size'] = [tex.blueprint_get_size_x(), tex.blueprint_get_size_y()]
        report['textures'][name] = rec
    report['dirty_after'] = dirty()
    report['assets_saved'] = False
    report['success'] = report['dirty_before'] == report['dirty_after']
    (OUT / 'workload_inventory.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    brief = {'success': report['success'], 'pack_counts': report['pack_counts'], 'meshes': report['meshes'],
             'material_instances': report['material_instances'],
             'texture_count': len(report['textures']), 'dirty_before': report['dirty_before'], 'dirty_after': report['dirty_after']}
    unreal.MCPythonHelper.submit_result(json.dumps(brief, ensure_ascii=False))

collect()
