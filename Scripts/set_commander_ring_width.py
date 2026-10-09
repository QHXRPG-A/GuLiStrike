"""Save the 20 cm Commander ring material without rebuilding native modules.

Run in the normal editor with PIE stopped. The native authoring source supplies
the same constants and HLSL for this asset update and future BuildUnitRing runs.
Only the designated material and tagged review actors in the Mass prototype map
are changed; this script never starts gameplay or builds navigation.
"""

import hashlib
import json
import re
from pathlib import Path

import unreal


ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / "Artifacts/MassRingWidth20cm20261007"
SOURCE = ROOT / "Source/GuLiStrike/Commander/Editor/GuLiCommanderRingAssetCommands.cpp"
MESH_PATH = "/Game/Commander/Units/SM_CommanderUnitRing"
MATERIAL_PATH = "/Game/Commander/UI/M_CommanderUnitRing"
MAP = "/Game/Maps/LVL_CommanderMassPrototype"
TAG = "MassRingWidth20cmReview20261007"
EDIT = unreal.MaterialEditingLibrary
report = {"success": False, "native_build_executed": False, "gameplay_started": False}


def read_source():
    text = SOURCE.read_text(encoding="utf-8")

    def constant(name):
        match = re.search(r"constexpr float " + name + r" = ([0-9.]+)f;", text)
        if not match:
            raise RuntimeError("Missing native ring constant: " + name)
        return float(match.group(1))

    def shader(name):
        match = re.search(
            r"constexpr TCHAR " + name + r'\[\] = TEXT\(R"\((.*?)\)"\);',
            text,
            re.DOTALL,
        )
        if not match:
            raise RuntimeError("Missing native ring shader: " + name)
        return match.group(1)

    return {
        "outer": constant("OuterRadius"),
        "inner": constant("InnerRadius"),
        "uv_scale": constant("MeshUVScale"),
        "width": constant("RadialWidthCentimeters"),
        "emissive": constant("EmissiveMultiplier"),
        "opacity": constant("OpacityMultiplier"),
        "local_code": shader("RingLocalDirectionCode"),
        "width_code": shader("RingWorldWidthCode"),
        "source_sha256": hashlib.sha256(text.encode("utf-8")).hexdigest(),
    }


def rebuild(material, settings):
    # UE 5.7's bulk deletion iterates the array while removing its elements.
    # Snapshot the owned expressions so no old node survives the rebuild.
    previous = [expression for expression in unreal.ObjectIterator(unreal.MaterialExpression)
                if expression.get_outer() == material]
    for expression in previous:
        EDIT.delete_material_expression(material, expression)
    assert EDIT.get_num_material_expressions(material) == 0, "Old ring nodes remain"

    def node(cls, x, y, **properties):
        result = EDIT.create_material_expression(material, cls, x, y)
        if not result:
            raise RuntimeError("Could not create " + cls.__name__)
        for name, value in properties.items():
            result.set_editor_property(name, value)
        return result

    def link(source, target, input_name=""):
        if not EDIT.connect_material_expressions(source, "", target, input_name):
            raise RuntimeError("Could not connect material input: " + input_name)

    def custom(code, description, x, y, inputs):
        result = node(
            unreal.MaterialExpressionCustom, x, y,
            code=code, description=description,
            output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        )
        pins = []
        for name in inputs:
            pin = unreal.CustomInput()
            pin.set_editor_property("input_name", name)
            pins.append(pin)
        result.set_editor_property("inputs", pins)
        for name, source in inputs.items():
            link(source, result, name)
        return result

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", False)
    material.set_editor_property("disable_depth_test", True)
    material.set_editor_property("used_with_instanced_static_meshes", True)
    material.set_editor_property("dithered_lod_transition", False)
    color = node(unreal.MaterialExpressionPerInstanceCustomData3Vector, -650, -160,
                 data_index=0, const_default_value=unreal.LinearColor(1, 1, 1, 1))
    alpha = node(unreal.MaterialExpressionPerInstanceCustomData, -650, 140,
                 data_index=3, const_default_value=1.0)
    emissive = node(unreal.MaterialExpressionMultiply, -400, -160,
                    const_b=settings["emissive"])
    opacity = node(unreal.MaterialExpressionMultiply, -400, 140,
                   const_b=settings["opacity"])
    link(color, emissive, "A")
    link(alpha, opacity, "A")
    color_varying = node(unreal.MaterialExpressionVertexInterpolator, -160, -160)
    alpha_varying = node(unreal.MaterialExpressionVertexInterpolator, -160, 140)
    link(emissive, color_varying)
    link(opacity, alpha_varying)
    if not EDIT.connect_material_property(color_varying, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError("Could not connect ring emissive color")
    if not EDIT.connect_material_property(alpha_varying, "", unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError("Could not connect ring opacity")
    uv = node(unreal.MaterialExpressionTextureCoordinate, -1050, 500, coordinate_index=0)
    direction = custom(settings["local_code"], "Ring local radial direction", -800, 500, {"UV": uv})
    world_direction = node(
        unreal.MaterialExpressionTransform, -550, 500,
        transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,
        transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD,
    )
    link(direction, world_direction)
    shape = node(unreal.MaterialExpressionConstant3Vector, -550, 700,
                 constant=unreal.LinearColor(settings["outer"], settings["inner"], 1 / settings["uv_scale"], 1))
    width = node(unreal.MaterialExpressionConstant, -550, 850, r=settings["width"])
    offset = custom(settings["width_code"], "Ring radial width in world centimeters", -250, 500,
                    {"UV": uv, "WorldDirection": world_direction, "NativeShape": shape, "WidthCm": width})
    if not EDIT.connect_material_property(offset, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET):
        raise RuntimeError("Could not connect ring world position offset")
    expression_count = EDIT.get_num_material_expressions(material)
    assert expression_count == 12, f"Unexpected ring expression count: {expression_count}"
    EDIT.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("Could not save Commander ring material")
    saved_offset = EDIT.get_material_property_input_node(material, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    assert saved_offset == offset
    assert str(saved_offset.get_editor_property("code")) == settings["width_code"]
    assert width.get_editor_property("r") == settings["width"] == 20.0
    return {"saved": True, "path": material.get_path_name(), "expressions": 12,
            "radial_width_cm": width.get_editor_property("r"), "wpo_connected": True,
            "instance_transform": "Local to World", "instance_custom_data_floats": 4}


def prepare_review(world, mesh, material, settings):
    if world.get_path_name() != MAP + ".LVL_CommanderMassPrototype":
        return {"saved": False, "reason": "The Mass prototype map is not open", "map": MAP}
    api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    owned = {actor.get_actor_label(): actor for actor in api.get_all_level_actors()
             if TAG in [str(tag) for tag in actor.tags]}
    entries = [("Default150", -2400, 3000, 150.0), ("Pioneer", 0, 3000, 312.5),
               ("WM01", 2400, 3000, 625.0), ("BiZhiMao", 0, -1800, 1629.75439453125)]
    objects = []
    for name, dx, dy, radius in entries:
        label = "MassRing20_" + name
        x, y = -20000.0 + dx, 72000.0 + dy
        hit = unreal.SystemLibrary.line_trace_single(
            world, unreal.Vector(x, y, 30000), unreal.Vector(x, y, -15000),
            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, list(owned.values()), unreal.DrawDebugTrace.NONE)
        if not hit or not hit.to_tuple()[0]:
            raise RuntimeError("No ground below review ring: " + label)
        location = hit.to_tuple()[5] + unreal.Vector(0, 0, 7)
        actor = owned.get(label)
        if not actor:
            actor = api.spawn_actor_from_class(unreal.StaticMeshActor, location)
            owned[label] = actor
        assert isinstance(actor, unreal.StaticMeshActor)
        actor.modify()
        actor.set_actor_label(label)
        actor.set_folder_path("GuLiStrike/Review/MassRingWidth20cm")
        actor.set_editor_property("tags", [TAG])
        actor.set_editor_property("is_editor_only_actor", True)
        actor.set_actor_location(location, False, False)
        actor.set_actor_rotation(unreal.Rotator(0, 0, 0), False)
        actor.set_actor_scale3d(unreal.Vector(radius / settings["outer"], radius / settings["outer"], 0.004))
        component = actor.static_mesh_component
        component.set_static_mesh(mesh)
        component.set_material(0, material)
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        component.set_cast_shadow(False)
        objects.append(actor)
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    rows = []
    for actor in objects:
        component = actor.static_mesh_component
        rows.append({"label": actor.get_actor_label(), "path": actor.get_path_name(),
                     "location_cm": list(actor.get_actor_location().to_tuple()),
                     "scale": list(actor.get_actor_scale3d().to_tuple()),
                     "mesh": component.static_mesh.get_path_name(),
                     "material": component.get_material(0).get_path_name(),
                     "collision": str(component.get_collision_enabled()),
                     "editor_only": actor.get_editor_property("is_editor_only_actor")})
        assert component.get_material(0) == material
        assert component.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION
    return {"saved": True, "map": MAP, "actors": rows, "runtime_verified": False,
            "preview_color": "White: non-instanced review actors use the material's default color"}


def main():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assert editor.get_game_world() is None, "A game session is running; leave it untouched"
    world = editor.get_editor_world()
    assert world, "Keep an editor world open"
    settings = read_source()
    mesh = unreal.load_asset(MESH_PATH)
    material = unreal.load_asset(MATERIAL_PATH)
    assert isinstance(mesh, unreal.StaticMesh) and isinstance(material, unreal.Material)
    existing_offset = EDIT.get_material_property_input_node(material, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    owned_offset = isinstance(existing_offset, unreal.MaterialExpressionCustom) and str(
        existing_offset.get_editor_property("code")) == settings["width_code"]
    assert EDIT.get_num_material_expressions(material) in (6, 12) or owned_offset, "Unexpected ring graph; leave it untouched"
    bounds = mesh.get_bounding_box()
    assert abs(bounds.max.x - settings["outer"]) < 0.01
    assert abs(bounds.max.y - settings["outer"]) < 0.01
    report["material"] = rebuild(material, settings)
    report["source_sha256"] = settings["source_sha256"]
    report["scene"] = prepare_review(world, mesh, material, settings)
    report["success"] = True


OUT.mkdir(parents=True, exist_ok=True)
try:
    main()
except Exception as error:
    report["error"] = f"{type(error).__name__}: {error}"
    raise
finally:
    (OUT / "material-update.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    unreal.log(json.dumps(report, ensure_ascii=False))
    if hasattr(unreal, "MCPythonHelper"):
        unreal.MCPythonHelper.submit_result(json.dumps(report, ensure_ascii=False))
