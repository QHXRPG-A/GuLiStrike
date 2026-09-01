"""Import selection icons and add the approved passive shortcut strip to the HUD.

Run through commander_editor_python.py --file after compiling native UI changes,
outside PIE. This script only saves the five icon textures and WBP_CommanderHUD.
It preserves the original 1120x204 dock inside a new 1120x260 scale wrapper.
"""

import json
from pathlib import Path

import unreal


ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUTPUT = ROOT / "outputs" / "commander-selection-20260831"
ASSETS = ROOT / "data" / "CommanderUI" / "Selection"
HUD_PATH = "/Game/Commander/UI/Widgets/WBP_CommanderHUD"
ICON_FOLDER = "/Game/Commander/UI/Textures/Icons"
HELPER = unreal.MCPythonHelper
CYAN = (0.094118, 0.843137, 1.0, 1.0)
PANEL = (0.012, 0.026, 0.040, 0.98)
TEXT = (0.88, 0.96, 1.0, 1.0)
ICON_FILES = {
    "Cursor": "cursor.png",
    "Box": "box.png",
    "Radius": "radius.png",
    "SameType": "same_type.png",
    "AddSelection": "add_selection.png",
}
# Matching native ShortcutEntries. More cells can be added without changing input routing.
SHORTCUTS = ["SameType", "AddSelection"]


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


def add(kind, name, parent, visible=False):
    widget = find(name)
    if not widget:
        checked(HELPER.umg_add_widget(hud, kind, name, parent))
        widget = find(name)
    checked(HELPER.umg_set_widget_is_variable(hud, name, True))
    is_panel = kind in ["CanvasPanel", "SizeBox"]
    widget.set_visibility(unreal.SlateVisibility.VISIBLE if visible else (
        unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE if is_panel
        else unreal.SlateVisibility.HIT_TEST_INVISIBLE))
    return widget


def layout(widget, x, y, width, height, z=0):
    slot = widget.get_editor_property("slot")
    if not isinstance(slot, unreal.CanvasPanelSlot):
        raise RuntimeError("Expected CanvasPanelSlot for " + widget.get_name())
    slot.set_anchors(unreal.Anchors(unreal.Vector2D(0, 0), unreal.Vector2D(0, 0)))
    slot.set_alignment(unreal.Vector2D(0, 0))
    slot.set_position(unreal.Vector2D(x, y))
    slot.set_size(unreal.Vector2D(width, height))
    slot.set_z_order(z)
    return widget


def fill(widget, margin=0):
    slot = widget.get_editor_property("slot")
    slot.set_anchors(unreal.Anchors(unreal.Vector2D(0, 0), unreal.Vector2D(1, 1)))
    slot.set_offsets(unreal.Margin(margin, margin, margin, margin))


def image(name, parent, x, y, width, height, color, z=0, texture=None):
    widget = layout(add("Image", name, parent), x, y, width, height, z)
    if texture:
        widget.set_brush_from_texture(texture, False)
    widget.set_color_and_opacity(unreal.LinearColor(*color))
    widget.set_render_opacity(1.0)
    return widget


def import_icons():
    tasks = []
    for suffix, filename in ICON_FILES.items():
        source = ASSETS / filename
        if not source.is_file():
            raise RuntimeError("Missing approved icon: " + str(source))
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(source))
        task.set_editor_property("destination_path", ICON_FOLDER)
        task.set_editor_property("destination_name", "T_UI_Cmd_" + suffix)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        # AssetTools.cpp (UE 5.7) selects Interchange only when SpecifiedFactory
        # is null. An explicit legacy factory avoids Interchange's synchronous
        # WaitUntilDone re-entering the game-thread TaskGraph from MCP callbacks.
        # Do not toggle a global importer CVar or persist engine configuration.
        task.set_editor_property("factory", unreal.TextureFactory())
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    result = {}
    for suffix, task in zip(ICON_FILES, tasks):
        texture = unreal.load_asset(ICON_FOLDER + "/T_UI_Cmd_" + suffix)
        if not isinstance(texture, unreal.Texture2D) or not task.get_editor_property("imported_object_paths"):
            raise RuntimeError("Texture import failed: " + suffix)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        texture.set_editor_property("filter", unreal.TextureFilter.TF_BILINEAR)
        texture.set_editor_property("srgb", True)
        texture.set_editor_property("never_stream", True)
        result[suffix] = texture
    return result


if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
    raise RuntimeError("Stop PIE before authoring selection HUD assets")
hud = unreal.load_asset(HUD_PATH)
if not hud:
    raise RuntimeError("Commander HUD is missing")
for required in ["RootCanvas", "C_DockRegion", "SC_DockScale", "SB_DockDesign", "C_Dock", "BTN_Cmd_Select"]:
    if not find(required):
        raise RuntimeError("Expected existing HUD widget: " + required)

icons = import_icons()

# Keep the old dock and every one of its child coordinates unchanged. The combined
# design canvas gets a 56px strip above it; the existing ScaleBox handles DPI/aspect.
dock = find("SB_DockDesign")
scale = find("SC_DockScale")
if not find("SB_CommandDockDesign"):
    if dock.get_parent() != scale:
        raise RuntimeError("Unexpected dock parent; refuse to reparent an unrelated layout")
    # Helper add/set-variable calls structurally compile the Blueprint. Never leave
    # the old dock detached across one: the compiler trashes unreachable widgets.
    # Stage the wrapper under RootCanvas, then atomically reparent each subtree.
    combined = add("SizeBox", "SB_CommandDockDesign", "RootCanvas")
    combined.set_width_override(1120)
    combined.set_height_override(260)
    canvas = add("CanvasPanel", "C_CommandDock", "SB_CommandDockDesign")
    checked(HELPER.umg_reparent_widget(hud, "SB_DockDesign", "C_CommandDock"))
    checked(HELPER.umg_reparent_widget(hud, "SB_CommandDockDesign", "SC_DockScale"))
    dock = find("SB_DockDesign")
    combined = find("SB_CommandDockDesign")
else:
    combined = find("SB_CommandDockDesign")
    canvas = find("C_CommandDock")
    if not canvas or dock.get_parent() != canvas:
        raise RuntimeError("Incomplete selection layout; inspect combined dock before retrying")
combined.set_width_override(1120)
combined.set_height_override(260)
layout(dock, 0, 56, 1120, 204)
region_slot = find("C_DockRegion").get_editor_property("slot")
anchors = region_slot.get_anchors()
if anchors.minimum.y != 1.0 or anchors.maximum.y != 1.0 or region_slot.get_alignment().y != 1.0:
    raise RuntimeError("Dock region must retain its bottom anchor and bottom alignment")
region_size = region_slot.get_size()
region_slot.set_size(unreal.Vector2D(region_size.x, 260))

strip = layout(add("SizeBox", "SB_ShortcutsDesign", "C_CommandDock"), 0, 0, 1120, 56, 1)
strip.set_width_override(1120)
strip.set_height_override(56)
add("CanvasPanel", "C_SelectionShortcuts", "SB_ShortcutsDesign")
image("I_ShortcutRail", "C_SelectionShortcuts", 0, 0, 1120, 56,
      (0.18, 0.45, 0.53, 0.86))
image("I_ShortcutPanel", "C_SelectionShortcuts", 1, 1, 1118, 54, PANEL, 1)
image("I_ShortcutAccent", "C_SelectionShortcuts", 18, 0, 1084, 1,
      (0.094118, 0.843137, 1.0, 0.64), 2)
for index, suffix in enumerate(SHORTCUTS):
    x = 18 + index * 52
    image("I_ShortcutFrame_" + suffix, "C_SelectionShortcuts", x, 6, 44, 44,
          (0.075, 0.19, 0.24, 0.90), 2)
    image("I_Shortcut_" + suffix, "C_SelectionShortcuts", x + 2, 8, 40, 40,
          (1, 1, 1, 1), 3, icons[suffix])
    button = layout(add("Button", "BTN_Shortcut_" + suffix, "C_SelectionShortcuts", True),
                    x, 6, 44, 44, 10)
    # Existing command buttons already use transparent brushes and do not take focus.
    button.set_style(find("BTN_Cmd_Select").get_editor_property("widget_style"))
    button.set_background_color(unreal.LinearColor(0, 0, 0, 0))
    button.set_editor_property("is_focusable", False)
    button.set_is_enabled(True)
    button.set_render_opacity(1.0)

# The image is swapped by the native shape-change event, replacing the old UMG glyph.
for corner in ["TL_H", "TL_V", "TR_H", "TR_V", "BL_H", "BL_V", "BR_H", "BR_V", "Center", "ContextLink"]:
    remove("I_CmdSelect" + corner)
image("I_CmdIcon_Select", "C_Dock", 846, 85, 32, 32, (1, 1, 1, 1), 10, icons["Box"])
find("TXT_CmdLabel_Select").set_text("框选")
find("TXT_CmdKey_Select").set_text("7")

# Keep switched/native-only icons referenced by the saved HUD for cooking too.
for suffix in ["Radius", "Cursor"]:
    reference = image("I_SelectionAsset_" + suffix, "C_SelectionShortcuts", 0, 0, 1, 1,
                      (1, 1, 1, 1), 0, icons[suffix])
    reference.set_visibility(unreal.SlateVisibility.COLLAPSED)

# Native hover handlers set text, measure, and clamp this panel in RootCanvas space.
# Unlike desktop Slate tooltips, this cannot escape a windowed PIE/game viewport.
tooltip = layout(add("CanvasPanel", "C_SelectionTooltip", "RootCanvas"), 0, 0, 368, 192, 500)
tooltip.set_editor_property("clipping", unreal.WidgetClipping.CLIP_TO_BOUNDS)
border = image("I_SelectionTooltipBorder", "C_SelectionTooltip", 0, 0, 368, 192,
               (0.12, 0.56, 0.67, 1), 0)
fill(border)
background = image("I_SelectionTooltipBackground", "C_SelectionTooltip", 1, 1, 366, 190,
                   (0.010, 0.024, 0.038, 1), 1)
fill(background, 1)
caption = layout(add("TextBlock", "TXT_SelectionTooltip", "C_SelectionTooltip"), 12, 12, 344, 168, 2)
fill(caption, 12)
caption.set_text("")
caption.set_auto_wrap_text(False)
caption.set_editor_property("wrap_text_at", 344)
checked(HELPER.umg_set_text_style(hud, "TXT_SelectionTooltip", 13, *TEXT, 0))
tooltip.set_visibility(unreal.SlateVisibility.COLLAPSED)

compiled = checked(HELPER.compile_blueprint(hud))
saved = [*icons.values(), hud]
for asset in saved:
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Could not save " + asset.get_path_name())
tree = checked(HELPER.umg_get_widget_info(hud))
report = {
    "success": True,
    "compiled": compiled,
    "saved_assets": [asset.get_path_name() for asset in saved],
    "dock_design": [1120, 204],
    "combined_design": [1120, 260],
    "shortcut_design": [1120, 56],
    "shortcut_entries": SHORTCUTS,
    "cursor_hotspot_normalized": [0.064, 0.012],
    "texture_filter": "bilinear_no_mips_ui_rgba",
    "texture_importer": "explicit_native_TextureFactory",
    "widget_count": tree["widget_count"],
    "widgets": tree["widgets"],
}
OUTPUT.mkdir(parents=True, exist_ok=True)
(OUTPUT / "selection-hud-build.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.MCPythonHelper.submit_result(json.dumps({key: value for key, value in report.items() if key != "widgets"}))
