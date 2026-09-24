"""Capture the source editor world and export recoverable terrain, without saving assets."""
import json
import shutil
from pathlib import Path

import unreal


ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / "Artifacts/Map2300/20260923"
MAP = "/Game/Maps/LVL_CommanderMassPrototype"


def value(item, depth=0):
    if item is None or isinstance(item, (bool, int, float, str)):
        return item
    if isinstance(item, unreal.Guid):
        return item.to_string()
    if isinstance(item, unreal.Object):
        return {"path": item.get_path_name(), "class": item.get_class().get_name()}
    if isinstance(item, (unreal.Vector, unreal.Vector2D)):
        return list(item.to_tuple())
    if isinstance(item, unreal.Rotator):
        return [item.pitch, item.yaw, item.roll]
    if isinstance(item, unreal.Transform):
        return {"location": value(item.translation), "rotation": value(item.rotation.rotator()), "scale": value(item.scale3d)}
    if isinstance(item, (unreal.Array, list, tuple)):
        return [value(v, depth + 1) for v in item]
    if hasattr(item, "get_editor_property") and depth < 5:
        return properties(item, depth + 1)
    return str(item)


def properties(obj, depth=0, skip=()):
    result = {}
    for name in dir(obj):
        if name.startswith("_") or name in skip:
            continue
        try:
            result[name] = value(obj.get_editor_property(name), depth)
        except Exception:
            pass
    return result


def write_json(name, obj):
    path = OUT / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, ensure_ascii=False, indent=2), encoding="utf-8")


def backup_package(package):
    if not package.startswith("/Game/"):
        return
    relative = Path("Content") / (package[6:] + ".uasset")
    src, dst = ROOT / relative, OUT / "backup" / relative
    if src.is_file() and not dst.exists():
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)


def main():
    if (OUT / "baseline.json").exists():
        return {"success": True, "already_captured": True, "path": str(OUT / "baseline.json")}
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_editor_world()
    assert world.get_path_name().split(".")[0] == MAP
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    dirty = {
        "maps": [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
        "content": [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
    }
    assert not dirty["maps"] and not dirty["content"], dirty
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    landscape = [a for a in actors if isinstance(a, unreal.LandscapeProxy)]
    assert len(landscape) == 1
    land = landscape[0]
    info = unreal.LandscapeService.get_landscape_info(land.get_name())
    assert info.num_components == 256 and info.resolution_x == 4081, str(info)
    rows = []
    for actor in actors:
        origin, extent = actor.get_actor_bounds(False)
        row = {
            "name": actor.get_name(), "label": actor.get_actor_label(), "path": actor.get_path_name(),
            "class": actor.get_class().get_name(), "transform": value(actor.get_actor_transform()),
            "bounds": {"origin": value(origin), "extent": value(extent)},
            "folder": str(actor.get_folder_path()),
            "properties": properties(actor, skip=("landscape_components", "collision_components", "record")),
            "components": [],
        }
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):
            mesh = component.get_editor_property("static_mesh")
            component_row = {
                "name": component.get_name(), "class": component.get_class().get_name(),
                "mesh": mesh.get_path_name() if mesh else None,
                "materials": [m.get_path_name() if m else None for m in component.get_materials()],
                "world_transform": value(component.get_world_transform()),
                "collision_profile": str(component.get_collision_profile_name()),
                "collision_enabled": str(component.get_collision_enabled()),
            }
            if isinstance(component, unreal.InstancedStaticMeshComponent):
                component_row["instances_world"] = [value(component.get_instance_transform(i, True)) for i in range(component.get_instance_count())]
            row["components"].append(component_row)
        rows.append(row)
    service = unreal.get_editor_subsystem(unreal.GuLiMapAuthoringSubsystem)
    layout = service.get_snapshot()
    density = service.get_density_snapshot()
    assert layout.success and density.success
    write_json("layout-before.json", json.loads(layout.json))
    write_json("density-before.json", json.loads(density.json))
    for type_asset in service.list_types():
        backup_package(type_asset.get_path_name().split(".")[0])
    nav = [row for row in rows if row["class"] in ("RecastNavMesh", "GuLiFlightNavigationVolume", "NavMeshBoundsVolume")]
    height_path = OUT / "terrain/source-height.png"
    height_path.parent.mkdir(parents=True, exist_ok=True)
    assert unreal.LandscapeService.export_heightmap(land.get_name(), str(height_path))
    weights = []
    for layer in info.layers:
        exported = unreal.LandscapeService.export_weight_map(land.get_name(), layer.layer_name, str(OUT / ("terrain/source-weight-" + layer.layer_name + ".png")))
        weights.append({"layer": value(layer), "export": value(exported)})
        assert exported.success, str(exported)
    resource = unreal.load_asset("/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap")
    result = {
        "success": True, "map": MAP, "actors": rows, "dirty_packages": dirty,
        "landscape": {"info": value(info), "properties": properties(land, skip=("landscape_components", "collision_components")), "weights": weights},
        "navigation": nav, "world_settings": properties(world.get_world_settings()),
        "resource": {"version": resource.layout_version, "source_hash": resource.source_hash, "layout_hash": resource.layout_hash,
                     "territories": len(resource.territories), "clusters": len(resource.clusters), "nodes": len(resource.nodes)},
        "viewport": value(unreal.EditorLevelLibrary.get_level_viewport_camera_info()),
    }
    write_json("baseline.json", result)
    return {"success": True, "path": str(OUT / "baseline.json"), "actors": len(rows), "landscape_components": info.num_components, "weights": len(weights), "resource": result["resource"]}


unreal.MCPythonHelper.submit_result(json.dumps(main(), ensure_ascii=False))
