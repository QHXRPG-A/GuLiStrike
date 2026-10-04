"""Staged environment alignment through commander_editor_python.py.

Set ENV_MATCH_STEP explicitly. All engine operations run on one editor thread.
Only the target map is saved; binary packages are copied, never parsed.
"""

import hashlib
import json
import math
import shutil
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / "Artifacts/EnvironmentAlignment/20261001"
TARGET = "/Game/Maps/LVL_CommanderMassPrototype"
SOURCE = "/Game/Maps/LVL_GroundMech_Demo"
ENV_CLASSES = {"DirectionalLight", "SkyLight", "ExponentialHeightFog", "PostProcessVolume", "SkyAtmosphere", "BP_Sky_Sphere_C"}
PAIRED = {"DirectionalLight": unreal.DirectionalLightComponent, "SkyLight": unreal.SkyLightComponent, "ExponentialHeightFog": unreal.ExponentialHeightFogComponent}
BP_FIELDS = ["Refresh material", "Colors determined by sun position", "Sun height", "Sun brightness", "Horizon Falloff", "Zenith Color", "Horizon color", "Cloud color", "Overall Color", "Cloud speed", "Cloud opacity", "Stars brightness", "Horizon color curve", "Zenith color curve", "Cloud color curve"]
CVARS = ["r.VolumetricFog", "r.VolumetricCloud", "r.SkyAtmosphere", "r.Shadow.Virtual.Enable", "r.DynamicGlobalIlluminationMethod", "r.ReflectionMethod", "r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange", "r.DefaultFeature.AutoExposure", "r.AntiAliasingMethod"]
ALIASES = {"brightness", "movable_whole_scene_dynamic_shadow_radius", "stationary_whole_scene_dynamic_shadow_radius", "relative_translation", "b_absolute_translation", "modify_frequency"}
PP_VOLUME_FIELDS = ["enabled", "unbound", "blend_weight", "blend_radius", "priority", "is_spatially_loaded"]


def write(name, result):
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / name).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")


def read(name):
    return json.loads((OUT / name).read_text(encoding="utf-8"))


def val(v):
    if v is None or isinstance(v, (bool, int, str)):
        return v
    if isinstance(v, float):
        return v if math.isfinite(v) else str(v)
    if isinstance(v, unreal.Object):
        return {"object_path": v.get_path_name(), "class": v.get_class().get_name()}
    if isinstance(v, (list, tuple, unreal.Array)):
        return [val(x) for x in v]
    if hasattr(v, "export_text"):
        return v.export_text()
    return str(v)


def props(obj):
    result = {}
    for key in sorted(dir(obj)):
        if not key.startswith("_"):
            try:
                result[key] = val(obj.get_editor_property(key))
            except Exception:
                pass
    return result


def same(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        return type(a) is type(b) and a == b
    if isinstance(a, (int, float)) and isinstance(b, (int, float)):
        return math.isclose(a, b, rel_tol=1e-7, abs_tol=1e-8)
    return a == b


def actors(world):
    return unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)


def one(world, kind):
    matching = [a for a in actors(world) if a.get_class().get_name() == kind]
    assert len(matching) == 1, (kind, len(matching))
    return matching[0]


def context():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_editor_world()
    assert world.get_path_name() == TARGET + ".LVL_CommanderMassPrototype", world.get_path_name()
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), "PIE is active"
    return editor, world, unreal.load_asset(SOURCE), unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def dirty():
    return {"maps": [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
            "content": [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}


def cvars():
    return {k: unreal.SystemLibrary.get_console_variable_int_value(k) for k in CVARS}


def viewport():
    vp = unreal.ViewportService.get_viewport_info()
    return {"export": vp.export_text(), "location": val(vp.location), "rotation": {"pitch": vp.rotation.pitch, "yaw": vp.rotation.yaw, "roll": vp.rotation.roll},
            "xyz": [vp.location.x, vp.location.y, vp.location.z], "fov": vp.fov,
            "fixed": vp.exposure_fixed, "ev100": vp.exposure_ev100,
            "game_view": vp.is_game_view, "view_mode": unreal.ViewportService.get_view_mode()}


def material(mat):
    lib = unreal.MaterialEditingLibrary
    result = {"parent": val(mat.get_editor_property("parent")), "scalar": {}, "vector": {}, "texture": {}}
    for kind in ["scalar", "vector", "texture"]:
        for name in getattr(lib, "get_" + kind + "_parameter_names")(mat):
            result[kind][str(name)] = val(getattr(mat, "get_" + kind + "_parameter_value")(name))
    return result


def environment(world):
    result = {}
    for kind, cls in PAIRED.items():
        a = one(world, kind)
        result[kind] = {"actor": a.get_path_name(), "transform": a.get_actor_transform().export_text(),
                        "hidden_editor": a.is_hidden_ed(), "hidden_game": a.get_editor_property("hidden"),
                        "component": props(a.get_components_by_class(cls)[0])}
    pp = one(world, "PostProcessVolume")
    result["PostProcessVolume"] = {"actor": pp.get_path_name(), "transform": pp.get_actor_transform().export_text(),
                                  "volume": {k: val(pp.get_editor_property(k)) for k in PP_VOLUME_FIELDS},
                                  "settings": props(pp.get_editor_property("settings"))}
    result["sky_atmospheres"] = [{"actor": a.get_path_name(), "transform": a.get_actor_transform().export_text(), "component": props(a.get_components_by_class(unreal.SkyAtmosphereComponent)[0])}
                                 for a in actors(world) if a.get_class().get_name() == "SkyAtmosphere"]
    sky = [a for a in actors(world) if a.get_class().get_name() == "BP_Sky_Sphere_C"]
    result["sky_spheres"] = []
    for a in sky:
        mesh = a.get_components_by_class(unreal.StaticMeshComponent)[0]
        result["sky_spheres"].append({"actor": a.get_path_name(), "transform": a.get_actor_transform().export_text(),
                                      "properties": {k: val(a.get_editor_property(k)) for k in BP_FIELDS},
                                      "sun_reference": val(a.get_editor_property("Directional light actor")),
                                      "sky_material_reference": val(a.get_editor_property("Sky material")),
                                      "mesh": {**{k: val(mesh.get_editor_property(k)) for k in ["static_mesh", "relative_location", "relative_rotation", "relative_scale3d", "visible", "hidden_in_game", "cast_shadow"]}, "collision_enabled": val(mesh.get_collision_enabled())},
                                      "material": material(mesh.get_material(0))})
    result["volumetric_cloud_count"] = sum(a.get_components_by_class(unreal.VolumetricCloudComponent).__len__() for a in actors(world))
    ws = world.get_world_settings()
    result["world_lighting"] = {k: val(ws.get_editor_property(k)) for k in ["lightmass_settings", "force_no_precomputed_lighting"]}
    return result


def untouched_scene(world):
    result = []
    for a in sorted(actors(world), key=lambda x: x.get_name()):
        if a.get_class().get_name() in ENV_CLASSES:
            continue
        row = {"name": a.get_name(), "class": a.get_class().get_name(), "transform": a.get_actor_transform().export_text()}
        if isinstance(a, unreal.InstancedFoliageActor):
            row["foliage"] = []
            for comp in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
                digest = hashlib.sha256()
                for i in range(comp.get_instance_count()):
                    digest.update(comp.get_instance_transform(i, True).export_text().encode("utf-8"))
                row["foliage"].append({"name": comp.get_name(), "mesh": val(comp.get_editor_property("static_mesh")), "count": comp.get_instance_count(), "transforms_sha256": digest.hexdigest()})
            row["foliage"].sort(key=lambda x: x["name"])
        if isinstance(a, unreal.Landscape):
            info = unreal.LandscapeService.get_landscape_info(a.get_name())
            origin, extent = a.get_actor_bounds(False)
            row["landscape"] = {"components": info.num_components, "resolution": [info.resolution_x, info.resolution_y],
                                "bounds": [val(origin), val(extent)], "layers": [{"name": x.layer_name, "layer_info": x.layer_info_path} for x in info.layers],
                                "properties": {k: val(a.get_editor_property(k)) for k in ["landscape_material", "collision_mip_level", "simple_collision_mip_level", "navigation_geometry_gathering_mode", "cast_shadow", "cast_shadow_as_two_sided"]}}
        if isinstance(a, unreal.WorldSettings):
            row["world_gameplay"] = {k: val(a.get_editor_property(k)) for k in ["default_game_mode", "world_to_meters", "enable_navigation_system", "navigation_system_config"]}
        if a.get_components_by_class(unreal.CameraComponent):
            row["cameras"] = [{"name": c.get_name(), "fov": c.get_editor_property("field_of_view"), "blend": c.get_editor_property("post_process_blend_weight"), "settings": props(c.get_editor_property("post_process_settings"))} for c in a.get_components_by_class(unreal.CameraComponent)]
        result.append(row)
    return result


def metadata(source):
    sky = one(source, "BP_Sky_Sphere_C")
    mesh = sky.get_components_by_class(unreal.StaticMeshComponent)[0]
    packages = {SOURCE, sky.get_class().get_path_name().split(".")[0], mesh.get_editor_property("static_mesh").get_path_name().split(".")[0], mesh.get_material(0).get_editor_property("parent").get_path_name().split(".")[0]}
    for k in ["Horizon color curve", "Zenith color curve", "Cloud color curve"]:
        packages.add(sky.get_editor_property(k).get_path_name().split(".")[0])
    files = []
    for package in sorted(packages):
        base = ROOT / "Content" if package.startswith("/Game/") else Path(unreal.Paths.engine_content_dir()).resolve()
        relative = package.split("/", 2)[2]
        p = base / (relative + (".umap" if package == SOURCE else ".uasset"))
        stat = p.stat()
        files.append({"path": str(p), "size": stat.st_size, "mtime_ns": stat.st_mtime_ns})
    return files


def backup():
    editor, world, source, subsystem = context()
    assert not (OUT / "baseline.json").exists(), "Backup already exists; do not overwrite it"
    assert len([a for a in actors(world) if a.get_class().get_name() == "SkyAtmosphere"]) == 1
    assert not [a for a in actors(world) if a.get_class().get_name() == "BP_Sky_Sphere_C"]
    folder = OUT / "before"
    folder.mkdir(parents=True, exist_ok=True)
    target_file = ROOT / "Content/Maps/LVL_CommanderMassPrototype.umap"
    shutil.copy2(target_file, folder / "LVL_CommanderMassPrototype.disk_before.umap")
    initial_dirty = dirty()
    baseline = {"map": TARGET, "source": SOURCE, "environment": environment(world), "source_environment": environment(source),
                "untouched_scene": untouched_scene(world), "source_metadata": metadata(source),
                "cvars": cvars(), "dirty_before": initial_dirty, "viewport": viewport(),
                "selected_actor_names": [a.get_name() for a in subsystem.get_selected_level_actors()], "actor_count": len(actors(world))}
    if TARGET in initial_dirty["maps"]:
        assert unreal.EditorLoadingAndSavingUtils.save_packages([world.get_outer()], True)
    shutil.copy2(target_file, folder / "LVL_CommanderMassPrototype.live_before.umap")
    baseline["dirty_after_backup"] = dirty()
    write("baseline.json", baseline)
    screenshot = unreal.ScreenshotService.capture_editor_window(str(folder / "editor-before.png"))
    write("before-capture.json", {"success": screenshot.success, "result": screenshot.export_text()})
    return {"success": True, "backup": str(folder), "actor_count": baseline["actor_count"], "preexisting_target_edits_preserved": TARGET in initial_dirty["maps"], "dirty": dirty()}


def copy_lighting():
    editor, world, source, subsystem = context()
    baseline = read("baseline.json")
    assert untouched_scene(world) == baseline["untouched_scene"], "Non-environment scene changed after backup"
    copied = []
    with unreal.ScopedEditorTransaction("Match Commander lighting, fog and post process to GroundMech"):
        for kind, cls in PAIRED.items():
            sa, ta = one(source, kind), one(world, kind)
            sc, tc = sa.get_components_by_class(cls)[0], ta.get_components_by_class(cls)[0]
            ta.modify()
            tc.modify()
            for key, source_value in props(sc).items():
                if key in ALIASES or key.startswith("relative_"):
                    continue
                if not same(source_value, val(tc.get_editor_property(key))):
                    tc.set_editor_property(key, sc.get_editor_property(key))
                    assert same(source_value, val(tc.get_editor_property(key))), (kind, key)
                    copied.append(kind + "." + key)
            ta.set_actor_transform(sa.get_actor_transform(), False, True)
            ta.set_actor_hidden_in_game(sa.get_editor_property("hidden"))
        sp, tp = one(source, "PostProcessVolume"), one(world, "PostProcessVolume")
        tp.modify()
        tp.set_editor_property("settings", sp.get_editor_property("settings"))
        for key in PP_VOLUME_FIELDS:
            tp.set_editor_property(key, sp.get_editor_property(key))
        tp.set_actor_transform(sp.get_actor_transform(), False, True)
        ws, ss = world.get_world_settings(), source.get_world_settings()
        for key in ["lightmass_settings", "force_no_precomputed_lighting"]:
            if val(ws.get_editor_property(key)) != val(ss.get_editor_property(key)):
                ws.modify()
                ws.set_editor_property(key, ss.get_editor_property(key))
    result = {"success": True, "copied_component_parameters": copied, "post_process_full_struct_copied": True, "world_lighting_matched": True, "saved": False}
    write("lighting.json", result)
    return result


def replace_sky():
    editor, world, source, subsystem = context()
    assert read("lighting.json")["success"]
    assert untouched_scene(world) == read("baseline.json")["untouched_scene"]
    source_sky = one(source, "BP_Sky_Sphere_C")
    source_mesh = source_sky.get_components_by_class(unreal.StaticMeshComponent)[0]
    source_mid = source_mesh.get_material(0)
    existing = [a for a in actors(world) if a.get_class().get_name() == "BP_Sky_Sphere_C"]
    assert len(existing) <= 1
    with unreal.ScopedEditorTransaction("Replace Commander atmosphere with GroundMech sky sphere"):
        new_sky = existing[0] if existing else subsystem.duplicate_actor(source_sky, world, unreal.Vector(0, 0, 0))
        # Cross-world duplication can create the actor yet return None in UE 5.7.
        if new_sky is None:
            new_sky = one(world, "BP_Sky_Sphere_C")
        assert new_sky and new_sky.get_world() == world
        new_sky.set_actor_label("Sky Sphere_Commander")
        new_sky.set_folder_path("Environment/Lighting")
        new_sky.modify()
        new_sky.set_actor_transform(source_sky.get_actor_transform(), False, True)
        for key in BP_FIELDS:
            if not same(val(new_sky.get_editor_property(key)), val(source_sky.get_editor_property(key))):
                new_sky.set_editor_property(key, source_sky.get_editor_property(key))
                assert same(val(new_sky.get_editor_property(key)), val(source_sky.get_editor_property(key))), key
        new_sky.set_editor_property("Directional light actor", one(world, "DirectionalLight"))
        new_mesh = new_sky.get_components_by_class(unreal.StaticMeshComponent)[0]
        # This Blueprint's cached MID is instance-read-only; duplication already
        # remaps it into the target map. Keep its cache and mesh in agreement.
        new_mid = new_sky.get_editor_property("Sky material")
        assert new_mid and new_mid.get_path_name().startswith(TARGET + ".")
        assert new_mid.get_editor_property("parent") == source_mid.get_editor_property("parent")
        new_mid.modify()
        for kind in ["scalar", "vector", "texture"]:
            names = getattr(unreal.MaterialEditingLibrary, "get_" + kind + "_parameter_names")(source_mid)
            for name in names:
                getattr(new_mid, "set_" + kind + "_parameter_value")(name, getattr(source_mid, "get_" + kind + "_parameter_value")(name))
        new_mesh.set_material(0, new_mid)
        old_atmospheres = [a for a in actors(world) if a.get_class().get_name() == "SkyAtmosphere"]
        for old in old_atmospheres:
            assert subsystem.destroy_actor(old)
    one(world, "SkyLight").get_components_by_class(unreal.SkyLightComponent)[0].recapture_sky()
    result = {"success": True, "sky_sphere": new_sky.get_path_name(), "removed_atmospheres": len(old_atmospheres),
              "sun_reference": new_sky.get_editor_property("Directional light actor").get_path_name(),
              "sky_material": new_mid.get_path_name(), "material_parameters": material(new_mid), "saved": False}
    write("sky.json", result)
    return result


def differences(source, target, path=""):
    if isinstance(source, dict) and isinstance(target, dict):
        result = []
        for k in sorted(set(source) | set(target)):
            result.extend(differences(source.get(k), target.get(k), path + "." + k))
        return result
    return [] if same(source, target) else [{"property": path, "source": source, "target": target}]


def verify(persistent=False):
    editor, world, source, subsystem = context()
    baseline = read("baseline.json")
    src, tgt = environment(source), environment(world)
    mismatches = []
    for kind in PAIRED:
        for key in ["component", "transform", "hidden_editor", "hidden_game"]:
            mismatches.extend(differences(src[kind][key], tgt[kind][key], kind + "." + key))
    for key in ["volume", "settings", "transform"]:
        mismatches.extend(differences(src["PostProcessVolume"][key], tgt["PostProcessVolume"][key], "PostProcessVolume." + key))
    mismatches.extend(differences(src["world_lighting"], tgt["world_lighting"], "WorldSettings.lighting"))
    assert not tgt["sky_atmospheres"] and not src["sky_atmospheres"]
    assert len(src["sky_spheres"]) == len(tgt["sky_spheres"]) == 1
    for key in ["properties", "transform", "mesh", "material"]:
        mismatches.extend(differences(src["sky_spheres"][0][key], tgt["sky_spheres"][0][key], "SkySphere." + key))
    sky = one(world, "BP_Sky_Sphere_C")
    assert sky.get_editor_property("Directional light actor") == one(world, "DirectionalLight")
    sky_mesh = sky.get_components_by_class(unreal.StaticMeshComponent)[0]
    assert sky.get_editor_property("Sky material") == sky_mesh.get_material(0)
    assert SOURCE not in sky_mesh.get_material(0).get_path_name()
    assert tgt["volumetric_cloud_count"] == src["volumetric_cloud_count"] == 0
    actual_scene = untouched_scene(world)
    # Landscape editor gizmos are transient helpers and disappear on map reload.
    stable_before = [a for a in baseline["untouched_scene"] if a["class"] != "LandscapeGizmoActiveActor"]
    stable_after = [a for a in actual_scene if a["class"] != "LandscapeGizmoActiveActor"]
    assert stable_after == stable_before, "Landscape, foliage, cameras or gameplay objects changed"
    assert environment(source) == baseline["source_environment"], "Source environment changed in memory"
    assert metadata(source) == baseline["source_metadata"], "Source package metadata changed"
    assert cvars() == baseline["cvars"], "Project/editor CVars changed"
    result = {"success": not mismatches, "map": TARGET, "source": SOURCE, "persistent_readback": persistent,
              "mismatches": mismatches, "environment": tgt, "untouched_scene_unchanged": True,
              "ignored_transient_editor_helpers": "LandscapeGizmoActiveActor",
              "source_environment_and_file_metadata_unchanged": True, "global_cvars_unchanged": True,
              "actor_count": len(actors(world)), "dirty": dirty(), "pie_run": False, "native_build_run": False}
    write("reload-readback.json" if persistent else "readback.json", result)
    assert not mismatches, json.dumps(mismatches, ensure_ascii=True)
    return {k: v for k, v in result.items() if k != "environment"}


def save():
    editor, world, source, subsystem = context()
    assert read("readback.json")["success"]
    before = dirty()
    assert unreal.EditorLoadingAndSavingUtils.save_packages([world.get_outer()], True)
    assert TARGET not in dirty()["maps"]
    assert metadata(source) == read("baseline.json")["source_metadata"]
    result = {"success": True, "saved_packages": [TARGET], "dirty_before": before, "dirty_after": dirty(), "source_unchanged": True}
    write("saved.json", result)
    return result


def reload():
    editor, world, source, subsystem = context()
    assert read("saved.json")["success"]
    assert TARGET not in dirty()["maps"], "Target has new unsaved edits; refuse reload"
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(TARGET)
    return {"success": True, "map": TARGET, "next": "reload_verify"}


def capture():
    editor, world, source, subsystem = context()
    label_by_view = {"top": "MassNav_Overview", "ground": "CommanderCamera_Observe_Near35", "commander": "CommanderCamera_Observe_Tactical300"}
    view = globals()["ENV_MATCH_VIEW"]
    camera = next(a for a in actors(world) if a.get_actor_label() == label_by_view[view])
    cc = camera.get_components_by_class(unreal.CameraComponent)[0]
    assert unreal.ViewportService.set_camera_location(camera.get_actor_location())
    assert unreal.ViewportService.set_camera_rotation(camera.get_actor_rotation())
    assert unreal.ViewportService.set_fov(cc.get_editor_property("field_of_view"))
    assert unreal.ViewportService.set_exposure_game_settings()
    assert unreal.ViewportService.set_game_view(True)
    destination = OUT / ("previews/" + view + ".png")
    destination.parent.mkdir(exist_ok=True)
    globals()["ENV_CAPTURE_TASK"] = unreal.AutomationLibrary.take_high_res_screenshot(1600, 900, str(destination), delay=0.75, force_game_view=True)
    result = {"success": True, "view": view, "camera": camera.get_actor_label(), "camera_transform": camera.get_actor_transform().export_text(),
              "viewport": viewport(), "file": str(destination), "capture_requested": True, "pie_run": False}
    write("capture-" + view + ".json", result)
    return result


def restore_viewport():
    editor, world, source, subsystem = context()
    baseline = read("baseline.json")
    vp = baseline["viewport"]
    assert unreal.ViewportService.set_camera_location(unreal.Vector(*vp["xyz"]))
    assert unreal.ViewportService.set_camera_rotation(unreal.Rotator(**vp["rotation"]))
    assert unreal.ViewportService.set_fov(vp["fov"])
    assert unreal.ViewportService.set_exposure(vp["fixed"], vp["ev100"])
    assert unreal.ViewportService.set_game_view(vp["game_view"])
    assert unreal.ViewportService.set_view_mode(vp["view_mode"])
    selection = [a for a in actors(world) if a.get_name() in baseline["selected_actor_names"]]
    selection_fallback = False
    if not selection and "LandscapeGizmoActiveActor_0" in baseline["selected_actor_names"]:
        selection = [a for a in actors(world) if isinstance(a, unreal.Landscape)]
        selection_fallback = True
    subsystem.set_selected_level_actors(selection)
    # Selection callbacks may adjust the editor camera; restore its pose last.
    assert unreal.ViewportService.set_camera_location(unreal.Vector(*vp["xyz"]))
    assert unreal.ViewportService.set_camera_rotation(unreal.Rotator(**vp["rotation"]))
    result = {"success": True, "before": vp, "after": viewport(), "selected_actor_names": [a.get_name() for a in subsystem.get_selected_level_actors()], "landscape_selection_fallback": selection_fallback}
    for key in ["xyz", "rotation", "fov", "fixed", "ev100", "game_view", "view_mode"]:
        assert result["after"][key] == vp[key] or (key == "rotation" and all(abs(result["after"][key][a] - vp[key][a]) < 1e-5 for a in vp[key])), ("Viewport restore mismatch", key)
    write("viewport-restored.json", result)
    return {"success": True, "viewport_restored": True, "selection_restored": result["selected_actor_names"] == baseline["selected_actor_names"], "landscape_selection_fallback": selection_fallback}


STEP = globals().get("ENV_MATCH_STEP", "verify")
try:
    result = {"backup": backup, "copy_lighting": copy_lighting, "replace_sky": replace_sky,
              "verify": verify, "save": save, "reload": reload,
              "reload_verify": lambda: verify(True), "capture": capture, "restore_viewport": restore_viewport}[STEP]()
except Exception:
    import traceback
    result = {"success": False, "step": STEP, "traceback": traceback.format_exc()}
    write("failure-" + STEP + ".json", result)
unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=False))
