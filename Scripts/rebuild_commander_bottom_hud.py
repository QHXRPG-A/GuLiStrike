"""Author the approved commander bottom dock through native UnrealMCPython.

Run in the editor, outside PIE, with commander_editor_python.py --file.
Only the HUD and three small title-material assets are saved. Gameplay state,
the top status bar, minimap and the existing command slots are not rebuilt.
"""

import json
from pathlib import Path

import unreal


HUD_PATH = "/Game/Commander/UI/Widgets/WBP_CommanderHUD"
MATERIAL_FOLDER = "/Game/Commander/UI/Materials"
OUTPUT = Path("D:/UE5.7/test1/outputs/commander-ring-hud-20260830")
HELPER = unreal.MCPythonHelper
CYAN = (0.094118, 0.843137, 1.0, 1.0)
WHITE = (0.929412, 0.972549, 1.0, 1.0)
MUTED = (0.46, 0.64, 0.72, 1.0)
METAL = (0.819608, 0.686275, 0.52549, 0.48)


def checked(raw):
    result = json.loads(raw)
    if not result.get("success"):
        raise RuntimeError(str(result))
    return result


def find(name):
    return HELPER.umg_find_widget(hud, name)


def remove(name):
    if find(name):
        checked(HELPER.umg_remove_widget(hud, name))


def layout(widget, x, y, width, height, z=0):
    slot = widget.get_editor_property("slot")
    slot.set_position(unreal.Vector2D(x, y))
    slot.set_size(unreal.Vector2D(width, height))
    slot.set_z_order(z)
    return widget


def add(kind, name, parent, x, y, width, height, z=0):
    checked(HELPER.umg_add_widget(hud, kind, name, parent))
    widget = find(name)
    widget.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
    checked(HELPER.umg_set_widget_is_variable(hud, name, True))
    return layout(widget, x, y, width, height, z)


def image(name, parent, x, y, width, height, color, z=0):
    widget = add("Image", name, parent, x, y, width, height, z)
    widget.set_color_and_opacity(unreal.LinearColor(*color))
    return widget


def text(name, parent, x, y, width, height, value, size=12, color=WHITE, right=False):
    widget = add("TextBlock", name, parent, x, y, width, height, 4)
    widget.set_text(value)
    checked(HELPER.umg_set_text_style(hud, name, size, *color, 0))
    font = widget.get_editor_property("font")
    font.set_editor_property("typeface_font_name", "Bold" if size >= 12 else "Regular")
    font.set_editor_property("letter_spacing", 0)
    widget.set_font(font)
    if right:
        widget.set_editor_property("justification", unreal.TextJustify.RIGHT)
    return widget


def expression(material, expression_type, x, y, **properties):
    node = unreal.MaterialEditingLibrary.create_material_expression(material, expression_type, x, y)
    for key, value in properties.items():
        node.set_editor_property(key, value)
    return node


def title_materials():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    name = "M_UI_Cmd_SlantedTitle"
    material = unreal.load_asset(MATERIAL_FOLDER + "/" + name)
    if not material:
        material = tools.create_asset(name, MATERIAL_FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    uv = expression(material, unreal.MaterialExpressionTextureCoordinate, -650, 0)
    width = expression(material, unreal.MaterialExpressionScalarParameter, -650, 130,
                       parameter_name="Width", default_value=130.0)
    height = expression(material, unreal.MaterialExpressionScalarParameter, -650, 260,
                        parameter_name="Height", default_value=22.0)
    custom = expression(material, unreal.MaterialExpressionCustom, -300, 0,
                        output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT4,
                        description="Pixel-sized left-vertical, right-45-degree title plate",
                        code="""float2 p = UV * float2(Width, Height);
float d = min(min(p.x, min(p.y, Height-p.y)), (Width-p.x-p.y)*0.70710678);
float a = saturate(d + 0.5);
float edge = 1.0-saturate(d-0.6);
float3 fill = float3(0.018, 0.042, 0.061);
float3 border = float3(0.094118, 0.843137, 1.0)*0.65;
return float4(lerp(fill, border, edge), a);""")
    inputs = []
    for input_name in ["UV", "Width", "Height"]:
        item = unreal.CustomInput()
        item.set_editor_property("input_name", input_name)
        inputs.append(item)
    custom.set_editor_property("inputs", inputs)
    for node, input_name in [(uv, "UV"), (width, "Width"), (height, "Height")]:
        if not unreal.MaterialEditingLibrary.connect_material_expressions(node, "", custom, input_name):
            raise RuntimeError("Could not connect title input " + input_name)
    alpha = expression(material, unreal.MaterialExpressionComponentMask, 0, 150,
                       r=False, g=False, b=False, a=True)
    # ComponentMask's sole pin is unnamed (NAME_None), not the label "Input".
    if not unreal.MaterialEditingLibrary.connect_material_expressions(custom, "", alpha, ""):
        raise RuntimeError("Could not connect title alpha mask")
    if not unreal.MaterialEditingLibrary.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("Could not connect title color")
    if not unreal.MaterialEditingLibrary.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError("Could not connect title opacity")
    unreal.MaterialEditingLibrary.recompile_material(material)
    result = {}
    for suffix, pixel_width in [("Squads", 130.0), ("Commands", 166.0)]:
        instance_name = "MI_UI_Cmd_SlantedTitle_" + suffix
        instance = unreal.load_asset(MATERIAL_FOLDER + "/" + instance_name)
        if not instance:
            instance = tools.create_asset(instance_name, MATERIAL_FOLDER,
                                          unreal.MaterialInstanceConstant,
                                          unreal.MaterialInstanceConstantFactoryNew())
        unreal.MaterialEditingLibrary.set_material_instance_parent(instance, material)
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(instance, "Width", pixel_width)
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(instance, "Height", 22.0)
        unreal.MaterialEditingLibrary.update_material_instance(instance)
        result[suffix] = instance
    return material, result


if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
    raise RuntimeError("Stop PIE before authoring the HUD asset")
OUTPUT.mkdir(parents=True, exist_ok=True)
hud = unreal.load_asset(HUD_PATH)
if not hud:
    raise RuntimeError("Commander HUD asset is missing")

# Remove only the obsolete squad presentation and our own previous revision.
old_tree = checked(HELPER.umg_get_widget_info(hud))
for row in reversed(old_tree["widgets"]):
    name = row["name"]
    obsolete_squad = (name.startswith(("I_Squad", "TXT_Squad")) and name != "TXT_SquadsTitle")
    own_revision = name.startswith(("I_UnitType", "TXT_UnitType", "C_UnitType", "I_TitlePlate_"))
    obsolete_adjust = name.startswith(("I_CmdSelectAdjust", "TXT_CmdSelectAdjust"))
    if obsolete_squad or own_revision or obsolete_adjust or name in ["BTN_Cmd_SelectSize", "TXT_VisualOnly"]:
        remove(name)

material, title_instances = title_materials()
for suffix, title_name, caption, x, width in [
    ("Squads", "TXT_SquadsTitle", "编队", 18.0, 130.0),
    ("Commands", "TXT_CommandTitle", "指令矩阵", 764.0, 166.0),
]:
    plate = image("I_TitlePlate_" + suffix, "C_Dock", x, 5, width, 22, (1, 1, 1, 1), 2)
    plate.set_brush_from_material(title_instances[suffix])
    caption_widget = find(title_name)
    layout(caption_widget, x + 12, 7, width - 38, 19, 5)
    caption_widget.set_text(caption)
    caption_widget.set_editor_property("justification", unreal.TextJustify.LEFT)
    checked(HELPER.umg_set_text_style(hud, title_name, 12, *CYAN, 0))
    font = caption_widget.get_editor_property("font")
    font.set_editor_property("letter_spacing", 0)
    caption_widget.set_font(font)

add("CanvasPanel", "C_UnitTypeCard", "C_Dock", 18, 34, 414, 146, 1)
image("I_UnitTypeOuter", "C_UnitTypeCard", 0, 0, 414, 146, METAL)
image("I_UnitTypePanel", "C_UnitTypeCard", 1, 1, 412, 144, (0.018, 0.035, 0.055, 0.97))
image("I_UnitTypeAccent", "C_UnitTypeCard", 0, 0, 3, 146, CYAN, 1)
image("I_UnitTypePortrait", "C_UnitTypeCard", 14, 14, 72, 94, (0.024, 0.079, 0.105, 1))

# An inexpensive UMG glyph for the one existing four-legged soldier archetype.
for index, (x, y, w, h) in enumerate([
    (40, 44, 20, 27), (46, 26, 8, 20), (43, 25, 14, 4),
    (31, 47, 9, 5), (60, 47, 9, 5), (31, 65, 9, 5), (60, 65, 9, 5),
    (27, 41, 5, 20), (68, 41, 5, 20), (27, 66, 5, 21), (68, 66, 5, 21),
]):
    image("I_UnitTypeGlyph_%02d" % index, "C_UnitTypeCard", x, y, w, h, CYAN, 2)
image("I_UnitTypeGlyphSensor", "C_UnitTypeCard", 45, 49, 10, 6, (0.018, 0.035, 0.055, 1), 3)
text("TXT_UnitTypeCaption", "C_UnitTypeCard", 14, 114, 72, 18, "已选部队", 9, MUTED)
text("TXT_UnitTypeName", "C_UnitTypeCard", 106, 12, 176, 26, "士兵", 17)
text("TXT_UnitTypeCount", "C_UnitTypeCard", 284, 10, 112, 29, "25 人", 20, CYAN, True)
status = text("TXT_UnitTypeStatus", "C_UnitTypeCard", 106, 48, 286, 36, "待命", 10, MUTED)
status.set_auto_wrap_text(True)
text("TXT_UnitTypeHealthCaption", "C_UnitTypeCard", 106, 93, 60, 18, "生命", 10, MUTED)
text("TXT_UnitTypeHealth", "C_UnitTypeCard", 166, 93, 226, 18, "2500 / 2500", 11, WHITE, True)
image("I_UnitTypeHealthTrack", "C_UnitTypeCard", 106, 118, 286, 12, (0, 0, 0, 1))
fill = image("I_UnitTypeHealthFill", "C_UnitTypeCard", 106, 119, 286, 10,
             (0.894118, 0.278431, 0.345098, 1), 1)
fill.set_render_transform_pivot(unreal.Vector2D(0, 0.5))

# Slot 07 is an ordinary empty cell again, matching adjacent empty slot 08.
for prefix in ["I_CmdOuter_", "I_CmdBG_"]:
    target, template = find(prefix + "07"), find(prefix + "08")
    if target and template:
        target.set_brush(template.get_editor_property("brush"))
        target.set_color_and_opacity(template.get_editor_property("color_and_opacity"))
        target.set_render_opacity(template.get_render_opacity())
find("TXT_CoreSystem").set_text("指挥官 · 在线")

compile_result = checked(HELPER.compile_blueprint(hud))
assets = [material, *title_instances.values(), hud]
for asset in assets:
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Could not save " + asset.get_path_name())
final_tree = checked(HELPER.umg_get_widget_info(hud))
report = {"success": True, "compiled": compile_result,
          "saved_assets": [a.get_path_name() for a in assets],
          "widget_count": final_tree["widget_count"], "widgets": final_tree["widgets"]}
(OUTPUT / "hud-build.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.MCPythonHelper.submit_result(json.dumps({k: v for k, v in report.items() if k != "widgets"}))
