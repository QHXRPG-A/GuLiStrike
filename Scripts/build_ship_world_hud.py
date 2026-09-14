"""Build and audit the three generated Ship information-panel assets.

Run only after compiling the native GuLiStrike Editor target.  The script is
idempotent: revision 2 refreshes Status, Flight and Combat only. Reticle,
AimBounds and the project copy of Widget3DPassThrough remain untouched. It never edits WBP_ShipHUD and
adds no Blueprint graph logic, animation, binding, input, or focusable control.
"""

import hashlib
import json
from pathlib import Path

import unreal


HELPER = unreal.MCPythonHelper
WIDGET_FOLDER = "/Game/Ship/UI/Widgets/World"
MATERIAL_FOLDER = "/Game/Ship/UI/Materials"
MATERIAL_PATH = MATERIAL_FOLDER + "/M_UI_ShipWorld_NoDepth"
MATERIAL_SOURCE = "/Engine/EngineMaterials/Widget3DPassThrough"
OUTPUT = Path("D:/UE5.7/test1/outputs/review/ship-world-hud")
KIT = "/Game/NEONCTRL_FuturisticClea_UIKit"
STYLE_METADATA_TAG = "GuLi.ShipWorldHUD.StyleRevision"
STYLE_REVISION = 2
STYLE_WIDGETS = (
    "WBP_ShipWorldStatus",
    "WBP_ShipWorldFlight",
    "WBP_ShipWorldCombat",
)

WHITE = (1.0, 1.0, 1.0, 1.0)
CYAN = (0.12, 0.95, 1.0, 1.0)
CYAN_DIM = (0.12, 0.95, 1.0, 0.55)
MUTED = (0.72, 0.84, 0.90, 1.0)
GOLD = (1.0, 0.82, 0.25, 1.0)
PANEL = (0.03, 0.10, 0.15, 0.88)
PANEL_BOUNDED = (0.01, 0.04, 0.07, 0.28)
OUTLINE = (0.0, 0.0, 0.0, 0.9)

TEXTURES = {
    "background": KIT + "/PNG/Commom/T_Background",
    "edge": KIT + "/PNG/Gameplay/T_EdgeFrame-Color",
    "details": KIT + "/PNG/Inventory/T_DetailsField",
    "bar_fill": KIT + "/PNG/Gameplay/T_BarFill-Color",
    "bar_track": KIT + "/PNG/Gameplay/T_BarNoFill-Color",
    "vector_h": KIT + "/PNG/Gameplay/T_VectorLine1",
    "vector_v": KIT + "/PNG/Gameplay/T_VectorLine2",
    "highlight": KIT + "/PNG/Inventory/T_Highlight_Line",
    "quest": KIT + "/PNG/Gameplay/T_QuestTracker-Color",
    "icon_compass": KIT + "/PNG/Icons/GeneralIcons/T_icon_compass",
    "icon_energy": KIT + "/PNG/Icons/GeneralIcons/T_icon_energy",
    "icon_target": KIT + "/PNG/Icons/GeneralIcons/T_icon_target",
    "icon_missile": KIT + "/PNG/Icons/GeneralIcons/T_icon_missile",
}

SPECS = {
    "WBP_ShipWorldStatus": {
        "parent": "/Script/GuLiStrike.GuLiShipWorldStatusWidget",
        "size": [420, 72],
        "signature": "8ba199e0473eafb3e63a9d3b35265484e0853382bc5e0f6f56ef02dd0d851bf7",
    },
    "WBP_ShipWorldFlight": {
        "parent": "/Script/GuLiStrike.GuLiShipWorldFlightWidget",
        "size": [280, 144],
        "signature": "54440d142496c563c5f71c88eb2a9d0fa66e84969dd140a122b6c2c791bf8e2b",
    },
    "WBP_ShipWorldCombat": {
        "parent": "/Script/GuLiStrike.GuLiShipWorldCombatWidget",
        "size": [300, 164],
        "signature": "5aee860dad412d584d57ada75107529e3e117f4bbaf1677c9c545a427861a0e0",
    },
    "WBP_ShipWorldReticle": {
        "parent": "/Script/GuLiStrike.GuLiShipWorldReticleWidget",
        "size": [96, 96],
        "signature": "b15f9c27bd57ae91731546e9fcf5d9745c91b9ab1caea9dc454578cebca5c22b",
    },
    "WBP_ShipWorldAimBounds": {
        "parent": "/Script/GuLiStrike.GuLiShipWorldAimBoundsWidget",
        "size": [768, 324],
        "signature": "7110960a86edd3947718527393e5216dd6304fb9bef3d5340c42283bd3105a8a",
    },
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
    checked(HELPER.umg_set_widget_is_variable(bp, name, True))
    return widget


def canvas_layout(widget, x, y, width, height, z=0):
    slot = widget.get_editor_property("slot")
    if not isinstance(slot, unreal.CanvasPanelSlot):
        raise RuntimeError(widget.get_name() + " must use CanvasPanelSlot")
    origin = unreal.Vector2D(0.0, 0.0)
    slot.set_anchors(unreal.Anchors(origin, origin))
    slot.set_alignment(origin)
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
        canvas_layout(widget, x, y, width, height, z)
    return widget


def line(bp, textures, name, parent, center_x, center_y, length, thickness,
         angle=0.0, color=WHITE, z=3):
    widget = image(
        bp, textures, name, parent, "highlight",
        center_x - length * 0.5, center_y - thickness * 0.5,
        length, thickness, color, z)
    widget.set_render_transform_pivot(unreal.Vector2D(0.5, 0.5))
    widget.set_render_transform_angle(angle)
    return widget


def anchor_layout(widget, anchor_x, anchor_y, offset_x, offset_y,
                  width, height, alignment_x, alignment_y, z=0):
    slot = widget.get_editor_property("slot")
    if not isinstance(slot, unreal.CanvasPanelSlot):
        raise RuntimeError(widget.get_name() + " must use CanvasPanelSlot")
    anchor = unreal.Vector2D(anchor_x, anchor_y)
    slot.set_anchors(unreal.Anchors(anchor, anchor))
    slot.set_alignment(unreal.Vector2D(alignment_x, alignment_y))
    slot.set_position(unreal.Vector2D(offset_x, offset_y))
    slot.set_size(unreal.Vector2D(width, height))
    slot.set_z_order(z)
    return widget


def anchored_line(bp, textures, name, parent, anchor_x, anchor_y,
                  offset_x, offset_y, length, thickness, angle, color=CYAN, z=3):
    widget = image(
        bp, textures, name, parent, "highlight",
        0, 0, length, thickness, color, z)
    anchor_layout(
        widget, anchor_x, anchor_y, offset_x, offset_y, length, thickness,
        anchor_x, anchor_y, z)
    widget.set_render_transform_pivot(unreal.Vector2D(0.5, 0.5))
    widget.set_render_transform_angle(angle)
    return widget


def text(bp, name, parent, x, y, width, height, value, size=12, color=WHITE,
         z=10, justification=unreal.TextJustify.LEFT):
    widget = add(bp, "TextBlock", name, parent)
    canvas_layout(widget, x, y, width, height, z)
    widget.set_text(value)
    widget.set_editor_property("justification", justification)
    widget.set_auto_wrap_text(False)
    apply_text_style(bp, name, size, color)
    return widget


def apply_text_style(bp, name, size, color):
    widget = find(bp, name)
    if not widget:
        raise RuntimeError("Missing generated TextBlock " + name)
    checked(HELPER.umg_set_text_style(bp, name, size, *color, 1))
    font = widget.get_editor_property("font")
    font.set_editor_property("typeface_font_name", "Bold" if size >= 14 else "Regular")
    font.set_editor_property("letter_spacing", 0)
    outline = font.get_editor_property("outline_settings")
    outline.set_editor_property("outline_size", 1)
    outline.set_editor_property("outline_color", unreal.LinearColor(*OUTLINE))
    font.set_editor_property("outline_settings", outline)
    widget.set_font(font)


def progress(bp, name, parent, x, y, width, height):
    widget = add(bp, "ProgressBar", name, parent)
    canvas_layout(widget, x, y, width, height, 10)
    widget.set_percent(0.0)
    widget.set_fill_color_and_opacity(unreal.LinearColor(*CYAN))
    return widget


def panel_base(bp, textures, width, height, prefix):
    root = add(bp, "CanvasPanel", "RootCanvas")
    root.set_visibility(unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE)
    image(bp, textures, prefix + "_Background", "RootCanvas", "background",
          0, 0, width, height, PANEL, 0)
    image(bp, textures, prefix + "_Frame", "RootCanvas", "edge",
          0, 0, width, height, WHITE, 1)
    image(bp, textures, prefix + "_TopRail", "RootCanvas", "vector_h",
          18, 4, width - 36, 7, CYAN, 2)
    return root


def build_status(bp, textures):
    panel_base(bp, textures, 420, 72, "I_Status")
    image(bp, textures, "I_StatusIcon", "RootCanvas", "icon_compass",
          14, 18, 26, 26, CYAN, 3)
    text(bp, "TXT_Readiness", "RootCanvas", 48, 14, 162, 22,
         "SHIP / SYNC", 14, WHITE)
    text(bp, "TXT_Hull", "RootCanvas", 220, 14, 180, 22,
         "HULL -- / --", 14, GOLD, justification=unreal.TextJustify.RIGHT)
    progress(bp, "PB_Hull", "RootCanvas", 48, 43, 352, 10)
    image(bp, textures, "I_StatusBottomRail", "RootCanvas", "highlight",
          48, 57, 352, 5, CYAN_DIM, 2)


def build_flight(bp, textures):
    panel_base(bp, textures, 280, 144, "I_Flight")
    image(bp, textures, "I_FlightIcon", "RootCanvas", "icon_energy",
          14, 17, 24, 24, CYAN, 3)
    text(bp, "TXT_FlightTitle", "RootCanvas", 47, 15, 210, 22,
         "FLIGHT TELEMETRY", 14, MUTED)
    image(bp, textures, "I_FlightDivider", "RootCanvas", "vector_h",
          16, 44, 248, 6, CYAN_DIM, 2)
    text(bp, "TXT_CurrentSpeed", "RootCanvas", 16, 54, 248, 28,
         "SPEED -- m/s", 20, CYAN)
    text(bp, "TXT_MaxSpeed", "RootCanvas", 16, 84, 248, 20,
         "MAX -- m/s", 13, MUTED)
    text(bp, "TXT_BoostState", "RootCanvas", 16, 111, 248, 20,
         "BOOST / STANDBY", 13, GOLD)


def build_combat(bp, textures):
    panel_base(bp, textures, 300, 164, "I_Combat")
    image(bp, textures, "I_CombatIcon", "RootCanvas", "icon_target",
          14, 17, 24, 24, CYAN, 3)
    text(bp, "TXT_CombatTitle", "RootCanvas", 47, 15, 230, 22,
         "WEAPON STATUS", 14, MUTED)
    image(bp, textures, "I_CombatDivider", "RootCanvas", "vector_h",
          16, 44, 268, 6, CYAN_DIM, 2)
    image(bp, textures, "I_BasicIcon", "RootCanvas", "icon_target",
          18, 60, 24, 24, CYAN, 3)
    text(bp, "TXT_BasicWeaponState", "RootCanvas", 52, 61, 226, 22,
         "BASIC / UNAVAILABLE", 14, WHITE)
    image(bp, textures, "I_CombatMidRail", "RootCanvas", "highlight",
          18, 94, 264, 5, CYAN_DIM, 2)
    image(bp, textures, "I_MissileIcon", "RootCanvas", "icon_missile",
          18, 111, 24, 24, GOLD, 3)
    text(bp, "TXT_MissileState", "RootCanvas", 52, 112, 226, 22,
         "MISSILE / UNAVAILABLE", 14, GOLD)
    image(bp, textures, "I_CombatBottomRail", "RootCanvas", "vector_h",
          18, 146, 264, 6, CYAN_DIM, 2)


def build_reticle(bp, textures):
    add(bp, "CanvasPanel", "RootCanvas")
    # Four disconnected fine-line bracket groups with diagonal inner cuts.
    # The visual is deliberately non-circular and remains legible at 96 px.
    for suffix, cx, cy, angle in [
        ("BracketTL_H", 21, 12, 0), ("BracketTL_V", 12, 21, 90),
        ("BracketTR_H", 75, 12, 0), ("BracketTR_V", 84, 21, 90),
        ("BracketBL_H", 21, 84, 0), ("BracketBL_V", 12, 75, 90),
        ("BracketBR_H", 75, 84, 0), ("BracketBR_V", 84, 75, 90),
    ]:
        line(bp, textures, "I_Reticle_" + suffix, "RootCanvas",
             cx, cy, 18, 3, angle, WHITE, 3)
    for suffix, cx, cy, angle in [
        ("CutTL_Line", 27, 27, 45), ("CutTR_Line", 69, 27, -45),
        ("CutBL_Line", 27, 69, -45), ("CutBR_Line", 69, 69, 45),
    ]:
        line(bp, textures, "I_Reticle_" + suffix, "RootCanvas",
             cx, cy, 12, 3, angle, CYAN, 4)
    for suffix, cx, cy, angle in [
        ("CenterLeft", 40, 48, 0), ("CenterRight", 56, 48, 0),
        ("CenterTop", 48, 40, 90), ("CenterBottom", 48, 56, 90),
    ]:
        line(bp, textures, "I_Reticle_" + suffix, "RootCanvas",
             cx, cy, 7, 2, angle, CYAN, 5)
    image(bp, textures, "I_Reticle_CenterPointSolid", "RootCanvas", "bar_fill",
          46, 46, 4, 4, GOLD, 6)


def build_aim_bounds(bp, textures):
    add(bp, "CanvasPanel", "RootCanvas")
    image(bp, textures, "I_AimBounds_Background", "RootCanvas", "background",
          fill=True, color=PANEL_BOUNDED, z=0)
    image(bp, textures, "I_AimBounds_Frame", "RootCanvas", "edge",
          fill=True, color=CYAN_DIM, z=1)
    # Each stripe is corner-anchored, so the accents survive every dynamic
    # 0..1 bounds size instead of assuming the 768x324 authoring resolution.
    for corner, ax, ay, ox, oy, angle in [
        ("TL", 0, 0, 17, 17, -45), ("TR", 1, 0, -17, 17, 45),
        ("BL", 0, 1, 17, -17, 45), ("BR", 1, 1, -17, -17, -45),
    ]:
        for index, shift in enumerate((-8, 0, 8), 1):
            anchored_line(
                bp, textures, f"I_AimBounds_Hatch_{corner}_{index}", "RootCanvas",
                ax, ay, ox + shift, oy, 18, 3, angle, CYAN, 3)
    top = image(bp, textures, "I_AimBounds_TopRail", "RootCanvas", "highlight",
                0, 0, 1, 4, CYAN, 2)
    top_slot = top.get_editor_property("slot")
    top_slot.set_anchors(unreal.Anchors(unreal.Vector2D(0.0, 0.0), unreal.Vector2D(1.0, 0.0)))
    top_slot.set_offsets(unreal.Margin(54.0, 7.0, 54.0, 4.0))
    bottom = image(bp, textures, "I_AimBounds_BottomRail", "RootCanvas", "highlight",
                   0, 0, 1, 4, CYAN, 2)
    bottom_slot = bottom.get_editor_property("slot")
    bottom_slot.set_anchors(unreal.Anchors(unreal.Vector2D(0.0, 1.0), unreal.Vector2D(1.0, 1.0)))
    bottom_slot.set_offsets(unreal.Margin(54.0, -11.0, 54.0, 4.0))


BUILDERS = {
    "WBP_ShipWorldStatus": build_status,
    "WBP_ShipWorldFlight": build_flight,
    "WBP_ShipWorldCombat": build_combat,
    "WBP_ShipWorldReticle": build_reticle,
    "WBP_ShipWorldAimBounds": build_aim_bounds,
}


TEXT_STYLES = {
    "WBP_ShipWorldStatus": {
        "TXT_Readiness": (14, WHITE),
        "TXT_Hull": (14, GOLD),
    },
    "WBP_ShipWorldFlight": {
        "TXT_FlightTitle": (14, MUTED),
        "TXT_CurrentSpeed": (20, CYAN),
        "TXT_MaxSpeed": (13, MUTED),
        "TXT_BoostState": (13, GOLD),
    },
    "WBP_ShipWorldCombat": {
        "TXT_CombatTitle": (14, MUTED),
        "TXT_BasicWeaponState": (14, WHITE),
        "TXT_MissileState": (14, GOLD),
    },
}


def apply_information_panel_style(bp, name):
    prefix = {
        "WBP_ShipWorldStatus": "I_Status",
        "WBP_ShipWorldFlight": "I_Flight",
        "WBP_ShipWorldCombat": "I_Combat",
    }[name]
    background = find(bp, prefix + "_Background")
    if not background:
        raise RuntimeError("Missing generated panel background for " + name)
    background.set_color_and_opacity(unreal.LinearColor(*PANEL))
    for text_name, (font_size, color) in TEXT_STYLES[name].items():
        apply_text_style(bp, text_name, font_size, color)


def validate_material():
    material = unreal.load_asset(MATERIAL_PATH)
    if not material:
        raise RuntimeError("World HUD material is missing: " + MATERIAL_PATH)
    if not isinstance(material, unreal.Material):
        raise RuntimeError("World HUD material is not a Material: " + MATERIAL_PATH)
    if not material.get_editor_property("disable_depth_test"):
        raise RuntimeError("World HUD material did not retain Disable Depth Test")
    if material.get_editor_property("two_sided"):
        raise RuntimeError("World HUD material unexpectedly retained Two Sided")
    if (material.get_editor_property("translucency_pass")
            != unreal.MaterialTranslucencyPass.MTP_AFTER_MOTION_BLUR):
        raise RuntimeError("World HUD material did not retain After Motion Blur")
    return material


def tree_signature(tree):
    rows = sorted(
        (row["name"], row["type"], row.get("parent", "")) for row in tree["widgets"])
    return hashlib.sha256(
        json.dumps(rows, ensure_ascii=False).encode("utf-8")).hexdigest()


def ensure_widget(name, spec, textures):
    path = WIDGET_FOLDER + "/" + name
    bp = unreal.load_asset(path)
    created = False
    parent_class = unreal.load_class(None, spec["parent"])
    if not parent_class:
        raise RuntimeError("Native Widget parent is not loaded: " + spec["parent"])
    if bp:
        current_tree = checked(HELPER.umg_get_widget_info(bp))
        if (current_tree.get("root_widget") != "RootCanvas"
                or tree_signature(current_tree) != spec["signature"]):
            # These six assets are exclusively owned by this generator. Replacing
            # a mismatched package avoids UMG's stale variable-GUID map when the
            # same widget names are removed and re-added in one editor process.
            if not unreal.EditorAssetLibrary.delete_asset(path):
                raise RuntimeError("Could not replace mismatched generated asset " + path)
            bp = None
    if not bp:
        factory = unreal.WidgetBlueprintFactory()
        factory.set_editor_property("parent_class", parent_class)
        bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, WIDGET_FOLDER, unreal.WidgetBlueprint, factory)
        created = True
    if not isinstance(bp, unreal.WidgetBlueprint):
        raise RuntimeError(path + " is not a WidgetBlueprint")
    if created:
        BUILDERS[name](bp, textures)

    current_revision = str(
        unreal.EditorAssetLibrary.get_metadata_tag(bp, STYLE_METADATA_TAG) or ""
    ).strip()
    if current_revision not in ("", "1", str(STYLE_REVISION)):
        raise RuntimeError(
            f"Refusing to overwrite unknown {STYLE_METADATA_TAG}={current_revision} on {path}"
        )
    changed = created or current_revision != str(STYLE_REVISION)

    graph = checked(HELPER.get_blueprint_graph_info(bp, "EventGraph"))
    if changed:
        apply_information_panel_style(bp, name)
        for node in graph["nodes"]:
            checked(HELPER.remove_blueprint_node(bp, "EventGraph", node["node_name"]))
        unreal.EditorAssetLibrary.set_metadata_tag(
            bp, STYLE_METADATA_TAG, str(STYLE_REVISION)
        )
        compile_result = checked(HELPER.compile_blueprint(bp))
    else:
        compile_result = {"success": True, "skipped": True}

    generated = bp.generated_class()
    if not generated:
        raise RuntimeError(path + " has the wrong native parent")
    default_widget = unreal.get_default_object(generated)
    if changed:
        default_widget.set_editor_property("is_focusable", False)

    tree = checked(HELPER.umg_get_widget_info(bp))
    graph = checked(HELPER.get_blueprint_graph_info(bp, "EventGraph"))
    animations = list(unreal.WidgetService.list_animations(path))
    view_models = list(unreal.WidgetService.list_view_models(path))
    mvvm_bindings = list(unreal.WidgetService.list_view_model_bindings(path))
    legacy_bindings = []
    try:
        legacy_bindings = list(bp.get_editor_property("bindings"))
    except Exception:
        # Reconstructed trees never invoke a binding API. Some UE Python builds
        # do not expose UWidgetBlueprint::Bindings for direct readback.
        legacy_bindings = []
    interactive_types = {
        "Button", "CheckBox", "ComboBox", "ComboBoxString", "EditableText",
        "EditableTextBox", "InputKeySelector", "MenuAnchor", "Slider", "SpinBox",
    }
    interactive = [row["name"] for row in tree["widgets"] if row["type"] in interactive_types]
    if (tree["root_widget"] != "RootCanvas" or graph["node_count"] != 0
            or animations or view_models or mvvm_bindings or legacy_bindings
            or interactive or default_widget.get_editor_property("is_focusable")):
        raise RuntimeError("Static world HUD contract failed for " + path)

    signature = tree_signature(tree)
    if signature != spec["signature"]:
        raise RuntimeError("Unexpected generated tree signature for " + path)
    if changed and not unreal.EditorAssetLibrary.save_loaded_asset(bp, only_if_is_dirty=False):
        raise RuntimeError("Could not save " + path)
    return bp, {
        "path": path,
        "created": created,
        "changed": changed,
        "style_revision": int(STYLE_REVISION),
        "native_parent": spec["parent"],
        "draw_size": spec["size"],
        "compile": compile_result,
        "root_widget": tree["root_widget"],
        "widget_count": tree["widget_count"],
        "tree_signature": signature,
        "event_graph_nodes": graph["node_count"],
        "animations": len(animations),
        "legacy_bindings": len(legacy_bindings),
        "view_models": len(view_models),
        "mvvm_bindings": len(mvvm_bindings),
        "interactive_widgets": interactive,
        "focusable": bool(default_widget.get_editor_property("is_focusable")),
    }


targets = {WIDGET_FOLDER + "/" + name for name in STYLE_WIDGETS}
dirty_targets = sorted(
    str(package.get_name())
    for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    if str(package.get_name()) in targets
)
if dirty_targets:
    raise RuntimeError("Unsaved edits in Ship HUD style targets: " + repr(dirty_targets))

unreal.EditorAssetLibrary.make_directory(WIDGET_FOLDER)
textures = load_textures()
material = validate_material()
reports = []
assets = []
for widget_name in STYLE_WIDGETS:
    widget_spec = SPECS[widget_name]
    asset, report = ensure_widget(widget_name, widget_spec, textures)
    assets.append(asset)
    reports.append(report)

OUTPUT.mkdir(parents=True, exist_ok=True)
report = {
    "success": True,
    "old_ship_hud_touched": False,
    "material": {
        "path": MATERIAL_PATH,
        "touched": False,
        "disable_depth_test": bool(material.get_editor_property("disable_depth_test")),
        "two_sided": bool(material.get_editor_property("two_sided")),
        "translucency_pass": str(material.get_editor_property("translucency_pass")),
    },
    "widgets": reports,
    "untouched_assets": [
        MATERIAL_PATH,
        WIDGET_FOLDER + "/WBP_ShipWorldReticle",
        WIDGET_FOLDER + "/WBP_ShipWorldAimBounds",
    ],
    "saved_assets": [row["path"] for row in reports if row["changed"]],
}
(OUTPUT / "build-report.json").write_text(
    json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
HELPER.submit_result(json.dumps(report, ensure_ascii=False))
