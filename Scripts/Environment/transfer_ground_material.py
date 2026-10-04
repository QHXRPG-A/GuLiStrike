"""Staged, scoped editor migration of the GroundMech landscape material.

Run through Scripts/commander_editor_python.py with TRANSFER_STEP supplied in
the exec namespace. Never reads UE binary package contents or saves all assets.
"""

import hashlib
import json
import shutil
from pathlib import Path

import unreal


ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / "Artifacts/GroundMaterialTransfer/20261001"
MAP = "/Game/Maps/LVL_CommanderMassPrototype"
SOURCE_MAP = "/Game/Maps/LVL_GroundMech_Demo"
SOURCE_MATERIAL = "/Game/StylizedPineEnvironment/Assets/Materials/Landscape/MI_Landscape"
TARGET_MATERIAL = "/Game/GuLiStrike/Environment/Materials/MI_CommanderGround_Pine"
LAYER_ROOT = "/Game/StylizedPineEnvironment/Assets/Misc/LandscapeLayerInfo/LL_"
SOURCE_LAYERS = ["AutoLayer", "Grass_WithFoliage", "Grass", "Dirt_Gravel", "Dirt_Light", "Rock"]


def write(name, value):
    destination = OUT / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def read(name):
    return json.loads((OUT / name).read_text(encoding="utf-8"))


def context():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_editor_world()
    assert world.get_path_name() == MAP + ".LVL_CommanderMassPrototype", world.get_path_name()
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    lands = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Landscape)
    assert len(lands) == 1, len(lands)
    landscape = lands[0]
    info = unreal.LandscapeService.get_landscape_info(landscape.get_name())
    assert info.num_components == 256 and info.resolution_x == 4081 and info.resolution_y == 4081
    return editor, world, landscape, info


def dirty():
    return {
        "maps": [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
        "content": [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
    }


def asset_path(value):
    return value.get_path_name() if value else None


def package_file(package):
    assert package.startswith("/Game/")
    suffix = ".umap" if package in [MAP, SOURCE_MAP] else ".uasset"
    return ROOT / "Content" / (package.removeprefix("/Game/") + suffix)


def file_metadata(path):
    stat = path.stat()
    return {"path": str(path), "size": stat.st_size, "mtime_ns": stat.st_mtime_ns}


def source_metadata():
    material = unreal.load_asset(SOURCE_MATERIAL)
    packages = [SOURCE_MAP, SOURCE_MATERIAL, material.get_editor_property("parent").get_path_name().split(".")[0]]
    packages.extend(LAYER_ROOT + name for name in SOURCE_LAYERS)
    return [file_metadata(package_file(package)) for package in packages]


def scene_snapshot(world, landscape):
    result = []
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    for actor in sorted(actors, key=lambda item: item.get_name()):
        row = {"name": actor.get_name(), "class": actor.get_class().get_name(),
               "transform": actor.get_actor_transform().export_text()}
        if isinstance(actor, unreal.InstancedFoliageActor):
            instances = []
            for component in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
                digest = hashlib.sha256()
                for i in range(component.get_instance_count()):
                    digest.update(component.get_instance_transform(i, True).export_text().encode("utf-8"))
                instances.append({"name": component.get_name(), "mesh": asset_path(component.get_editor_property("static_mesh")),
                                  "count": component.get_instance_count(), "transforms_sha256": digest.hexdigest()})
            row["foliage"] = sorted(instances, key=lambda item: item["name"])
        result.append(row)
    origin, extent = landscape.get_actor_bounds(False)
    collision = {}
    for field in ["collision_mip_level", "simple_collision_mip_level", "navigation_geometry_gathering_mode", "cast_shadow", "cast_shadow_as_two_sided"]:
        collision[field] = str(landscape.get_editor_property(field))
    return {"actors": result, "landscape_bounds": {"origin": list(origin.to_tuple()), "extent": list(extent.to_tuple())},
            "landscape_collision": collision}


def backup():
    editor, world, land, info = context()
    assert not (OUT / "baseline.json").exists(), "Baseline already exists; do not overwrite it."
    assert not unreal.EditorAssetLibrary.does_asset_exist(TARGET_MATERIAL), "Target instance already exists."
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "before").mkdir(exist_ok=True)
    map_file = package_file(MAP)
    shutil.copy2(map_file, OUT / "before/LVL_CommanderMassPrototype.disk_before.umap")
    before_dirty = dirty()
    snapshot = scene_snapshot(world, land)
    assert unreal.LandscapeService.export_heightmap(land.get_name(), str(OUT / "before/height.png"))
    layers = []
    for layer in info.layers:
        destination = OUT / ("before/weight-" + layer.layer_name + ".png")
        exported = unreal.LandscapeService.export_weight_map(land.get_name(), layer.layer_name, str(destination))
        assert exported.success, exported.export_text()
        assert exported.width == 4081 and exported.height == 4081
        layers.append({"name": layer.layer_name, "info": layer.layer_info_path, "weight_png": str(destination)})
    assert {layer["name"] for layer in layers} == {"Base_Layer", "Layer_02", "Layer_03"}
    # Save only the designated target map to retain all of its pre-existing live edits.
    assert unreal.EditorLoadingAndSavingUtils.save_packages([world.get_outer()], True)
    shutil.copy2(map_file, OUT / "before/LVL_CommanderMassPrototype.live_before.umap")
    baseline = {"success": True, "map": MAP, "source_map": SOURCE_MAP, "material_before": asset_path(land.get_editor_property("landscape_material")),
                "layers": layers, "scene": snapshot, "dirty_before": before_dirty, "dirty_after_backup": dirty(),
                "source_metadata": source_metadata(), "viewport": unreal.ViewportService.get_viewport_info().export_text(),
                "view_mode": unreal.ViewportService.get_view_mode(), "viewport_camera": {
                    "location": list(editor.get_level_viewport_camera_info()[0].to_tuple()),
                    "rotation": list(editor.get_level_viewport_camera_info()[1].to_tuple())},
                "target_map_backups": [str(OUT / "before/LVL_CommanderMassPrototype.disk_before.umap"),
                                       str(OUT / "before/LVL_CommanderMassPrototype.live_before.umap")]}
    write("baseline.json", baseline)
    return {"success": True, "baseline": str(OUT / "baseline.json"), "original_weights": len(layers),
            "actor_count": len(snapshot["actors"]), "preexisting_map_edits_preserved": MAP in before_dirty["maps"]}


def bind():
    editor, world, land, info = context()
    baseline = read("baseline.json")
    assert scene_snapshot(world, land) == baseline["scene"], "Scene changed after backup; take a fresh baseline."
    assert asset_path(land.get_editor_property("landscape_material")) == baseline["material_before"]
    source = unreal.load_asset(SOURCE_MATERIAL)
    source_world = unreal.load_asset(SOURCE_MAP)
    source_land = unreal.GameplayStatics.get_all_actors_of_class(source_world, unreal.LandscapeProxy)
    assert len(source_land) == 1 and source_land[0].get_editor_property("landscape_material") == source
    if unreal.EditorAssetLibrary.does_asset_exist(TARGET_MATERIAL):
        # Resume only the unsaved instance created by this migration's first stage.
        assert not package_file(TARGET_MATERIAL).exists()
        instance = unreal.load_asset(TARGET_MATERIAL)
        assert instance.get_editor_property("parent") == source
    else:
        package, name = TARGET_MATERIAL.rsplit("/", 1)
        instance = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, package, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        assert instance
        unreal.MaterialEditingLibrary.set_material_instance_parent(instance, source)
    # UE 5.7's native setter always returns false, including on success.
    # Check the effective parameter below instead of its broken return value.
    unreal.MaterialEditingLibrary.set_material_instance_static_switch_parameter_value(instance, "Spawn_Grass?", False)
    assert instance.get_editor_property("parent") == source
    assert not unreal.MaterialEditingLibrary.get_material_instance_static_switch_parameter_value(instance, "Spawn_Grass?")
    assert unreal.LandscapeService.set_landscape_material(land.get_name(), TARGET_MATERIAL)
    assert land.get_editor_property("landscape_material") == instance
    layer_infos = [unreal.load_asset(LAYER_ROOT + name) for name in SOURCE_LAYERS]
    assert all(layer_infos)
    assert [str(layer.get_editor_property("layer_name")) for layer in layer_infos] == SOURCE_LAYERS
    assert unreal.GuLiLandscapeAuthoringLibrary.register_target_layers(land, layer_infos)
    info = unreal.LandscapeService.get_landscape_info(land.get_name())
    layers = [{"name": layer.layer_name, "info": layer.layer_info_path} for layer in info.layers]
    assert set(SOURCE_LAYERS).issubset({layer["name"] for layer in layers}), layers
    result = {"success": True, "material": instance.get_path_name(), "parent": source.get_path_name(), "spawn_grass": False,
              "persistent_layer_count": len(land.get_editor_property("target_layers")), "layers": layers,
              "resolution": [info.resolution_x, info.resolution_y], "saved": False}
    write("bound.json", result)
    return result


def weights():
    editor, world, land, info = context()
    bound = read("bound.json")
    current_layers = [{"name": layer.layer_name, "info": layer.layer_info_path} for layer in info.layers]
    assert current_layers == bound["layers"], current_layers
    layer_infos = [unreal.load_asset(layer["info"]) for layer in current_layers]
    packed = OUT / "weights-auto-interleaved.raw"
    assert packed.stat().st_size == 4081 * 4081 * len(layer_infos)
    assert unreal.GuLiLandscapeAuthoringLibrary.import_target_layer_weights(land, layer_infos, str(packed))
    result = {"success": True, "painted_vertices": 4081 * 4081, "AutoLayer_weight": 1.0,
              "other_layer_weights": 0.0, "layers": current_layers, "saved": False}
    write("painted.json", result)
    return result


def verify():
    editor, world, land, info = context()
    baseline = read("baseline.json")
    assert scene_snapshot(world, land) == baseline["scene"], "Actor, foliage, bounds or collision changed."
    assert source_metadata() == baseline["source_metadata"], "Source package metadata changed."
    instance = land.get_editor_property("landscape_material")
    assert instance.get_path_name().split(".")[0] == TARGET_MATERIAL
    assert instance.get_editor_property("parent").get_path_name().split(".")[0] == SOURCE_MATERIAL
    assert not unreal.MaterialEditingLibrary.get_material_instance_static_switch_parameter_value(instance, "Spawn_Grass?")
    after = OUT / "after"
    after.mkdir(exist_ok=True)
    assert unreal.LandscapeService.export_heightmap(land.get_name(), str(after / "height.png"))
    weights = []
    for layer in info.layers:
        path = after / ("weight-" + layer.layer_name + ".png")
        exported = unreal.LandscapeService.export_weight_map(land.get_name(), layer.layer_name, str(path))
        assert exported.success, exported.export_text()
        weights.append({"name": layer.layer_name, "info": layer.layer_info_path, "file": str(path),
                        "dimensions": [exported.width, exported.height]})
    samples = []
    for x, y in [(-114900,-114900),(-114900,114900),(114900,-114900),(114900,114900),(0,0),(0,72500)]:
        values = unreal.LandscapeService.get_layer_weights_at_location(land.get_name(),x,y)
        samples.append({"xy": [x,y], "weights": [value.export_text() for value in values]})
    result = {"success": True, "map": MAP, "landscape": land.get_name(), "components": info.num_components,
              "material": instance.get_path_name(), "parent": instance.get_editor_property("parent").get_path_name(),
              "spawn_grass": False, "actors_foliage_bounds_collision_unchanged": True, "source_package_metadata_unchanged": True,
              "weights": weights, "samples": samples, "dirty_packages": dirty()}
    write("readback.json", result)
    return {k: v for k, v in result.items() if k not in ["samples", "weights"]}


def save():
    editor, world, land, info = context()
    assert read("pixel-verification.json")["success"]
    instance = unreal.load_asset(TARGET_MATERIAL)
    before = dirty()
    assert unreal.EditorLoadingAndSavingUtils.save_packages([instance.get_outer(), world.get_outer()], True)
    remaining = dirty()
    assert MAP not in remaining["maps"] and TARGET_MATERIAL not in remaining["content"]
    assert source_metadata() == read("baseline.json")["source_metadata"]
    result = {"success": True, "saved_packages": [MAP, TARGET_MATERIAL], "dirty_before": before,
              "dirty_remaining": remaining, "source_package_metadata_unchanged": True, "pie_run": False, "native_build_run": False}
    write("saved.json", result)
    return result


STEP = globals().get("TRANSFER_STEP", "verify")
try:
    result = {"backup": backup, "bind": bind, "weights": weights, "verify": verify, "save": save}[STEP]()
except Exception:
    import traceback
    result = {"success": False, "step": STEP, "traceback": traceback.format_exc()}
    write("failure-" + STEP + ".json", result)
unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=False))
