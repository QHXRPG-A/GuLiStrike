"""Staged editor-side import. Run through run_combat_island_import.py, outside PIE."""

import json
from pathlib import Path

import unreal


ROOT = Path(unreal.Paths.project_dir()).resolve()
FILES = ROOT / "ArtSource/Environment/GuLiStrike_CombatIsland_2300m_v1/UEImport"
MANIFEST = json.loads((FILES / "import_manifest.json").read_text(encoding="utf-8"))
MAP = MANIFEST["ue_map"]
ASSETS = MANIFEST["ue_asset_root"]
OWNER = "GuLiStrike.CombatIsland.2300m.v1"
LABEL = "Landscape_CombatIsland_2300m_v1"
MATERIAL = ASSETS + "/Materials/M_CombatIsland_Zones"
SEA_MATERIAL = ASSETS + "/Materials/M_CombatIsland_Sea"
LIB = unreal.EditorAssetLibrary
EDIT = unreal.MaterialEditingLibrary
ACTORS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def world():
    assert not LEVELS.is_in_play_in_editor(), "Leave PIE before importing"
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def owned_world():
    value = world()
    assert value.get_path_name().split(".")[0] == MAP, value.get_path_name()
    assert LIB.get_metadata_tag(value, "GuLi.ArtImport.Owner") == OWNER
    return value


def dirty():
    return {"maps": [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
            "assets": [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}


def mark(obj):
    LIB.set_metadata_tag(obj, "GuLi.ArtImport.Owner", OWNER)
    LIB.set_metadata_tag(obj, "GuLi.ArtImport.Source", MANIFEST["source_project"])
    return obj


def save_asset(obj):
    assert obj.get_path_name().startswith(ASSETS + "/")
    assert LIB.get_metadata_tag(obj, "GuLi.ArtImport.Owner") == OWNER
    assert LIB.save_loaded_asset(obj, only_if_is_dirty=False), obj.get_path_name()


def landscape():
    owned_world()
    matches = [a for a in ACTORS.get_all_level_actors() if a.get_actor_label() == LABEL]
    assert len(matches) == 1 and isinstance(matches[0], unreal.Landscape)
    return matches[0]


def setup():
    previous_map = world().get_path_name()
    assert not LIB.does_asset_exist(MAP), "Existing map must be inspected, never overwritten"
    before = dirty()
    assert not before["maps"] and not before["assets"], before
    assert LEVELS.new_level_from_template(MAP, "/Engine/Maps/Templates/Template_Default")
    new_world = mark(world())
    floor = [a for a in ACTORS.get_all_level_actors() if isinstance(a, unreal.StaticMeshActor)
             and a.get_actor_label() == "Floor"]
    assert len(floor) == 1, [a.get_actor_label() for a in ACTORS.get_all_level_actors()]
    assert ACTORS.destroy_actor(floor[0])
    for a in ACTORS.get_all_level_actors():
        if isinstance(a, unreal.DirectionalLight):
            a.get_editor_property("directional_light_component").set_editor_property("intensity", 3.0)
        if isinstance(a, unreal.PlayerStart):
            a.set_actor_label("CombatIsland_PlayerStart_SW")
            a.set_actor_location(unreal.Vector(-39000, -35000, 1200), False, False)
        a.set_folder_path("CombatIsland/Environment")
    game_mode = unreal.load_class(None, "/Script/GuLiStrike.GuLiCommanderGameMode")
    assert game_mode
    new_world.get_world_settings().set_editor_property("default_game_mode", game_mode)
    assert LEVELS.save_current_level()
    return {"success": True, "map": new_world.get_path_name(), "previous_map": previous_map, "dirty": dirty()}


def node(mat, cls, x, y, **properties):
    value = EDIT.create_material_expression(mat, cls, x, y)
    assert value
    for key, val in properties.items():
        value.set_editor_property(key, val)
    return value


def materials():
    owned_world()
    assert not LIB.does_asset_exist(MATERIAL) and not LIB.does_asset_exist(SEA_MATERIAL)
    made = unreal.LandscapeMaterialService.create_landscape_material("M_CombatIsland_Zones", ASSETS + "/Materials")
    assert made.success, made.error_message
    mat = mark(unreal.load_asset(made.asset_path))
    assert mat
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    configs = [unreal.LandscapeMaterialLayerConfig(layer_name=l["name"], blend_type="LB_WeightBlend",
                preview_weight=1.0 if i == 0 else 0.0) for i, l in enumerate(MANIFEST["layers"])]
    blend_info = unreal.LandscapeMaterialService.create_layer_blend_node_with_layers(MATERIAL, configs, -200, 0)
    assert blend_info.node_id and len(blend_info.layers) == 6
    for i, layer in enumerate(MANIFEST["layers"]):
        node(mat, unreal.MaterialExpressionVectorParameter, -650, i * 180,
             parameter_name=layer["name"] + "Color", group="Terrain zones",
             default_value=unreal.LinearColor(*layer["color_linear"], 1))
    graph = json.loads(unreal.MaterialNodeService.export_material_graph(MATERIAL))
    ids = {e.get("parameter_name"): e["id"] for e in graph["expressions"] if e.get("parameter_name")}
    for layer in MANIFEST["layers"]:
        assert unreal.LandscapeMaterialService.connect_to_layer_input(MATERIAL, ids[layer["name"] + "Color"],
                    "RGB", blend_info.node_id, layer["name"], "Layer")
    # Get the actual blend UObject through a reflected material output; IDs are native pointers.
    assert unreal.MaterialNodeService.connect_to_output(MATERIAL, blend_info.node_id, "", "BaseColor")
    blend = EDIT.get_material_property_input_node(mat, unreal.MaterialProperty.MP_BASE_COLOR)
    assert isinstance(blend, unreal.MaterialExpressionLandscapeLayerBlend)
    normal = node(mat, unreal.MaterialExpressionVertexNormalWS, -450, -300)
    shade = node(mat, unreal.MaterialExpressionCustom, -100, -300,
                 code="float d=dot(normalize(N),normalize(float3(.35,-.55,.76))); return d<.18 ? .60 : (d<.46 ? .80 : 1.0);",
                 output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                 desc="Three terrain tones with the project's fixed art light")
    inp = unreal.CustomInput()
    inp.set_editor_property("input_name", "N")
    shade.set_editor_property("inputs", [inp])
    assert EDIT.connect_material_expressions(normal, "", shade, "N")
    multiply = node(mat, unreal.MaterialExpressionMultiply, 200, 0)
    assert EDIT.connect_material_expressions(blend, "", multiply, "A")
    assert EDIT.connect_material_expressions(shade, "", multiply, "B")
    assert unreal.MaterialNodeService.disconnect_output(MATERIAL, "BaseColor")
    assert EDIT.connect_material_property(multiply, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.layout_material_expressions(mat)
    EDIT.recompile_material(mat)
    save_asset(mat)
    layer_paths = []
    for layer in MANIFEST["layers"]:
        result = unreal.LandscapeMaterialService.create_layer_info_object(layer["name"], ASSETS + "/Layers", True)
        assert result.success, result.error_message
        asset = mark(unreal.load_asset(result.asset_path))
        save_asset(asset)
        layer_paths.append(result.asset_path)
    sea = mark(unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_CombatIsland_Sea", ASSETS + "/Materials",
                      unreal.Material, unreal.MaterialFactoryNew()))
    assert sea
    sea.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    color = node(sea, unreal.MaterialExpressionVectorParameter, -300, 0, parameter_name="SeaColor",
                 default_value=unreal.LinearColor(.022, .13, .22, 1))
    assert EDIT.connect_material_property(color, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.recompile_material(sea)
    save_asset(sea)
    data = {"success": True, "material": made.asset_path, "layer_info_paths": layer_paths,
            "sea_material": sea.get_path_name(), "dirty": dirty()}
    (FILES / "materials.json").write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    return data


def create_terrain():
    owned_world()
    result = unreal.LandscapeService.create_landscape(unreal.Vector(*MANIFEST["actor_location_cm"]), unreal.Rotator(0, 0, 0),
            unreal.Vector(*MANIFEST["actor_scale"]), 1, 255, 16, 16, LABEL)
    assert result.success, result.error_message
    actor = landscape()
    actor.set_folder_path("CombatIsland/Terrain")
    actor.set_editor_property("tags", [OWNER])
    info = unreal.LandscapeService.get_landscape_info(LABEL)
    return {"success": True, "landscape": result.actor_label, "components": info.num_components,
            "resolution": [info.resolution_x, info.resolution_y], "dirty": dirty()}


def import_height():
    actor = landscape()
    result = unreal.LandscapeService.import_heightmap(LABEL, str(FILES / MANIFEST["height_png"]))
    assert result.success, result.error_message
    LIB.set_metadata_tag(owned_world(), "GuLi.ArtImport.Heightmap", str(FILES / MANIFEST["height_png"]))
    return {"success": True, "resolution": result.resolution, "location": list(actor.get_actor_location().to_tuple()),
            "scale": list(actor.get_actor_scale3d().to_tuple()), "dirty": dirty()}


def import_weights():
    actor = landscape()
    data = json.loads((FILES / "materials.json").read_text(encoding="utf-8"))
    layers = [unreal.load_asset(path) for path in data["layer_info_paths"]]
    assert len(layers) == 6 and all(layers)
    assert unreal.LandscapeMaterialService.assign_material_to_landscape(LABEL, MATERIAL,
            {str(layer.get_editor_property("layer_name")): layer.get_path_name() for layer in layers})
    assert unreal.GuLiLandscapeAuthoringLibrary.register_target_layers(actor, layers)
    registered = [str(n) for n in actor.get_target_layer_names()]
    assert set(registered) == {l["name"] for l in MANIFEST["layers"]}, registered
    assert unreal.GuLiLandscapeAuthoringLibrary.import_target_layer_weights(actor, layers,
                str(FILES / MANIFEST["packed_weights"])), "Packed weight import failed"
    return {"success": True, "registered_layers": registered, "dirty": dirty()}


VIEWS = {
    "01_topdown": {"location": [0, 0, 270000], "rotation": {"pitch": -90, "yaw": 90, "roll": 0}, "fov": 65},
    "02_overview": {"location": [0, -200000, 155000], "target": [0, 0, 1500], "fov": 65},
    "03_commander_SW": {"location": [-39000, -56000, 31000], "target": [-39000, -35000, 900], "fov": 65},
    "04_east_canyon": {"location": [88000, -40000, 42000], "target": [40000, 5000, 2600], "fov": 65},
    "05_northwest_ridge": {"location": [-85000, 65000, 23000], "target": [-35000, 38000, 4500], "fov": 60},
}


def scene():
    owned_world()
    assert not any(a.get_actor_label() == "CombatIsland_SeaLevel_0m" for a in ACTORS.get_all_level_actors())
    plane = ACTORS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    assert plane
    plane.set_actor_label("CombatIsland_SeaLevel_0m")
    plane.set_folder_path("CombatIsland/Terrain")
    plane.set_actor_scale3d(unreal.Vector(2300, 2300, 1))
    component = plane.get_editor_property("static_mesh_component")
    assert component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Plane"))
    component.set_material(0, unreal.load_asset(SEA_MATERIAL))
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    component.set_cast_shadow(False)
    plane.set_editor_property("tags", [OWNER])
    component.set_collision_profile_name("NoCollision", True)
    plane.set_actor_enable_collision(False)
    cam = ACTORS.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(), unreal.Rotator())
    assert cam
    cam.set_actor_label("CombatIsland_ReviewCamera")
    cam.set_folder_path("CombatIsland/Review")
    cam.set_editor_property("tags", [OWNER])
    (FILES / "camera_presets.json").write_text(json.dumps(VIEWS, indent=2), encoding="utf-8")
    return {"success": True, "sea_level_cm": 0, "sea_collision": str(component.get_collision_enabled()), "dirty": dirty()}


def finalize_sea():
    owned_world()
    sea = next(a for a in ACTORS.get_all_level_actors() if a.get_actor_label() == "CombatIsland_SeaLevel_0m")
    component = sea.get_editor_property("static_mesh_component")
    component.set_collision_profile_name("NoCollision", True)
    sea.set_actor_enable_collision(False)
    assert component.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
    return {"success": True, "sea_collision": str(component.get_collision_enabled()),
            "profile": str(component.get_collision_profile_name()), "actor_collision": sea.get_actor_enable_collision()}


def collision_checks():
    landscape()
    points = list(MANIFEST["checkpoints"])
    layout = json.loads((FILES / "GaeaExports/layout.json").read_text(encoding="utf-8"))
    for route in layout["routes"]:
        for i, point in enumerate(route["points"]):
            points.append({"label": route["name"] + "_Route_" + str(i), "world_xy_cm": [point[0] * 100, point[1] * 100]})
    # Trace within the same surface cell, avoiding Chaos's float ray/vertex corner ambiguity.
    offset_cm = 0.25
    starts = [unreal.Vector(p["world_xy_cm"][0] + offset_cm, p["world_xy_cm"][1] + offset_cm, 20000) for p in points]
    ends = [unreal.Vector(p["world_xy_cm"][0] + offset_cm, p["world_xy_cm"][1] + offset_cm, -5000) for p in points]
    hits = unreal.LandscapeService.batch_line_trace(starts, ends)
    assert len(hits) == len(points)
    results = []
    for p, hit in zip(points, hits):
        height = unreal.LandscapeService.get_height_at_location(LABEL,
                    p["world_xy_cm"][0] + offset_cm, p["world_xy_cm"][1] + offset_cm)
        results.append({"label": p["label"], "hit": hit.hit, "actor": hit.actor_name,
            "location_cm": list(hit.hit_location.to_tuple()), "normal": list(hit.hit_normal.to_tuple()),
            "height_data_cm": height.height, "error_cm": abs(hit.hit_location.z - height.height)})
    failures = [r for r in results if not r["hit"] or r["actor"] != LABEL or r["error_cm"] > 1.0]
    return {"success": not failures, "method": "Editor visibility line traces, complex Landscape collision; no PIE",
            "xy_offset_from_vertex_cm": offset_cm,
            "count": len(results), "max_error_cm": max(r["error_cm"] for r in results),
            "failures": failures, "samples": results}


def view(name, capture=False):
    owned_world()
    unreal.SystemLibrary.execute_console_command(owned_world(), "MODE EM_Default")
    views = json.loads((FILES / "camera_presets.json").read_text(encoding="utf-8"))
    preset = views[name]
    location = unreal.Vector(*preset["location"])
    rotation = unreal.Rotator(**preset["rotation"]) if "rotation" in preset else unreal.MathLibrary.find_look_at_rotation(
                location, unreal.Vector(*preset["target"]))
    cam = next(a for a in ACTORS.get_all_level_actors() if a.get_actor_label() == "CombatIsland_ReviewCamera")
    cam.set_actor_location(location, False, False)
    cam.set_actor_rotation(rotation, False)
    cam.get_component_by_class(unreal.CameraComponent).set_field_of_view(preset["fov"])
    unreal.EditorLevelLibrary.set_level_viewport_camera_info(location, rotation)
    unreal.ViewportService.set_viewport_type("perspective")
    unreal.ViewportService.set_view_mode("lit")
    unreal.ViewportService.set_fov(preset["fov"])
    unreal.ViewportService.set_realtime(True)
    unreal.ViewportService.set_game_view(True)
    unreal.ViewportService.set_exposure(True, 0.0)
    ACTORS.set_selected_level_actors([])
    path = FILES / "Evidence" / (name + ".png")
    if capture:
        task = unreal.AutomationLibrary.take_high_res_screenshot(1920, 1080, str(path), cam,
                       delay=1.0, force_game_view=True)
        assert task
    return {"success": True, "view": name, "location": list(location.to_tuple()),
            "rotation": {"pitch": rotation.pitch, "yaw": rotation.yaw, "roll": rotation.roll},
            "screenshot": str(path) if capture else None}


def verify():
    actor = landscape()
    info = unreal.LandscapeService.get_landscape_info(LABEL)
    samples = []
    for point in MANIFEST["checkpoints"]:
        result = unreal.LandscapeService.get_height_at_location(LABEL, *point["world_xy_cm"])
        assert result.valid, point["label"]
        exact = unreal.LandscapeService.get_height_in_region(LABEL, *point["vertex"], 1, 1)
        assert len(exact) == 1
        weights = unreal.LandscapeService.get_layer_weights_at_location(LABEL, *point["world_xy_cm"])
        samples.append({"label": point["label"], "vertex": point["vertex"], "world_xy_cm": point["world_xy_cm"],
                       "expected_height_cm": point["expected_height_cm"], "height_sample_cm": result.height,
                       "height_vertex_cm": exact[0], "weights": [str(w) for w in weights]})
    export = FILES / "Evidence/height_ue_readback.png"
    assert unreal.LandscapeService.export_heightmap(LABEL, str(export))
    layer_exports = []
    for layer in MANIFEST["layers"]:
        path = FILES / "Evidence" / ("weight_ue_readback_" + layer["name"] + ".png")
        result = unreal.LandscapeService.export_weight_map(LABEL, layer["name"], str(path))
        assert result.success, str(result)
        layer_exports.append(str(path))
    return {"success": True, "map": owned_world().get_path_name(), "actor_label": LABEL,
            "resolution": [info.resolution_x, info.resolution_y], "components": info.num_components,
            "component_size_quads": info.component_size_quads, "location": list(info.location.to_tuple()),
            "scale": list(info.scale.to_tuple()), "material": info.material_path,
            "target_layers": [str(n) for n in actor.get_target_layer_names()],
            "samples": samples, "height_export": str(export), "layer_exports": layer_exports, "dirty": dirty()}


def save():
    owned_world()
    before = dirty()
    assert all(p == MAP for p in before["maps"]), before
    assert all(p.startswith(ASSETS + "/") for p in before["assets"]), before
    for path in LIB.list_assets(ASSETS, recursive=True, include_folder=False):
        save_asset(unreal.load_asset(path))
    assert LEVELS.save_current_level()
    return {"success": True, "map": MAP, "dirty": dirty()}


def reopen():
    owned_world()
    before = dirty()
    assert not before["maps"] and not before["assets"], before
    assert LEVELS.load_level(MAP)
    actor = landscape()
    info = unreal.LandscapeService.get_landscape_info(LABEL)
    sea = next(a for a in ACTORS.get_all_level_actors() if a.get_actor_label() == "CombatIsland_SeaLevel_0m")
    collision = sea.get_editor_property("static_mesh_component").get_collision_enabled()
    assert collision == unreal.CollisionEnabled.NO_COLLISION and not sea.get_actor_enable_collision()
    assert info.resolution_x == 4081 and info.resolution_y == 4081 and info.num_components == 256
    assert set(str(n) for n in actor.get_target_layer_names()) == {l["name"] for l in MANIFEST["layers"]}
    assert info.material_path.split(".")[0] == MATERIAL
    return {"success": True, "world": owned_world().get_path_name(),
            "landscape_components": info.num_components, "resolution": [info.resolution_x, info.resolution_y],
            "collision_components": len(actor.get_components_by_class(unreal.LandscapeHeightfieldCollisionComponent)),
            "sea_collision": str(collision), "target_layers": [str(n) for n in actor.get_target_layer_names()],
            "dirty": dirty()}


actions = {"setup": setup, "materials": materials, "create_terrain": create_terrain, "import_height": import_height,
           "import_weights": import_weights, "scene": scene, "finalize_sea": finalize_sea,
           "collision_checks": collision_checks, "verify": verify, "save": save, "reopen": reopen}
if ISLAND_ACTION.startswith("view:"):
    result = view(ISLAND_ACTION.split(":", 1)[1], ISLAND_CAPTURE)
else:
    result = actions[ISLAND_ACTION]()
unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=False))
