"""Create the shared fully clipped surface used by non-near model outline slots."""
import json
import unreal

path = "/Game/GuLiStrike/Presentation/Materials/M_NoModelOutline"
material = unreal.load_asset(path)
created = material is None
if created:
    material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "M_NoModelOutline", "/Game/GuLiStrike/Presentation/Materials",
        unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("used_with_skeletal_mesh", True)
    zero = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionConstant, -200, 0)
    zero.set_editor_property("r", 0.0)
    assert unreal.MaterialEditingLibrary.connect_material_property(
        zero, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    unreal.MaterialEditingLibrary.recompile_material(material)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material, False)
unreal.MCPythonHelper.submit_result(json.dumps({
    "success": True, "created": created, "material": material.get_path_name()}))
