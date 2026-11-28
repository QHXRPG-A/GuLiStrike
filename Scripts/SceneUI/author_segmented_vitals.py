"""Rebuild the shared UI-domain material for rounded health and shield markers."""
from pathlib import Path

import unreal


def build():
    folder = "/Game/GuLiStrike/UI/SceneOverlay"
    name = "M_SegmentedVitals"
    path = f"{folder}/{name}"
    lib = unreal.EditorAssetLibrary
    edit = unreal.MaterialEditingLibrary
    material = (lib.load_asset(path) if lib.does_asset_exist(path) else
                unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew()))
    assert material, path
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("num_customized_u_vs", 5)
    edit.delete_all_material_expressions(material)
    nodes = {}
    for pin, channel in (("UV", 0), ("Fractions", 1), ("PackedSize", 3), ("CellSpans", 4)):
        node = edit.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate)
        node.set_editor_property("coordinate_index", channel)
        nodes[pin] = (node, "")
    vertex = edit.create_material_expression(material, unreal.MaterialExpressionVertexColor)
    nodes.update({"Fade": (vertex, "A"), "ShieldFade": (vertex, "R")})
    shader = edit.create_material_expression(material, unreal.MaterialExpressionCustom)
    inputs = []
    for pin in nodes:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", pin)
        inputs.append(item)
    shader.set_editor_property("inputs", inputs)
    shader.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    shader.set_editor_property("code", (project / "ArtSource/UI/SegmentedVitals/RoundedVitals.hlsl").read_text(encoding="utf-8"))
    for pin, (node, output) in nodes.items():
        assert edit.connect_material_expressions(node, output, shader, pin), pin
    for alpha, prop in ((False, unreal.MaterialProperty.MP_EMISSIVE_COLOR), (True, unreal.MaterialProperty.MP_OPACITY)):
        mask = edit.create_material_expression(material, unreal.MaterialExpressionComponentMask)
        for component in "rgba":
            mask.set_editor_property(component, (component == "a") == alpha)
        assert edit.connect_material_expressions(shader, "", mask, "")
        assert edit.connect_material_property(mask, "", prop)
    edit.layout_material_expressions(material)
    edit.recompile_material(material)
    assert lib.save_loaded_asset(material, False)
    return material.get_path_name()


if __name__ == "__main__":
    unreal.log(build())
