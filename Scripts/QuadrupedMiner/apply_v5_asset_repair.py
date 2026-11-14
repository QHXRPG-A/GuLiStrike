"""Save the winding/scale repair after the user has authorized ending PIE.

The Hybrid logic process holds the mesh file open while playing. This script
never stops or starts gameplay itself; it requires that session to be ended.
"""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'ArtSource/QuadrupedMiner_10k_v5'
BASE = '/Game/GuLiStrike/Commander/Units/ElectromagneticMiner/QuadrupedMiner_10k_v5'
META = json.loads((OUT / 'metadata.json').read_text(encoding='utf8'))
assert unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None
assert META['winding'] == 'ue_clockwise' and META['presentation_size_multiplier'] == 2.5
mesh = unreal.load_asset(BASE + '/Meshes/SM_QuadrupedMiner_VAT')
assert mesh.get_path_name() == BASE + '/Meshes/SM_QuadrupedMiner_VAT.SM_QuadrupedMiner_VAT'
data = unreal.load_asset(BASE + '/Animations/DA_QuadrupedMiner_VAT')
assert data.get_editor_property('source_sha256') == META['source_sha256']
assert unreal.GuLiQuadrupedMinerVATData.build_approved_mesh(mesh, str(OUT)) == 'OK'
assert [mesh.get_num_triangles(i) for i in range(3)] == [9854, 4614, 2676]
# Save only the two repaired content packages, leaving other dirty assets alone.
assert unreal.EditorLoadingAndSavingUtils.save_packages([mesh.get_outermost()], False)

table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers')
previous = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
source = (ROOT / 'Data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf8')
row_struct = unreal.load_object(None, '/Script/GuLiStrike.GuLiStrikeCommanderSoldiersRow')
assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, source, row_struct)
actual = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
assert [r for r in actual if r['Id'] != 3] == [r for r in previous if r['Id'] != 3]
miner = next(r for r in actual if r['Id'] == 3)
old_miner = next(r for r in previous if r['Id'] == 3)
assert all(miner[k] == v for k, v in old_miner.items() if k not in ['PresentationScale', 'Note'])
assert abs(miner['PresentationScale'] - META['presentation_scale']) < 1e-5  # JSON exports six significant digits.
assert unreal.EditorLoadingAndSavingUtils.save_packages([table.get_outermost()], False)

task = unreal.AssetExportTask()
task.object = mesh
task.filename = str(OUT / 'UE_RenderMesh_Readback.fbx')
task.automated = True
task.prompt = False
task.replace_identical = True
task.exporter = unreal.StaticMeshExporterFBX()
options = unreal.FbxExportOption()
for key, value in dict(ascii=False, collision=False, vertex_color=True, level_of_detail=True,
                       export_source_mesh=False, force_front_x_axis=True).items():
    options.set_editor_property(key, value)
task.options = options
assert unreal.Exporter.run_asset_export_task(task)
report = dict(success=True, triangles=[mesh.get_num_triangles(i) for i in range(3)],
              winding='ue_clockwise', presentation_scale=miner['PresentationScale'],
              speed_cm_s=miner['MovementSpeedCmPerSecond'], other_soldiers_unchanged=True,
              pie_running=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None,
              mesh_saved=True, table_saved=True, visual_inspection=False)
(OUT / 'asset-repair.json').write_text(json.dumps(report, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report))
