"""Build the Gate-2 NEONCTRL Ship HUD and reskin Commander in place.

Run through ``commander_editor_python.py --file`` with the editor open and PIE
stopped.  The script saves only WBP_ShipHUD and WBP_CommanderHUD.  It does not
add Blueprint gameplay logic, bindings, animations, input modes, or focusable
controls.
"""

import json
from pathlib import Path

import unreal


SHIP_FOLDER = "/Game/Ship/UI/Widgets"
SHIP_PATH = SHIP_FOLDER + "/WBP_ShipHUD"
COMMANDER_PATH = "/Game/Commander/UI/Widgets/WBP_CommanderHUD"
KIT = "/Game/NEONCTRL_FuturisticClea_UIKit"
OUTPUT = Path("D:/UE5.7/test1/outputs/review/neonctrl-ui")
HELPER = unreal.MCPythonHelper

WHITE = (0.929, 0.973, 1.0, 1.0)
CYAN = (0.07, 0.84, 1.0, 1.0)
MUTED = (0.46, 0.64, 0.72, 1.0)
GOLD = (1.0, 0.74, 0.12, 1.0)
RED = (0.90, 0.28, 0.35, 1.0)
PANEL = (0.035, 0.10, 0.14, 0.94)
PANEL_DARK = (0.015, 0.055, 0.085, 0.96)

TEXTURES = {
    "background": KIT + "/PNG/Commom/T_Background",
    "edge": KIT + "/PNG/Gameplay/T_EdgeFrame-Color",
    "details": KIT + "/PNG/Inventory/T_DetailsField",
    "inventory": KIT + "/PNG/Inventory/T_InventorySlot",
    "inventory_selected": KIT + "/PNG/Inventory/T_InventorySlotSelected",
    "slot": KIT + "/PNG/Gameplay/T_Slot",
    "utility": KIT + "/PNG/Gameplay/T_Utility_Slot",
    "weapon_slot": KIT + "/PNG/Gameplay/T_WeaponSlot",
    "bar_fill": KIT + "/PNG/Gameplay/T_BarFill-Color",
    "bar_track": KIT + "/PNG/Gameplay/T_BarNoFill-Color",
    "vector_h": KIT + "/PNG/Gameplay/T_VectorLine1",
    "vector_v": KIT + "/PNG/Gameplay/T_VectorLine2",
    "highlight": KIT + "/PNG/Inventory/T_Highlight_Line",
    "key": KIT + "/PNG/Inventory/T_TabKey_Indicator",
    "quest": KIT + "/PNG/Gameplay/T_QuestTracker-Color",
    "compass_line": KIT + "/PNG/Gameplay/T_Compass_Line",
    "weapon": KIT + "/PNG/Gameplay/T_Weapon",
    "weapon_alt": KIT + "/PNG/Gameplay/T_Weapon2",
    "icon_compass": KIT + "/PNG/Icons/GeneralIcons/T_icon_compass",
    "icon_energy": KIT + "/PNG/Icons/GeneralIcons/T_icon_energy",
    "icon_focus": KIT + "/PNG/Icons/GeneralIcons/T_icon_focus",
    "icon_location": KIT + "/PNG/Icons/GeneralIcons/T_icon_location",
    "icon_missile": KIT + "/PNG/Icons/GeneralIcons/T_icon_missile",
    "icon_refresh": KIT + "/PNG/Icons/GeneralIcons/T_icon_refresh",
    "icon_reload": KIT + "/PNG/Icons/GeneralIcons/T_icon_reload",
    "icon_target": KIT + "/PNG/Icons/GeneralIcons/T_icon_target",
    "icon_warning": KIT + "/PNG/Icons/GeneralIcons/T_icon_warning",
    "icon_wifi_on": KIT + "/PNG/Icons/GeneralIcons/T_icon_wifi_on",
    "icon_wifi_off": KIT + "/PNG/Icons/GeneralIcons/T_icon_wifi_off",
    "icon_arrow_up": KIT + "/PNG/Icons/GeneralIcons/T_icon_arrow_up",
}


def checked(raw):
    result = json.loads(raw)
    if not result.get("success"):
        raise RuntimeError(str(result))
    return result


def load_textures():
    result = {}
    for key, path in TEXTURES.items():
        texture = unreal.load_asset(path)
        if not isinstance(texture, unreal.Texture2D):
            raise RuntimeError("Missing NEONCTRL Texture2D: " + path)
        result[key] = texture
    return result


def find(bp, name):
    return HELPER.umg_find_widget(bp, name)


def add(bp, kind, name, parent=""):
    checked(HELPER.umg_add_widget(bp, kind, name, parent))
    widget = find(bp, name)
    if not widget:
        raise RuntimeError("Could not find newly added widget " + name)
    if kind in ("CanvasPanel", "SizeBox"):
        widget.set_visibility(unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE)
    else:
        widget.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)
    return widget


def canvas_layout(widget, x, y, width, height, anchor=(0.0, 0.0),
                  alignment=(0.0, 0.0), z=0):
    slot = widget.get_editor_property("slot")
    if not isinstance(slot, unreal.CanvasPanelSlot):
        raise RuntimeError(widget.get_name() + " must use CanvasPanelSlot")
    point = unreal.Vector2D(*anchor)
    slot.set_anchors(unreal.Anchors(point, point))
    slot.set_alignment(unreal.Vector2D(*alignment))
    slot.set_position(unreal.Vector2D(x, y))
    slot.set_size(unreal.Vector2D(width, height))
    slot.set_z_order(z)
    return widget


def fill_layout(widget, margin=0.0, z=0):
    slot = widget.get_editor_property("slot")
    if not isinstance(slot, unreal.CanvasPanelSlot):
        raise RuntimeError(widget.get_name() + " must use CanvasPanelSlot")
    slot.set_anchors(unreal.Anchors(unreal.Vector2D(0.0, 0.0), unreal.Vector2D(1.0, 1.0)))
    slot.set_alignment(unreal.Vector2D(0.0, 0.0))
    slot.set_offsets(unreal.Margin(margin, margin, margin, margin))
    slot.set_z_order(z)
    return widget


def image(bp, textures, name, parent, key, x=None, y=None, width=None, height=None,
          color=WHITE, z=0, fill=False, margin=0.0):
    widget = add(bp, "Image", name, parent)
    widget.set_brush_from_texture(textures[key], False)
    widget.set_color_and_opacity(unreal.LinearColor(*color))
    if fill:
        fill_layout(widget, margin, z)
    else:
        canvas_layout(widget, x, y, width, height, z=z)
    return widget


def text(bp, name, parent, x, y, width, height, value, size=12, color=WHITE,
         z=10, justification=unreal.TextJustify.LEFT):
    widget = add(bp, "TextBlock", name, parent)
    canvas_layout(widget, x, y, width, height, z=z)
    widget.set_text(value)
    widget.set_editor_property("justification", justification)
    widget.set_auto_wrap_text(False)
    checked(HELPER.umg_set_text_style(bp, name, size, *color, 0))
    font = widget.get_editor_property("font")
    font.set_editor_property("typeface_font_name", "Bold" if size >= 14 else "Regular")
    font.set_editor_property("letter_spacing", 0)
    widget.set_font(font)
    return widget


def rebuild_ship_hud(textures):
    asset = unreal.load_asset(SHIP_PATH)
    created = False
    if not asset:
        unreal.EditorAssetLibrary.make_directory(SHIP_FOLDER)
        factory = unreal.WidgetBlueprintFactory()
        factory.set_editor_property("parent_class", unreal.UserWidget)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "WBP_ShipHUD", SHIP_FOLDER, unreal.WidgetBlueprint, factory)
        created = True
    if not asset:
        raise RuntimeError("Could not create " + SHIP_PATH)

    old_tree = checked(HELPER.umg_get_widget_info(asset))
    if old_tree.get("root_widget"):
        checked(HELPER.umg_remove_widget(asset, old_tree["root_widget"]))

    root = add(asset, "CanvasPanel", "RootCanvas")
    root.set_visibility(unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE)

    # Five responsive zones.  Fixed component sizes preserve Figma typography;
    # viewport-edge/center anchors keep a true 24 px safe area on all targets.
    top = add(asset, "CanvasPanel", "C_ShipTopStatus", "RootCanvas")
    canvas_layout(top, 0, 24, 420, 64, anchor=(0.5, 0.0), alignment=(0.5, 0.0), z=20)
    image(asset, textures, "I_ShipTopBackground", top.get_name(), "background", fill=True,
          color=PANEL_DARK, z=0)
    image(asset, textures, "I_ShipTopFrame", top.get_name(), "edge", fill=True, z=1)
    image(asset, textures, "I_ShipTopCompassLine", top.get_name(), "compass_line",
          108, 2, 204, 12, color=CYAN, z=2)
    image(asset, textures, "I_ShipTopAccent", top.get_name(), "vector_v", 8, 10, 12, 44,
          color=CYAN, z=2)
    image(asset, textures, "I_ShipLink", top.get_name(), "icon_wifi_on", 26, 20, 22, 22,
          color=CYAN, z=3)
    text(asset, "TXT_ShipDomain", top.get_name(), 54, 17, 96, 24, "空战 · AIR", 12, CYAN)
    text(asset, "TXT_ShipMission", top.get_name(), 146, 13, 166, 18, "任务 02:32 · 战斗就绪", 10, WHITE,
         justification=unreal.TextJustify.CENTER)
    image(asset, textures, "I_ShipStatePipA", top.get_name(), "quest", 210, 37, 8, 8, z=3)
    image(asset, textures, "I_ShipStatePipB", top.get_name(), "quest", 224, 37, 8, 8, z=3)
    text(asset, "TXT_ShipState", top.get_name(), 310, 18, 92, 22, "飞船就绪", 11, GOLD,
         justification=unreal.TextJustify.RIGHT)

    left = add(asset, "CanvasPanel", "C_ShipFlightTelemetry", "RootCanvas")
    # T_Gameplay's in-game composition keeps telemetry above the bottom edge,
    # leaving the flight view open.  The 108 px inset clears the 56 px shortcut
    # strip plus a 28 px visual gap at every supported viewport size.
    canvas_layout(left, 24, -108, 300, 244, anchor=(0.0, 1.0), alignment=(0.0, 1.0), z=20)
    image(asset, textures, "I_FlightBackground", left.get_name(), "background", fill=True,
          color=PANEL_DARK, z=0)
    image(asset, textures, "I_FlightFrame", left.get_name(), "inventory", fill=True,
          color=WHITE, z=1)
    image(asset, textures, "I_FlightIcon", left.get_name(), "icon_compass", 16, 14, 24, 24,
          color=CYAN, z=3)
    text(asset, "TXT_FlightTitle", left.get_name(), 48, 14, 220, 22, "飞行状态", 13, WHITE)
    image(asset, textures, "I_FlightDivider", left.get_name(), "vector_h", 18, 42, 264, 8,
          color=CYAN, z=2)
    text(asset, "TXT_SpeedCaption", left.get_name(), 18, 55, 180, 18, "速度 / 最大速度", 10, MUTED)
    text(asset, "TXT_CurrentSpeed", left.get_name(), 18, 72, 118, 31, "842", 22, CYAN)
    text(asset, "TXT_MaxSpeed", left.get_name(), 104, 79, 164, 24, "/ 1200 m/s", 14, WHITE)
    text(asset, "TXT_BoostCaption", left.get_name(), 18, 111, 160, 18, "真实加力场", 10, MUTED)
    text(asset, "TXT_BoostState", left.get_name(), 204, 111, 66, 18, "待机", 10, WHITE,
         justification=unreal.TextJustify.RIGHT)
    image(asset, textures, "I_BoostTrack", left.get_name(), "bar_track", 18, 133, 250, 14, z=2)
    image(asset, textures, "I_BoostFill", left.get_name(), "bar_fill", 18, 133, 172, 14, z=3)
    text(asset, "TXT_AttitudeCaption", left.get_name(), 18, 157, 160, 18, "偏航 / 压弯", 10, MUTED)
    text(asset, "TXT_Attitude", left.get_name(), 18, 176, 250, 26, "YAW +03° · BANK −08°", 16, CYAN)
    image(asset, textures, "I_BoostBadge", left.get_name(), "weapon_slot", 16, 208, 136, 28,
          color=WHITE, z=2)
    image(asset, textures, "I_BoostBadgeIcon", left.get_name(), "icon_energy", 24, 213, 18, 18,
          color=CYAN, z=3)
    text(asset, "TXT_BoostBadge", left.get_name(), 48, 211, 96, 20, "加力就绪", 10, WHITE)

    right = add(asset, "CanvasPanel", "C_ShipLoadoutStatus", "RootCanvas")
    canvas_layout(right, -24, -108, 320, 284, anchor=(1.0, 1.0), alignment=(1.0, 1.0), z=20)
    image(asset, textures, "I_LoadoutBackground", right.get_name(), "background", fill=True,
          color=PANEL_DARK, z=0)
    image(asset, textures, "I_LoadoutFrame", right.get_name(), "inventory", fill=True,
          color=WHITE, z=1)
    image(asset, textures, "I_LoadoutIcon", right.get_name(), "icon_reload", 16, 14, 24, 24,
          color=CYAN, z=3)
    text(asset, "TXT_LoadoutTitle", right.get_name(), 48, 14, 242, 22, "装配状态 · 已应用", 13, WHITE)
    image(asset, textures, "I_LoadoutDivider", right.get_name(), "vector_h", 18, 42, 284, 8,
          color=CYAN, z=2)

    rows = [
        ("Engine", 54, "icon_energy", "引擎", "标准推进"),
        ("Weapon", 100, "icon_missile", "武器", "舰艏激光"),
        ("Rate", 146, "icon_target", "射速", "3.0 / 秒"),
    ]
    for suffix, y, icon_key, label, value in rows:
        image(asset, textures, "I_LoadoutSlot_" + suffix, right.get_name(), "weapon_slot",
              16, y, 288, 40, color=WHITE, z=2)
        image(asset, textures, "I_LoadoutRowIcon_" + suffix, right.get_name(), icon_key,
              24, y + 10, 20, 20, color=CYAN, z=3)
        text(asset, "TXT_LoadoutLabel_" + suffix, right.get_name(), 54, y + 10, 96, 20,
             label, 10, MUTED)
        text(asset, "TXT_LoadoutValue_" + suffix, right.get_name(), 144, y + 10, 146, 20,
             value, 10, GOLD if suffix == "Rate" else CYAN,
             justification=unreal.TextJustify.RIGHT)
    text(asset, "TXT_LoadoutProgressCaption", right.get_name(), 18, 202, 150, 18,
         "装配发布状态", 10, MUTED)
    text(asset, "TXT_LoadoutRevision", right.get_name(), 190, 202, 100, 18,
         "REV 12", 10, WHITE, justification=unreal.TextJustify.RIGHT)
    image(asset, textures, "I_LoadoutTrack", right.get_name(), "bar_track", 18, 226, 274, 14, z=2)
    image(asset, textures, "I_LoadoutFill", right.get_name(), "bar_fill", 18, 226, 192, 14, z=3)
    image(asset, textures, "I_LoadoutFooter", right.get_name(), "highlight", 18, 254, 274, 8,
          color=CYAN, z=2)
    text(asset, "TXT_LoadoutFooter", right.get_name(), 18, 249, 274, 22,
         "合法装配 · 数据稳定", 10, WHITE, justification=unreal.TextJustify.CENTER)

    bottom = add(asset, "CanvasPanel", "C_ShipShortcutStrip", "RootCanvas")
    canvas_layout(bottom, 0, -24, 840, 56, anchor=(0.5, 1.0), alignment=(0.5, 1.0), z=20)
    image(asset, textures, "I_ShortcutBackground", bottom.get_name(), "background", fill=True,
          color=PANEL_DARK, z=0)
    image(asset, textures, "I_ShortcutFrame", bottom.get_name(), "edge", fill=True, z=1)
    shortcuts = [
        ("Fire", 8, "weapon", "LMB", "开火"),
        ("Boost", 174, "icon_energy", "SHIFT", "加力"),
        ("Engine", 340, "icon_refresh", "R", "引擎"),
        ("Weapon", 506, "icon_missile", "T", "武器"),
        ("Zoom", 672, "icon_focus", "滚轮", "缩放"),
    ]
    for suffix, x, icon_key, key_text, label in shortcuts:
        image(asset, textures, "I_ShortcutIcon_" + suffix, bottom.get_name(), icon_key,
              x + 10, 16, 24, 24, color=CYAN, z=3)
        image(asset, textures, "I_ShortcutKey_" + suffix, bottom.get_name(), "key",
              x + 42, 17, 52, 22, color=WHITE, z=2)
        text(asset, "TXT_ShortcutKey_" + suffix, bottom.get_name(), x + 42, 19, 52, 18,
             key_text, 9, WHITE, justification=unreal.TextJustify.CENTER)
        text(asset, "TXT_ShortcutLabel_" + suffix, bottom.get_name(), x + 100, 19, 58, 18,
             label, 10, MUTED)

    heading = add(asset, "CanvasPanel", "C_ShipBoresightHeading", "RootCanvas")
    canvas_layout(heading, 0, 0, 88, 88, anchor=(0.5, 0.5), alignment=(0.5, 0.5), z=30)
    image(asset, textures, "I_HeadingBackground", heading.get_name(), "utility", fill=True,
          color=WHITE, z=0)
    image(asset, textures, "I_HeadingCompassLine", heading.get_name(), "compass_line",
          8, 36, 72, 14, color=CYAN, z=1)
    image(asset, textures, "I_HeadingCompass", heading.get_name(), "icon_compass",
          23, 20, 42, 42, color=CYAN, z=2)
    image(asset, textures, "I_HeadingArrow", heading.get_name(), "icon_arrow_up",
          35, 5, 18, 18, color=GOLD, z=3)
    text(asset, "TXT_Heading", heading.get_name(), 12, 65, 64, 17,
         "航向", 9, WHITE, justification=unreal.TextJustify.CENTER)

    # WidgetBlueprintFactory seeds three disabled template events, including
    # Tick.  Gate 2 requires a genuinely empty EventGraph, so remove those
    # unconnected templates instead of merely leaving them disabled.
    seeded_graph = checked(HELPER.get_blueprint_graph_info(asset, "EventGraph"))
    for node in seeded_graph["nodes"]:
        checked(HELPER.remove_blueprint_node(asset, "EventGraph", node["node_name"]))

    compile_result = checked(HELPER.compile_blueprint(asset))
    tree = checked(HELPER.umg_get_widget_info(asset))
    event_graph = checked(HELPER.get_blueprint_graph_info(asset, "EventGraph"))
    animations = list(unreal.WidgetService.list_animations(SHIP_PATH))
    view_models = list(unreal.WidgetService.list_view_models(SHIP_PATH))
    mvvm_bindings = list(unreal.WidgetService.list_view_model_bindings(SHIP_PATH))
    if tree["root_widget"] != "RootCanvas" or tree["widget_count"] != 83:
        raise RuntimeError("Unexpected Ship HUD tree: " + str(tree))
    if event_graph["node_count"] != 0 or animations or view_models or mvvm_bindings:
        raise RuntimeError("Ship HUD must have zero graph nodes, animations, ViewModels, and MVVM bindings")
    return asset, {
        "created": created,
        "compile": compile_result,
        "widget_count": tree["widget_count"],
        "root_children": [
            "C_ShipTopStatus", "C_ShipFlightTelemetry", "C_ShipLoadoutStatus",
            "C_ShipShortcutStrip", "C_ShipBoresightHeading",
        ],
        "event_graph_nodes": event_graph["node_count"],
        "animations": len(animations),
        "view_models": len(view_models),
        "mvvm_bindings": len(mvvm_bindings),
    }


def tree_signature(tree):
    return sorted((row["name"], row["type"], row.get("parent", "")) for row in tree["widgets"])


def slot_contract(widget):
    slot = widget.get_editor_property("slot")
    if not isinstance(slot, unreal.CanvasPanelSlot):
        return None
    anchors = slot.get_anchors()
    position = slot.get_position()
    size = slot.get_size()
    alignment = slot.get_alignment()
    return {
        "anchors": [anchors.minimum.x, anchors.minimum.y, anchors.maximum.x, anchors.maximum.y],
        "position": [position.x, position.y],
        "size": [size.x, size.y],
        "alignment": [alignment.x, alignment.y],
        "z": slot.get_z_order(),
    }


def set_commander_brush(bp, textures, widget_name, texture_key, color=WHITE):
    widget = find(bp, widget_name)
    if not isinstance(widget, unreal.Image):
        raise RuntimeError("Commander Image missing: " + widget_name)
    widget.set_brush_from_texture(textures[texture_key], False)
    widget.set_color_and_opacity(unreal.LinearColor(*color))


def reskin_commander(textures):
    # Keep the ship authoring path; commander regeneration is owned by the new console.
    import runpy
    root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    result = runpy.run_path(str(root / "Scripts/build_commander_sc2_ui.py"), run_name="__main__")["result"]
    return unreal.load_asset(COMMANDER_PATH), result


if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
    raise RuntimeError("Stop PIE before authoring NEONCTRL HUD assets")

OUTPUT.mkdir(parents=True, exist_ok=True)
textures = load_textures()
ship, ship_report = rebuild_ship_hud(textures)
commander, commander_report = reskin_commander(textures)

for asset in (ship, commander):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError("Could not save " + asset.get_path_name())

report = {
    "success": True,
    "kit_root": KIT,
    "textures_loaded": len(textures),
    "saved_assets": [ship.get_path_name(), commander.get_path_name()],
    "ship": ship_report,
    "commander": commander_report,
}
(OUTPUT / "build-report.json").write_text(
    json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.MCPythonHelper.submit_result(json.dumps(report, ensure_ascii=False))
