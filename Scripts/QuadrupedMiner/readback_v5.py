"""Read saved production references, shader statistics and rendered geometry only."""
import json
import math
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'ArtSource/QuadrupedMiner_10k_v5'
BASE = '/Game/GuLiStrike/Commander/Units/ElectromagneticMiner/QuadrupedMiner_10k_v5'
META = json.loads((OUT / 'metadata.json').read_text(encoding='utf8'))
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor.get_game_world()
bp = unreal.load_asset(BASE + '/BP_QuadrupedMiner')
cdo = unreal.get_default_object(bp.generated_class())
body = cdo.get_editor_property('body_vat')
mesh = body.get_editor_property('static_mesh')
data = body.get_editor_property('animation')
assert mesh.get_path_name().startswith(BASE + '/Meshes/')
assert data.is_valid_data() and data.get_editor_property('source_sha256') == META['source_sha256']
assert data.get_editor_property('total_frames') == 103
assert len(data.get_editor_property('muzzle_samples')) == 103
for actual, expected in zip(data.get_editor_property('muzzle_samples'), META['muzzle_samples_cm']):
    assert math.dist(actual.to_tuple(), expected) < .001

report = dict(success=False, blueprint=bp.generated_class().get_path_name(), mesh=mesh.get_path_name(),
              animation=data.get_path_name(), source_sha256=META['source_sha256'],
              reference_muzzle=list(data.get_editor_property('reference_muzzle').to_tuple()),
              muzzle_samples=103, render_bounds=data.get_editor_property('render_bounds').export_text(),
              mesh_bounds=mesh.get_bounding_box().export_text(), materials={}, textures=[], tables={}, gameplay_started=False)
report['geometry'] = json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(mesh))
assert [x['triangles'] for x in report['geometry']['render_lods']] == [9854, 4614, 2676]
for i, row in enumerate(META['lods']):
    description = mesh.get_static_mesh_description(i)
    assert description.get_vertex_count() == row['vertices']
    assert description.get_vertex_instance_count() == row['normal_count']
    source = json.loads((OUT / f'lod{i}.json').read_text())
    for j, expected in enumerate(source['positions']):
        actual = description.get_vertex_position(unreal.VertexID(j))
        assert math.dist(actual.to_tuple(), expected) < .001
    for j in range(row['normal_count']):
        corner = unreal.VertexInstanceID(j)
        for channel in [0, 2]:
            actual = description.get_vertex_instance_uv(corner, channel)
            assert math.dist(actual.to_tuple(), source['uvs'][channel][j]) < .0001
    for role in ['Position', 'Normal']:
        texture = unreal.load_asset(BASE + f'/Textures/T_{role}_LOD{i}')
        size = [texture.blueprint_get_size_x(), texture.blueprint_get_size_y()]
        assert size == [row[role.lower()]['width'], row[role.lower()]['height']]
        assert not texture.get_editor_property('srgb')
        assert texture.get_editor_property('mip_gen_settings') == unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS
        assert texture.get_editor_property('filter') == unreal.TextureFilter.TF_NEAREST
        report['textures'].append(dict(path=texture.get_path_name(), size=size, compression=str(texture.get_editor_property('compression_settings'))))
for name in ['Body', 'Outline', 'Lamp', 'Hit', 'Wreck']:
    material = unreal.load_asset(BASE + '/Materials/M_QuadrupedMiner_' + name)
    stats = unreal.MaterialEditingLibrary.get_statistics(material)
    assert stats.num_vertex_shader_instructions > 0 and stats.num_pixel_shader_instructions > 0
    assert stats.num_samplers >= 6
    params = [str(p) for p in unreal.MaterialEditingLibrary.get_scalar_parameter_names(material)]
    if name == 'Lamp': assert 'GuLi_TeamEnabled' in params and 'GuLi_TeamLightStrength' in params
    else: assert 'GuLi_TeamEnabled' not in params
    report['materials'][name] = dict(vertex_instructions=stats.num_vertex_shader_instructions,
                                    pixel_instructions=stats.num_pixel_shader_instructions,
                                    sampler_count=stats.num_samplers, scalar_parameters=params)
report['cpd'] = {name: body.get_custom_primitive_data_index_for_scalar_parameter(name) for name in [
    'GuLi_TeamEnabled', 'GuLi_TeamLightStrength', 'GuLi_MinerFrameA', 'GuLi_MinerFrameB', 'GuLi_MinerBlend',
    'GuLi_MinerPreviousFrameA', 'GuLi_MinerPreviousFrameB', 'GuLi_MinerPreviousBlend']}
assert list(report['cpd'].values()) == [16, 17, 19, 20, 21, 22, 23, 24]
for name, index in [('GuLi_TeamPrimary', 8), ('GuLi_TeamSecondary', 12)]:
    assert body.get_custom_primitive_data_index_for_vector_parameter(name) == index
    report['cpd'][name] = index
for table in ['Models_Models', 'Models_Parts', 'Models_MaterialParameters', 'Models_ColorRegions', 'Commander_Soldiers']:
    asset = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrike' + table)
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(asset))
    field = 'Id' if table in ['Models_Models', 'Commander_Soldiers'] else 'ModelId'
    identities = [3] if table == 'Commander_Soldiers' else [1003, 8017]
    report['tables'][table] = [r for r in rows if r[field] in identities]
model = next(r for r in report['tables']['Models_Models'] if r['Id'] == 1003)
assert model['ResourcePath'] == report['blueprint'] and model['bTeamColorEnabled']
assert model['BluePrimaryHex'] == '#6AA4BE' and model['EnemyPrimaryHex'] == '#A34053'
assert len(report['tables']['Models_Parts']) == 1
assert report['tables']['Models_Parts'][0]['ComponentPath'] == 'BodyVAT'
assert report['tables']['Models_Parts'][0]['ChildModelId'] == 8017
assert len(report['tables']['Models_ColorRegions']) == 4
assert all(r['PaintRole'] == 7 for r in report['tables']['Models_ColorRegions'])
parameters = report['tables']['Models_MaterialParameters']
assert len(parameters) == 4 and next(r for r in parameters if r['ParameterKey'] == 'TeamLightStrength')['DefaultScalar'] == 1.8
unit = report['tables']['Commander_Soldiers'][0]
assert unit['ModelId'] == 1003 and unit['MovementSpeedCmPerSecond'] == 900 and unit['ModelWidthMeters'] == 3.6
assert math.isclose(unit['PresentationScale'], META['presentation_scale'], rel_tol=1e-5)  # UE JSON precision.
assert unit['ActorClass'] == '/Script/GuLiStrike.GuLiMiningVehiclePawn'
for registry in unreal.ObjectIterator(unreal.GuLiModelRegistrySubsystem):
    assert registry.reload_catalog()
    assert registry.load_presentation_class(1003) == bp.generated_class()

# Export actual built render buffers (all LODs), never an image, for numeric readback.
task = unreal.AssetExportTask()
task.object = mesh; task.filename = str(OUT / 'UE_RenderMesh_Readback.fbx')
task.automated = True; task.prompt = False; task.replace_identical = True; task.exporter = unreal.StaticMeshExporterFBX()
options = unreal.FbxExportOption()
for key, value in dict(ascii=False, collision=False, vertex_color=True, level_of_detail=True,
                       export_source_mesh=False, force_front_x_axis=True).items(): options.set_editor_property(key, value)
task.options = options
assert unreal.Exporter.run_asset_export_task(task)
report['success'] = True
(OUT / 'ue-readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True, triangles=[x['triangles'] for x in report['geometry']['render_lods']],
    shaders=list(report['materials']), model1003=model['ResourcePath'], cpd=report['cpd'])))
