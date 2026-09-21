"""Build project-owned commander UI references and static unit portraits in UE5.7.

Run after compiling the native console, outside PIE, through commander_editor_python.
Vendor assets are referenced, never modified. Temporary portrait actors are removed.
The native HUD owns the reproducible widget layout; its WBP remains the project entry.
"""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUTPUT = ROOT / "ArtSource/UI/CommanderHUD"
OUTPUT.mkdir(parents=True, exist_ok=True)
ASSET_ROOT = "/Game/Commander/UI"
KIT = "/Game/Assets/Dark_GUI_Main_Menu_Pro_Kit"
HELPER = unreal.MCPythonHelper


def checked(raw):
    value = json.loads(raw)
    if not value.get("success"):
        raise RuntimeError(str(value))
    return value


def import_texture(source, name):
    if not source.is_file():
        raise RuntimeError("Missing portrait source: " + str(source))
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source))
    task.set_editor_property("destination_path", ASSET_ROOT + "/Textures/Portraits")
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(ASSET_ROOT + "/Textures/Portraits/" + name)
    if not texture:
        raise RuntimeError("Portrait import failed: " + name)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property("srgb", True)
    unreal.EditorAssetLibrary.save_loaded_asset(texture)
    return texture


def capture_model(row):
    """Use the authored presentation Blueprint, with a show-only offscreen capture."""
    actor_api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    spawned = []
    try:
        cls = unreal.load_class(None, row["PresentationClass"])
        if not cls:
            raise RuntimeError("Missing presentation class for " + row["Name"])
        preview = actor_api.spawn_actor_from_class(cls, unreal.Vector(0, 0, 500000))
        spawned.append(preview)
        center, extent = preview.get_actor_bounds(False, True)
        radius = max(extent.length(), 50.0)
        direction = unreal.Vector(1.0, -1.4, 0.8)
        camera_point = center + direction * (radius * 2.0)
        rotation = unreal.MathLibrary.find_look_at_rotation(camera_point, center)
        capture_actor = actor_api.spawn_actor_from_class(unreal.SceneCapture2D, camera_point, rotation)
        spawned.append(capture_actor)
        capture = capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
        target = unreal.RenderingLibrary.create_render_target2d(
            world, 512, 512, unreal.TextureRenderTargetFormat.RTF_RGBA8,
            unreal.LinearColor(0.018, 0.024, 0.034, 1.0))
        capture.set_editor_property("texture_target", target)
        capture.set_editor_property("capture_every_frame", False)
        capture.set_editor_property("capture_on_movement", False)
        capture.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        capture.set_editor_property("primitive_render_mode", unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
        capture.show_only_actor_components(preview, include_from_child_actors=True)
        capture.set_editor_property("fov_angle", 38.0)
        settings = unreal.PostProcessSettings()
        settings.set_editor_property("override_auto_exposure_method", True)
        settings.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
        settings.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
        settings.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
        settings.set_editor_property("override_auto_exposure_bias", True)
        settings.set_editor_property("auto_exposure_bias", -2.0)
        capture.set_editor_property("post_process_settings", settings)
        for location, intensity, color in [
            (center + unreal.Vector(radius, -radius, radius * 2), 3500.0, unreal.LinearColor(0.8, 0.9, 1)),
            (center + unreal.Vector(-radius, radius, radius), 1800.0, unreal.LinearColor(0.3, 0.7, 1)),
        ]:
            light = actor_api.spawn_actor_from_class(unreal.PointLight, location)
            spawned.append(light)
            lamp = light.get_component_by_class(unreal.PointLightComponent)
            lamp.set_editor_property("intensity", intensity)
            lamp.set_editor_property("attenuation_radius", radius * 8)
            lamp.set_light_color(color)
        capture.capture_scene()
        filename = f"Unit_{row['Id']}_Portrait.png"
        unreal.RenderingLibrary.export_render_target(world, target, str(OUTPUT), filename)
        source = OUTPUT / filename
        if not source.is_file():
            raise RuntimeError("Portrait render did not create " + str(source))
        return source
    finally:
        for actor in reversed(spawned):
            if actor:
                actor_api.destroy_actor(actor)


def build():
    if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError("Stop PIE before building UI assets")
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    textures = [a for a in registry.get_assets_by_path(KIT, recursive=True)
                if str(a.asset_class_path.asset_name) == "Texture2D"]

    def kit_texture(*names):
        for name in names:
            matches = [a for a in textures if str(a.asset_name) == name]
            matches.sort(key=lambda a: ("/Icons/" not in str(a.package_name), str(a.package_name)))
            if matches:
                return matches[0].get_asset()
        raise RuntimeError("Required kit texture missing: " + ", ".join(names))

    icon_names = {
        "Move": ["t_arrows_64", "t_arrow_up_64"],
        "Stop": ["t_pause_64"], "Skill": ["t_target_64"],
        "Focus": ["t_target_64"], "Tasks": ["t_menu_64"],
        "Mine": ["t_gems_blue_64"],
        "Return": ["t_arrow_left_64"], "Construct": ["t_hammer_64"],
        "Transit": ["t_arrow_right_64"], "Build": ["t_hammer_64"],
        "Teleport": ["t_bolt_64"], "Help": ["t_info_64"],
        "Menu": ["t_cog_64"], "Unit": ["t_users_64"],
        "Health": ["t_heart_red_64"], "Shield": ["t_shield_blue_fire_64"],
        "Speed": ["t_speed_64"], "Defense": ["t_shield_grey_64"],
        "CargoBlue": ["t_gems_blue_64"], "CargoRed": ["t_gems_red_64"],
    }
    icons = {name: kit_texture(*candidates) for name, candidates in icon_names.items()}
    for ident, name in {1:"t_target_64",2:"t_swords_64",3:"t_flag_64",4:"t_users_64",5:"t_shield_64",6:"t_home_64"}.items():
        icons[f"Building.{ident}"] = kit_texture(name)
    icons["Task.Special.Mining"] = icons["Mine"]
    icons["Task.Special.Construction"] = icons["Construct"]
    icons["Task.Special.StrongholdAdvance"] = icons["Move"]
    portraits = {
        1: import_texture(ROOT / "ArtSource/UI/UnitPortraits/Sweeper_Concept_UI.png", "T_UI_Sweeper"),
        2: import_texture(ROOT / "ArtSource/UI/UnitPortraits/WarMachine_Concept_UI.png", "T_UI_WarMachine"),
    }
    soldiers = json.loads((ROOT / "data/Json/DT_GuLiStrikeCommander_Soldiers.json").read_text(encoding="utf-8-sig"))
    for row in soldiers:
        if row["Id"] in (3, 4):
            portraits[row["Id"]] = import_texture(capture_model(row), f"T_UI_Unit_{row['Id']}")
    path = ASSET_ROOT + "/DA_CommanderUITheme"
    theme = unreal.load_asset(path)
    if not theme:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.GuLiCommanderUITheme)
        theme = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "DA_CommanderUITheme", ASSET_ROOT, unreal.GuLiCommanderUITheme, factory)
    theme.set_editor_property("icons", icons)
    theme.set_editor_property("portraits", portraits)
    theme.set_editor_property("panel", kit_texture("t_UniversalPanel30", "t_UniversalPanel20"))
    theme.set_editor_property("button", kit_texture("t_ButtonLeft", "t_ButtonRight"))
    theme.set_editor_property("bar", kit_texture("t_BarLeftFill", "t_BarRightFill"))
    theme.set_editor_property("scroll_thumb", kit_texture("t_ScrollBarVerticalHandle"))
    fonts = [a for a in registry.get_assets_by_path(KIT, recursive=True) if str(a.asset_class_path.asset_name) == "Font"]
    if fonts:
        fonts.sort(key=lambda a: (str(a.asset_name) != "f_Aileron-Regular", str(a.package_name)))
        theme.set_editor_property("numeric_font", fonts[0].get_asset())
    unreal.EditorAssetLibrary.save_loaded_asset(theme)
    hud = unreal.load_asset(ASSET_ROOT + "/Widgets/WBP_CommanderHUD")
    if not hud:
        raise RuntimeError("Project commander HUD entry is missing")
    # Native RebuildWidget authors the console for editor preview and gameplay alike.
    tree = checked(HELPER.umg_get_widget_info(hud))
    for row in reversed(tree["widgets"]):
        if row["name"] != tree["root_widget"] and HELPER.umg_find_widget(hud, row["name"]):
            checked(HELPER.umg_remove_widget(hud, row["name"]))
    checked(HELPER.compile_blueprint(hud))
    default = unreal.get_default_object(hud.generated_class())
    default.set_editor_property("console_theme", theme)
    unreal.EditorAssetLibrary.set_metadata_tag(hud, "GuLiConsoleLayout", "SC2-1")
    unreal.EditorAssetLibrary.save_loaded_asset(hud, only_if_is_dirty=False)
    manifest = {
        "schema": "guli-commander-ui-assets/v1", "vendor_root": KIT,
        "layout_source": "Source/GuLiStrike/Commander/UI/GuLiCommanderConsoleLayout.cpp",
        "theme": theme.get_path_name(), "hud": hud.get_path_name(),
        "surfaces": {key: theme.get_editor_property(key).get_path_name() for key in ("panel", "button", "bar", "scroll_thumb")},
        "numeric_font": theme.get_editor_property("numeric_font").get_path_name(),
        "game_text_table": "/Game/GuLiStrike/Data/DT_GuLiStrikeGameTexts_Texts",
        "icons": {key: value.get_path_name() for key, value in icons.items()},
        "portraits": {str(key): value.get_path_name() for key, value in portraits.items()},
        "portrait_sources": {str(row["Id"]): row.get("PresentationClass") for row in soldiers if row["Id"] in (3, 4)},
        "notes": ["SC2 references are analysis only; not imported as game art.",
                  "Vendor textures remain unchanged.", "Chinese uses the existing font fallback."]}
    (OUTPUT / "asset_manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    return {"success": True, "theme": path, "portrait_count": len(portraits), "icon_bindings": len(icons)}


result = build()
unreal.MCPythonHelper.submit_result(json.dumps(result))
