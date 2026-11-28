"""Import the native instrument atlas, reference numeral font and depth-tested surface ink."""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
DEST = "/Game/GuLiStrike/Ship/UI/Xuntian"
ART = ROOT / "ArtSource/UI/Ship/XuntianHUD_20261125/Native"
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
ME = unreal.MaterialEditingLibrary


def import_asset(name, source, factory):
    task = unreal.AssetImportTask()
    task.filename = str(source)
    task.destination_path = DEST
    task.destination_name = name
    task.automated = True
    task.replace_existing = True
    task.save = False
    task.factory = factory
    TOOLS.import_asset_tasks([task])
    value = unreal.load_asset(DEST + "/" + name)
    assert value, name
    return value


def asset(name, cls, factory):
    path = DEST + "/" + name
    return unreal.load_asset(path) if LIB.does_asset_exist(path) else TOOLS.create_asset(name, DEST, cls, factory)


def linear_color(s):
    channels = [int(s[i:i+2], 16) / 255.0 for i in (0, 2, 4)]
    return unreal.LinearColor(*[v/12.92 if v <= .04045 else ((v+.055)/1.055)**2.4 for v in channels], 1.0)


def guide_material(name, shader, colors, ground=False, marker=False):
    material = asset(name, unreal.Material, unreal.MaterialFactoryNew())
    ME.delete_all_material_expressions(material)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    if marker:
        nodes = {"UV": ME.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate),
                 "Style": ME.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate),
                 "Ink": ME.create_material_expression(material, unreal.MaterialExpressionVertexColor)}
        nodes["Style"].set_editor_property("coordinate_index", 1)
    else:
        nodes = {"WorldPoint": ME.create_material_expression(material, unreal.MaterialExpressionWorldPosition),
                 "Eye": ME.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS)}
    for name, srgb in colors.items():
        n = ME.create_material_expression(material, unreal.MaterialExpressionConstant3Vector)
        n.set_editor_property("constant", linear_color(srgb))
        nodes[name] = n
    if ground:
        nodes["UV"] = ME.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate)
        reach = ME.create_material_expression(material, unreal.MaterialExpressionScalarParameter)
        reach.set_editor_property("parameter_name", "Reach")
        reach.set_editor_property("default_value", 190000.0)
        nodes["Reach"] = reach
    ink = ME.create_material_expression(material, unreal.MaterialExpressionCustom)
    ink.set_editor_property("description", shader)
    ink.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    inputs = []
    for name in nodes:
        inp = unreal.CustomInput()
        inp.set_editor_property("input_name", name)
        inputs.append(inp)
    ink.set_editor_property("inputs", inputs)
    ink.set_editor_property("code", (ART / shader).read_text(encoding="utf-8"))
    for name, node in nodes.items():
        assert ME.connect_material_expressions(node, "", ink, name), name
    for prop, channels in ((unreal.MaterialProperty.MP_EMISSIVE_COLOR, "rgb"), (unreal.MaterialProperty.MP_OPACITY, "a")):
        mask = ME.create_material_expression(material, unreal.MaterialExpressionComponentMask)
        for channel in "rgba":
            mask.set_editor_property(channel, channel in channels)
        assert ME.connect_material_expressions(ink, "", mask, "")
        assert ME.connect_material_property(mask, "", prop)
    ME.layout_material_expressions(material)
    ME.recompile_material(material)
    return material


def air_marker_material():
    name, shader = "M_XuntianAirMarker", "AirMarker.hlsl"
    path = f"{DEST}/{name}"
    if LIB.does_asset_exist(path):
        material = unreal.load_asset(path)
        # The pilot's class default can already hold this graph. Update the authored expression in place.
        nodes = [node for node in unreal.ObjectIterator(unreal.MaterialExpressionCustom)
                 if node.get_outermost() == material.get_outermost() and node.get_editor_property("description") == shader]
        if len(nodes) != 1:
            raise RuntimeError(f"Expected one {shader} expression in {path}, found {len(nodes)}")
        source = (ART / shader).read_text(encoding="utf-8")
        if nodes[0].get_editor_property("code") != source:
            nodes[0].set_editor_property("code", source)
            ME.recompile_material(material)
    else:
        material = guide_material(name, shader, {"DarkInk": "2C3735"}, marker=True)
    check = json.loads(unreal.GuLiHybridAuthoringLibrary.inspect_xuntian_marker_material(material))
    if not check["ready"]:
        raise RuntimeError(f"Marker shader failed for {material.get_path_name()}: {check}")
    return material


def run():
    atlas = import_asset("T_XuntianInstruments", ART / "T_XuntianInstruments.png", unreal.TextureFactory())
    atlas.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    atlas.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    atlas.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    atlas.set_editor_property("srgb", True)
    atlas.set_editor_property("never_stream", True)
    face = import_asset("FF_XuntianPanelSemiBold", ROOT / "ArtSource/UI/HUDuiPRO/Fonts/Rajdhani-SemiBold.ttf",
                        unreal.FontFileImportFactory())
    font = asset("F_XuntianPanel", unreal.Font, unreal.FontFactory())
    error = unreal.GuLiGroundHUDAssetLibrary.configure_font(font, face, face)
    assert not error, error
    ink = guide_material("M_XuntianCoverage", "GroundCoverage.hlsl",
                         {"OpenInk": "EE9D58", "BlindInk": "EABCBA", "DarkInk": "2C3735"}, ground=True)
    sky = guide_material("M_XuntianAirGuide", "AirCoverage.hlsl",
                         {"OpenInk": "6AA4BE", "RimInk": "FEE4D9", "DarkInk": "2C3735"})
    for obj in (atlas, face, font, ink, sky, air_marker_material()):
        assert LIB.save_loaded_asset(obj, only_if_is_dirty=False), obj.get_path_name()
    unreal.log("XUNTIAN_INSTRUMENT_ASSETS " + " ".join(o.get_path_name() for o in (atlas, font, ink, sky)))


if __name__ == "__main__":
    run()
