"""Duplicate the existing guide-line shader and disable terrain depth testing locally."""
import json
import traceback
from pathlib import Path

import unreal

SOURCE = '/Engine/EngineDebugMaterials/DebugMeshMaterial'
TARGET = '/Game/GuLiStrike/Rendering/CommanderGuideLines/M_CommanderRouteLineOverlay'
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'outputs/commander-green-guide-line-20261004'
REPORT = {'success': False, 'source': SOURCE, 'target': TARGET,
          'native_build_executed': False, 'runtime_verified': False}
STYLE_PROPERTIES = ['blend_mode', 'shading_model', 'material_domain', 'two_sided',
                    'translucency_pass']


def properties(material):
    return {name: str(material.get_editor_property(name)) for name in STYLE_PROPERTIES}


def run():
    source = unreal.load_asset(SOURCE)
    assert isinstance(source, unreal.Material)
    original = properties(source)
    original_depth = source.get_editor_property('disable_depth_test')
    assert source.get_editor_property('blend_mode') == unreal.BlendMode.BLEND_TRANSLUCENT
    assert not original_depth, 'The engine source should remain depth-tested.'
    created = not unreal.EditorAssetLibrary.does_asset_exist(TARGET)
    material = (unreal.EditorAssetLibrary.duplicate_asset(SOURCE, TARGET) if created
                else unreal.load_asset(TARGET))
    assert isinstance(material, unreal.Material)
    assert properties(material) == original, 'Keep the original guide-line style.'
    expressions = unreal.MaterialEditingLibrary.get_num_material_expressions(source)
    assert unreal.MaterialEditingLibrary.get_num_material_expressions(material) == expressions
    material.modify()
    material.set_editor_property('disable_depth_test', True)
    unreal.MaterialEditingLibrary.recompile_material(material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    assert material.get_editor_property('disable_depth_test')
    assert properties(source) == original and source.get_editor_property('disable_depth_test') == original_depth
    REPORT.update(success=True, created=created, saved=True, properties=properties(material),
                  disable_depth_test=True, expression_count=expressions,
                  source_unchanged=True, material_shader_recompile_requested=True,
                  dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])


try:
    run()
except Exception:
    REPORT['error'] = traceback.format_exc()
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'material-delivery.json').write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({k: REPORT[k] for k in
    ['success', 'saved', 'created', 'target', 'error'] if k in REPORT}, ensure_ascii=False))
