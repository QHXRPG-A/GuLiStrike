"""Deploy the Wingman render mesh and map-specific FlightNav bake assets.

Intended invocation (while the editor is open and PIE is stopped):

    python Scripts/ue_exec.py Scripts/deploy_wingman_flightnav_assets.py

The deployment is deliberately conservative:

* it refuses to start when any content or map package is dirty;
* it never edits the marketplace source mesh;
* it only adopts assets/actors carrying this script's provenance marker;
* it validates every bake before saving the exact packages it dirtied;
* each committed unit is complete and a later run safely resumes remaining maps;
* it writes an atomic JSON report outside Content even when deployment fails.

Do not run this from PIE. Do not run it concurrently with Live Coding, a build,
or another editor automation task.
"""

from __future__ import annotations

import json
import math
import os
import tempfile
import traceback
from dataclasses import dataclass
from typing import Any, Iterable, Sequence

import unreal


SCHEMA_VERSION = 1
OWNER_MARKER = "GuLi.WingmanFlightNav.Deployment.v1"
VOLUME_MARKER = "GuLiFlightNavigation.Managed.v1"

SOURCE_MESH = "/Game/Assets/Arma/fly-01/space_mouse_fbx.space_mouse_fbx"
DERIVED_MESH = "/Game/GuLiStrike/Wingman/SM_Wingman_Mass"
DERIVED_MESH_OBJECT = DERIVED_MESH + ".SM_Wingman_Mass"

# LOD0 plus three generated reductions. Screen thresholds are explicit so the
# deployed asset does not depend on workstation/editor defaults.
LOD_REDUCTION_SETTINGS = (
    (1.00, 1.00),
    (0.50, 0.50),
    (0.20, 0.20),
    (0.08, 0.08),
)

NAV_DATA_ROOT = "/Game/GuLiStrike/Navigation/Baked"
DEFAULT_TARGET_MAPS = (
    "/Game/Maps/LVL_CommanderMassPrototype",
    "/Game/Maps/LVL_Main",
    "/Game/Maps/LVL_ShipTest",
)
_requested_maps = tuple(
    value.strip()
    for value in os.environ.get("GULI_FLIGHTNAV_TARGET_MAPS", "").split(";")
    if value.strip()
)
if any(value not in DEFAULT_TARGET_MAPS for value in _requested_maps):
    raise RuntimeError(
        "GULI_FLIGHTNAV_TARGET_MAPS contains an unsupported map: "
        + repr(_requested_maps)
    )
TARGET_MAPS = _requested_maps or DEFAULT_TARGET_MAPS
SKIP_DERIVED_MESH = os.environ.get("GULI_FLIGHTNAV_SKIP_DERIVED_MESH", "") == "1"

PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
REPORT_PATH = os.path.join(
    PROJECT_DIR,
    "TestResults",
    "WingmanPlan",
    "AssetDeployment",
    "asset_deployment.json",
)

# Centimetres. Every finite gameplay actor used to derive the map bounds may be
# a ship spawn/carrier location, so the padding must contain the complete
# authored double-ring formation around that point. The extra recovery margin
# covers the 15 m agent radius plus ordinary fixed-wing correction without
# silently shrinking the authored 900 m outer ring on small test maps.
FORMATION_OUTER_RING_RADIUS = 90_000.0
FORMATION_AGENT_RADIUS = 1_500.0
FORMATION_RECOVERY_MARGIN = 10_000.0
FORMATION_VERTICAL_OFFSET = 15_000.0
FORMATION_NAVIGATION_HORIZONTAL_CLEARANCE = (
    FORMATION_OUTER_RING_RADIUS
    + FORMATION_AGENT_RADIUS
    + FORMATION_RECOVERY_MARGIN
)
FORMATION_NAVIGATION_VERTICAL_CLEARANCE = (
    FORMATION_VERTICAL_OFFSET
    + FORMATION_AGENT_RADIUS
    + FORMATION_RECOVERY_MARGIN
)

# Bounds are derived from finite level-actor bounds, padded, then expanded to
# these minimum extents. Hard maxima make accidental sky-dome or world-scale
# captures fail loudly instead of producing an unusable bake.
BOUNDS_PADDING = (
    FORMATION_NAVIGATION_HORIZONTAL_CLEARANCE,
    FORMATION_NAVIGATION_HORIZONTAL_CLEARANCE,
    FORMATION_NAVIGATION_VERTICAL_CLEARANCE,
)
MINIMUM_VOLUME_EXTENT = (125_000.0, 125_000.0, 40_000.0)
MAXIMUM_VOLUME_EXTENT = (600_000.0, 600_000.0, 250_000.0)
MAXIMUM_SOURCE_ACTOR_EXTENT = 1_000_000.0
BOUNDS_RELATIVE_TOLERANCE = 0.005
BOUNDS_ABSOLUTE_TOLERANCE = 10.0

BAKE_SETTINGS = {
    # Arena-scale global guidance remains coarse; 15 m local sphere sweeps
    # handle near-field detail. Depth 6 yields ~130 m leaves on the 8.36 km maps
    # and avoids shipping hundreds of MB of redundant portal topology.
    "minimum_cell_size": 8_000.0,
    # Requirement: Wingman logical collision/navigation radius is 15 m.
    "agent_radius": FORMATION_AGENT_RADIUS,
    "maximum_depth": 6,
    "maximum_nodes": 200_000,
    "maximum_cells": 100_000,
    "face_coordinate_tolerance": 0.1,
    "minimum_portal_span": 25.0,
    "trace_complex": False,
}

IGNORED_BOUNDS_CLASS_TOKENS = (
    "atmosphere",
    "brush",
    "cloud",
    "decals",
    "directionallight",
    "exponentialheightfog",
    "levelbounds",
    "levelscript",
    "lightmass",
    "navmesh",
    "postprocess",
    "reflectioncapture",
    "skylight",
    "sky",
    "volume",
    "worldpartition",
    "worldsettings",
)


@dataclass
class Bounds:
    minimum: tuple[float, float, float]
    maximum: tuple[float, float, float]

    @property
    def center(self) -> tuple[float, float, float]:
        return tuple((lo + hi) * 0.5 for lo, hi in zip(self.minimum, self.maximum))

    @property
    def extent(self) -> tuple[float, float, float]:
        return tuple((hi - lo) * 0.5 for lo, hi in zip(self.minimum, self.maximum))


class DeploymentError(RuntimeError):
    pass


def _atomic_write_json(path: str, value: dict[str, Any]) -> None:
    directory = os.path.dirname(path)
    os.makedirs(directory, exist_ok=True)
    handle, temporary_path = tempfile.mkstemp(
        prefix="asset_deployment_", suffix=".json.tmp", dir=directory
    )
    try:
        with os.fdopen(handle, "w", encoding="utf-8") as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2, sort_keys=True)
            stream.write("\n")
        os.replace(temporary_path, path)
    except Exception:
        try:
            os.unlink(temporary_path)
        except OSError:
            pass
        raise


def _package_name(package: Any) -> str:
    if package is None:
        return ""
    try:
        return str(package.get_name())
    except Exception:
        return str(package.get_path_name())


def _dirty_packages() -> list[Any]:
    packages = list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    packages.extend(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
    unique: dict[str, Any] = {}
    for package in packages:
        unique[_package_name(package)] = package
    return [unique[name] for name in sorted(unique)]


def _dirty_package_names() -> list[str]:
    return [_package_name(package) for package in _dirty_packages()]


def _require_clean_editor(context: str) -> None:
    dirty = _dirty_package_names()
    if dirty:
        raise DeploymentError(
            f"{context}: editor has dirty packages; save or discard them first: {dirty}"
        )


def _require_no_pie(editor_subsystem: Any) -> None:
    game_world = editor_subsystem.get_game_world()
    if game_world is not None:
        raise DeploymentError("PIE/SIE is active; stop play before asset deployment")


def _object_path(asset: Any) -> str:
    return str(asset.get_path_name()) if asset is not None else ""


def _outermost(asset: Any) -> Any:
    package = asset.get_outermost()
    if package is None:
        raise DeploymentError(f"Object has no outermost package: {_object_path(asset)}")
    return package


def _asset_exists(path: str) -> bool:
    return bool(unreal.EditorAssetLibrary.does_asset_exist(path))


def _load_asset(path: str) -> Any:
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        raise DeploymentError(f"Unable to load asset: {path}")
    return asset


def _get_metadata(asset: Any, key: str) -> str:
    try:
        return str(unreal.EditorAssetLibrary.get_metadata_tag(asset, key) or "")
    except Exception:
        return str(unreal.EditorAssetLibrary.get_metadata_tag(asset, unreal.Name(key)) or "")


def _set_metadata(asset: Any, key: str, value: str) -> None:
    try:
        unreal.EditorAssetLibrary.set_metadata_tag(asset, key, value)
    except Exception:
        unreal.EditorAssetLibrary.set_metadata_tag(asset, unreal.Name(key), value)


def _require_owned_asset(asset: Any, expected_source: str) -> None:
    owner = _get_metadata(asset, "GuLi.Deployment.Owner")
    source = _get_metadata(asset, "GuLi.Deployment.Source")
    if owner != OWNER_MARKER or source != expected_source:
        raise DeploymentError(
            "Refusing to modify an existing unowned asset "
            f"{_object_path(asset)} (owner={owner!r}, source={source!r})"
        )


def _ensure_exact_dirty_packages(expected: Iterable[Any], context: str) -> list[Any]:
    expected_names = {_package_name(package) for package in expected if package is not None}
    dirty = _dirty_packages()
    unexpected = [
        _package_name(package)
        for package in dirty
        if _package_name(package) not in expected_names
    ]
    if unexpected:
        raise DeploymentError(
            f"{context}: unrelated packages became dirty during deployment: {unexpected}"
        )
    return dirty


def _save_exact_packages(expected: Iterable[Any], context: str) -> list[str]:
    dirty = _ensure_exact_dirty_packages(expected, context)
    dirty_names = [_package_name(package) for package in dirty]
    if dirty and not unreal.EditorLoadingAndSavingUtils.save_packages(dirty, True):
        raise DeploymentError(f"{context}: save_packages returned false for {dirty_names}")
    still_dirty = [
        name for name in _dirty_package_names() if name in set(dirty_names)
    ]
    if still_dirty:
        raise DeploymentError(f"{context}: packages remain dirty after save: {still_dirty}")
    return dirty_names


def _vector_tuple(value: Any) -> tuple[float, float, float]:
    return float(value.x), float(value.y), float(value.z)


def _finite_vector(values: Sequence[float]) -> bool:
    return len(values) == 3 and all(math.isfinite(value) for value in values)


def _class_name(obj: Any) -> str:
    try:
        return str(obj.get_class().get_name())
    except Exception:
        return type(obj).__name__


def _is_exact_class(obj: Any, expected_class: Any) -> bool:
    try:
        return str(obj.get_class().get_path_name()) == str(expected_class.get_path_name())
    except Exception:
        return False


def _actor_tags(actor: Any) -> set[str]:
    try:
        return {str(tag) for tag in actor.get_editor_property("tags")}
    except Exception:
        return set()


def _set_actor_tag(actor: Any, tag: str) -> None:
    tags = _actor_tags(actor)
    tags.add(tag)
    actor.set_editor_property("tags", [unreal.Name(value) for value in sorted(tags)])


def _is_managed_volume(actor: Any, map_name: str) -> bool:
    tags = _actor_tags(actor)
    return VOLUME_MARKER in tags and f"GuLiFlightNavigation.Map.{map_name}" in tags


def _actor_label(actor: Any) -> str:
    try:
        return str(actor.get_actor_label())
    except Exception:
        return str(actor.get_name())


def _derive_level_bounds(
    actors: Sequence[Any], managed_volume: Any | None
) -> tuple[Bounds, dict[str, Any]]:
    minima = [math.inf, math.inf, math.inf]
    maxima = [-math.inf, -math.inf, -math.inf]
    included: list[str] = []
    skipped: list[dict[str, str]] = []

    for actor in actors:
        if actor is managed_volume:
            continue
        class_name = _class_name(actor)
        normalized_class = class_name.lower()
        if any(token in normalized_class for token in IGNORED_BOUNDS_CLASS_TOKENS):
            skipped.append({"actor": _actor_label(actor), "reason": class_name})
            continue
        try:
            origin, extent = actor.get_actor_bounds(False)
            origin_values = _vector_tuple(origin)
            extent_values = tuple(abs(value) for value in _vector_tuple(extent))
        except Exception as error:
            skipped.append({"actor": _actor_label(actor), "reason": str(error)})
            continue
        if not _finite_vector(origin_values) or not _finite_vector(extent_values):
            skipped.append({"actor": _actor_label(actor), "reason": "non-finite bounds"})
            continue
        if max(extent_values) <= 0.1:
            continue
        if max(extent_values) > MAXIMUM_SOURCE_ACTOR_EXTENT:
            skipped.append(
                {"actor": _actor_label(actor), "reason": "oversized decorative bounds"}
            )
            continue
        for axis in range(3):
            minima[axis] = min(minima[axis], origin_values[axis] - extent_values[axis])
            maxima[axis] = max(maxima[axis], origin_values[axis] + extent_values[axis])
        included.append(_actor_label(actor))

    if not included:
        raise DeploymentError("No finite gameplay actor bounds were found in the map")

    raw = Bounds(tuple(minima), tuple(maxima))
    center = raw.center
    requested_extent = tuple(
        max(raw.extent[axis] + BOUNDS_PADDING[axis], MINIMUM_VOLUME_EXTENT[axis])
        for axis in range(3)
    )
    for axis, extent in enumerate(requested_extent):
        if extent > MAXIMUM_VOLUME_EXTENT[axis]:
            raise DeploymentError(
                "Derived map bounds exceed the deployment hard limit: "
                f"axis={axis}, extent={extent}, limit={MAXIMUM_VOLUME_EXTENT[axis]}"
            )

    bounds = Bounds(
        tuple(center[axis] - requested_extent[axis] for axis in range(3)),
        tuple(center[axis] + requested_extent[axis] for axis in range(3)),
    )
    audit = {
        "included_actor_count": len(included),
        "included_actor_sample": sorted(included)[:32],
        "skipped_actor_count": len(skipped),
        "skipped_actor_sample": skipped[:32],
        "raw_center": list(raw.center),
        "raw_extent": list(raw.extent),
        "volume_center": list(bounds.center),
        "volume_extent": list(bounds.extent),
        "formation_navigation_contract": {
            "outer_ring_radius": FORMATION_OUTER_RING_RADIUS,
            "agent_radius": FORMATION_AGENT_RADIUS,
            "recovery_margin": FORMATION_RECOVERY_MARGIN,
            "vertical_offset": FORMATION_VERTICAL_OFFSET,
            "required_horizontal_clearance_from_gameplay_bounds": (
                FORMATION_NAVIGATION_HORIZONTAL_CLEARANCE
            ),
            "required_vertical_clearance_from_gameplay_bounds": (
                FORMATION_NAVIGATION_VERTICAL_CLEARANCE
            ),
            "satisfied": all(
                bounds.extent[axis] + BOUNDS_ABSOLUTE_TOLERANCE
                >= raw.extent[axis] + BOUNDS_PADDING[axis]
                for axis in range(3)
            ),
        },
    }
    return bounds, audit


def _approximately_equal(actual: float, expected: float) -> bool:
    return math.isclose(
        actual,
        expected,
        rel_tol=BOUNDS_RELATIVE_TOLERANCE,
        abs_tol=BOUNDS_ABSOLUTE_TOLERANCE,
    )


def _configure_volume_brush(volume: Any, bounds: Bounds) -> dict[str, Any]:
    center = unreal.Vector(*bounds.center)
    volume.set_actor_location(center, False, False)

    _origin, current_extent = volume.get_actor_bounds(False)
    current_extent_values = _vector_tuple(current_extent)
    if any(value <= 0.1 or not math.isfinite(value) for value in current_extent_values):
        raise DeploymentError(
            "The editor Volume actor factory did not create a valid box brush; "
            f"current extent is {current_extent_values}"
        )

    current_scale = _vector_tuple(volume.get_actor_scale3d())
    new_scale = tuple(
        current_scale[axis] * bounds.extent[axis] / current_extent_values[axis]
        for axis in range(3)
    )
    if not _finite_vector(new_scale) or any(value <= 0.0 for value in new_scale):
        raise DeploymentError(f"Invalid computed volume scale: {new_scale}")
    volume.set_actor_scale3d(unreal.Vector(*new_scale))

    actual_origin, actual_extent = volume.get_actor_bounds(False)
    actual_center_values = _vector_tuple(actual_origin)
    actual_extent_values = _vector_tuple(actual_extent)
    if not all(
        _approximately_equal(actual_center_values[axis], bounds.center[axis])
        and _approximately_equal(actual_extent_values[axis], bounds.extent[axis])
        for axis in range(3)
    ):
        raise DeploymentError(
            "FlightNav volume bounds verification failed: "
            f"center={actual_center_values}, extent={actual_extent_values}, "
            f"expected_center={bounds.center}, expected_extent={bounds.extent}"
        )

    return {
        "center": list(actual_center_values),
        "extent": list(actual_extent_values),
        "scale": list(_vector_tuple(volume.get_actor_scale3d())),
    }


def _result_with_error(result: Any, operation: str) -> tuple[bool, str]:
    if isinstance(result, tuple):
        if not result:
            raise DeploymentError(f"{operation} returned an empty tuple")
        success = bool(result[0])
        error = "" if len(result) < 2 else str(result[1])
        return success, error
    if isinstance(result, bool):
        return result, ""
    # UE 5.7 Python exposes these UFUNCTIONs' FText out parameter as the sole
    # return value: empty text means success; failure text contains the reason.
    if result is not None:
        error = str(result)
        return not error, error
    return False, f"{operation} returned None"


def _make_bake_settings() -> Any:
    settings_type = getattr(unreal, "GuLiFlightNavBakeSettings", None)
    if settings_type is None:
        raise DeploymentError(
            "FGuLiFlightNavBakeSettings is unavailable to Python; verify the "
            "GuLiFlightNavigationEditor module is loaded"
        )
    settings = settings_type()
    for name, value in BAKE_SETTINGS.items():
        settings.set_editor_property(name, value)
    return settings


def _flightnav_types() -> tuple[Any, Any, Any]:
    data_class = unreal.load_class(
        None, "/Script/GuLiFlightNavigationRuntime.GuLiFlightNavigationData"
    )
    volume_class = unreal.load_class(
        None, "/Script/GuLiFlightNavigationRuntime.GuLiFlightNavigationVolume"
    )
    editor_library = getattr(unreal, "GuLiFlightNavigationEditorLibrary", None)
    if data_class is None or volume_class is None or editor_library is None:
        raise DeploymentError(
            "FlightNav reflected classes are unavailable. Build the plugin and restart "
            "the editor before running deployment."
        )
    return data_class, volume_class, editor_library


def _validate_mesh(mesh: Any, context: str) -> dict[str, Any]:
    if not isinstance(mesh, unreal.StaticMesh):
        raise DeploymentError(f"{context} is not a StaticMesh: {_class_name(mesh)}")
    bounds = mesh.get_bounding_box()
    dimensions = bounds.max - bounds.min
    dimension_values = _vector_tuple(dimensions)
    if not _finite_vector(dimension_values) or min(dimension_values) <= 0.0:
        raise DeploymentError(f"{context} has invalid bounds: {dimension_values}")
    if dimension_values[0] < max(dimension_values[1], dimension_values[2]):
        raise DeploymentError(
            f"{context} no longer has +X as its dominant forward axis: {dimension_values}"
        )
    return {
        "object_path": _object_path(mesh),
        "dimensions": list(dimension_values),
        "dominant_axis": "+X",
        "material_slots": len(mesh.get_editor_property("static_materials")),
    }


def _configure_mass_mesh(mesh: Any) -> dict[str, Any]:
    subsystem_type = getattr(unreal, "StaticMeshEditorSubsystem", None)
    options_type = getattr(unreal, "StaticMeshReductionOptions", None)
    setting_type = getattr(unreal, "StaticMeshReductionSettings", None)
    if subsystem_type is None or options_type is None or setting_type is None:
        raise DeploymentError(
            "StaticMeshEditorSubsystem reduction types are unavailable; "
            "the derived Wingman mesh cannot satisfy its LOD contract"
        )
    subsystem = unreal.get_editor_subsystem(subsystem_type)
    if subsystem is None:
        raise DeploymentError("StaticMeshEditorSubsystem is unavailable")

    repaired_materials = []
    static_materials = list(mesh.get_editor_property("static_materials"))
    for slot_index, static_material in enumerate(static_materials):
        if static_material.get_editor_property("material_interface") is not None:
            continue
        slot_name = str(static_material.get_editor_property("material_slot_name"))
        candidate_path = f"/Game/Assets/Arma/fly-01/{slot_name}.{slot_name}"
        material = unreal.load_asset(candidate_path)
        if material is None:
            material = unreal.load_asset(
                "/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"
            )
        if material is None:
            raise DeploymentError(
                f"Unable to repair missing material slot {slot_index} ({slot_name})"
            )
        mesh.set_material(slot_index, material)
        repaired_materials.append(
            {"slot_index": slot_index, "slot_name": slot_name, "material": str(material.get_path_name())}
        )

    remaining_missing_materials = [
        index
        for index, static_material in enumerate(mesh.get_editor_property("static_materials"))
        if static_material.get_editor_property("material_interface") is None
    ]
    if remaining_missing_materials:
        raise DeploymentError(
            f"Derived Wingman mesh still has missing materials: {remaining_missing_materials}"
        )

    reductions = []
    for triangle_fraction, screen_size in LOD_REDUCTION_SETTINGS:
        setting = setting_type()
        setting.set_editor_property("percent_triangles", triangle_fraction)
        setting.set_editor_property("screen_size", screen_size)
        reductions.append(setting)
    options = options_type()
    options.set_editor_property("auto_compute_lod_screen_size", False)
    options.set_editor_property("reduction_settings", reductions)
    generated_lod_count = int(subsystem.set_lods(mesh, options))
    if generated_lod_count != len(LOD_REDUCTION_SETTINGS):
        raise DeploymentError(
            "Static mesh LOD generation returned an unexpected count: "
            f"{generated_lod_count} != {len(LOD_REDUCTION_SETTINGS)}"
        )

    # The ISM components are also NoCollision at runtime. Removing authored
    # simple/convex collision here prevents accidental collision if the visual
    # mesh is reused by another representation asset.
    if int(subsystem.get_simple_collision_count(mesh)) > 0 or int(
        subsystem.get_convex_collision_count(mesh)
    ) > 0:
        subsystem.remove_collisions(mesh)
    simple_collision_count = int(subsystem.get_simple_collision_count(mesh))
    convex_collision_count = int(subsystem.get_convex_collision_count(mesh))
    if simple_collision_count != 0 or convex_collision_count != 0:
        raise DeploymentError(
            "Derived Wingman visual mesh still has authored collision: "
            f"simple={simple_collision_count}, convex={convex_collision_count}"
        )

    return {
        "lod_count": int(subsystem.get_lod_count(mesh)),
        "lod_reduction_settings": [
            {"percent_triangles": triangles, "screen_size": screen}
            for triangles, screen in LOD_REDUCTION_SETTINGS
        ],
        "simple_collision_count": simple_collision_count,
        "convex_collision_count": convex_collision_count,
        "repaired_materials": repaired_materials,
        # Material atlasing cannot be inferred safely from nine marketplace
        # slots, so preserve visual fidelity and report it explicitly.
        "material_section_policy": "preserve_source_slots_until_authored_atlas",
    }


def _deploy_derived_mesh(report: dict[str, Any]) -> None:
    _require_clean_editor("before derived mesh deployment")
    source = _load_asset(SOURCE_MESH)
    source_record = _validate_mesh(source, "source mesh")
    source_package_name = _package_name(_outermost(source))

    created = False
    if _asset_exists(DERIVED_MESH):
        derived = _load_asset(DERIVED_MESH)
        _require_owned_asset(derived, SOURCE_MESH)
    else:
        unreal.EditorAssetLibrary.make_directory("/Game/GuLiStrike/Wingman")
        derived = unreal.EditorAssetLibrary.duplicate_asset(SOURCE_MESH, DERIVED_MESH)
        if derived is None:
            raise DeploymentError(
                f"Unable to duplicate {SOURCE_MESH} to {DERIVED_MESH}"
            )
        created = True

    derived_record = _validate_mesh(derived, "derived mesh")
    bounds_relative_delta = []
    for source_dimension, derived_dimension in zip(
        source_record["dimensions"], derived_record["dimensions"]
    ):
        delta = abs(derived_dimension - source_dimension) / max(
            abs(source_dimension), 1.0
        )
        bounds_relative_delta.append(delta)
    # Mesh reduction may conservatively expand the union bounds across generated
    # LODs. Reject true scale/axis corruption, while allowing the measured 5%
    # reduction overshoot from the marketplace source.
    if any(delta > 0.08 for delta in bounds_relative_delta):
        raise DeploymentError(
            "Derived mesh bounds drift more than 8% from the source mesh: "
            f"{bounds_relative_delta}"
        )
    if source_record["material_slots"] != derived_record["material_slots"]:
        raise DeploymentError("Derived mesh material slots differ from the source mesh")

    mass_mesh_contract = _configure_mass_mesh(derived)

    _set_metadata(derived, "GuLi.Deployment.Owner", OWNER_MARKER)
    _set_metadata(derived, "GuLi.Deployment.Source", SOURCE_MESH)
    _set_metadata(derived, "GuLi.Wingman.ForwardAxis", "+X")
    _set_metadata(derived, "GuLi.Wingman.Schema", str(SCHEMA_VERSION))
    saved = _save_exact_packages([_outermost(derived)], "derived mesh")
    if source_package_name in _dirty_package_names():
        raise DeploymentError("Marketplace source mesh package became dirty")

    report["derived_mesh"] = {
        "created": created,
        "source": source_record,
        "derived": derived_record,
        "bounds_relative_delta": bounds_relative_delta,
        "mass_mesh_contract": mass_mesh_contract,
        "saved_packages": saved,
    }


def _create_or_load_nav_data(data_class: Any, map_path: str) -> tuple[Any, str, bool]:
    map_name = map_path.rsplit("/", 1)[-1]
    asset_name = f"DA_FlightNav_{map_name}"
    asset_directory = f"{NAV_DATA_ROOT}/{map_name}"
    asset_path = f"{asset_directory}/{asset_name}"
    if _asset_exists(asset_path):
        asset = _load_asset(asset_path)
        if not _is_exact_class(asset, data_class):
            raise DeploymentError(
                f"Existing navigation asset has the wrong class: {asset_path}"
            )
        _require_owned_asset(asset, map_path)
        return asset, asset_path, False

    unreal.EditorAssetLibrary.make_directory(asset_directory)
    factory = unreal.DataAssetFactory()
    try:
        factory.set_editor_property("data_asset_class", data_class)
    except Exception:
        # Some engine builds infer the concrete class from create_asset's argument.
        pass
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        asset_name, asset_directory, data_class, factory
    )
    if asset is None:
        raise DeploymentError(f"Unable to create navigation data asset: {asset_path}")
    _set_metadata(asset, "GuLi.Deployment.Owner", OWNER_MARKER)
    _set_metadata(asset, "GuLi.Deployment.Source", map_path)
    _set_metadata(asset, "GuLi.FlightNav.Schema", str(SCHEMA_VERSION))
    return asset, asset_path, True


def _find_or_spawn_volume(
    actor_subsystem: Any, volume_class: Any, map_name: str, center: tuple[float, float, float]
) -> tuple[Any, bool]:
    actors = list(actor_subsystem.get_all_level_actors())
    managed = [actor for actor in actors if _is_managed_volume(actor, map_name)]
    if len(managed) > 1:
        raise DeploymentError(
            f"Multiple managed FlightNav volumes exist for {map_name}: "
            f"{[_actor_label(actor) for actor in managed]}"
        )
    if managed:
        volume = managed[0]
        if not _is_exact_class(volume, volume_class):
            raise DeploymentError(
                f"Managed actor is not AGuLiFlightNavigationVolume: {_actor_label(volume)}"
            )
        return volume, False

    expected_label = f"FlightNav_{map_name}"
    collisions = [actor for actor in actors if _actor_label(actor) == expected_label]
    if collisions:
        raise DeploymentError(
            f"Actor label {expected_label} already exists without the deployment marker"
        )

    volume = actor_subsystem.spawn_actor_from_class(
        volume_class, unreal.Vector(*center), unreal.Rotator()
    )
    if volume is None:
        raise DeploymentError(
            f"Unable to spawn AGuLiFlightNavigationVolume in {map_name}"
        )
    volume.set_actor_label(expected_label, False)
    try:
        volume.set_folder_path(unreal.Name("GuLiStrike/Navigation"))
    except Exception:
        unreal.log_warning("Unable to set FlightNav actor folder; continuing")
    _set_actor_tag(volume, VOLUME_MARKER)
    _set_actor_tag(volume, f"GuLiFlightNavigation.Map.{map_name}")
    return volume, True


def _navigation_data_record(data: Any) -> dict[str, Any]:
    metadata = data.get_editor_property("metadata")
    record = {
        "object_path": _object_path(data),
        "node_count": len(data.get_editor_property("nodes")),
        "cell_count": len(data.get_editor_property("cells")),
        "portal_count": len(data.get_editor_property("portals")),
        "link_count": len(data.get_editor_property("links")),
    }
    for property_name in (
        "format_version",
        "definition_revision",
        "content_checksum",
        "geometry_signature",
        "settings_hash",
        "source_world_package",
        "source_volume_path",
    ):
        try:
            value = metadata.get_editor_property(property_name)
            record[property_name] = str(value) if property_name.startswith("source_") else int(value)
        except Exception as error:
            record[property_name + "_error"] = str(error)
    return record


def _deploy_map(
    map_path: str,
    data_class: Any,
    volume_class: Any,
    editor_library: Any,
    editor_subsystem: Any,
    actor_subsystem: Any,
) -> dict[str, Any]:
    _require_clean_editor(f"before loading {map_path}")
    current_world = editor_subsystem.get_editor_world()
    current_path = (
        _package_name(_outermost(current_world)) if current_world is not None else ""
    )
    if current_path == map_path:
        world = current_world
    else:
        # CPython Unreal wrappers participate in FPyReferenceCollector. Drop our
        # last script-side world wrapper before LoadMap or UE's leak guard will
        # correctly abort the switch.
        current_world = None
        world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    if world is None:
        raise DeploymentError(f"Unable to load map: {map_path}")
    loaded_path = _package_name(_outermost(world))
    if loaded_path != map_path:
        raise DeploymentError(f"Loaded the wrong map: {loaded_path} != {map_path}")
    _require_no_pie(editor_subsystem)
    _require_clean_editor(f"after loading {map_path}")

    map_name = map_path.rsplit("/", 1)[-1]
    current_actors = list(actor_subsystem.get_all_level_actors())
    managed_before = next(
        (actor for actor in current_actors if _is_managed_volume(actor, map_name)), None
    )
    bounds, bounds_audit = _derive_level_bounds(current_actors, managed_before)

    data, data_path, data_created = _create_or_load_nav_data(data_class, map_path)
    volume, volume_created = _find_or_spawn_volume(
        actor_subsystem, volume_class, map_name, bounds.center
    )
    volume.modify()
    volume_bounds = _configure_volume_brush(volume, bounds)
    _set_actor_tag(volume, VOLUME_MARKER)
    _set_actor_tag(volume, f"GuLiFlightNavigation.Map.{map_name}")
    volume.set_editor_property("navigation_data", data)
    volume.set_editor_property("navigation_enabled", True)
    volume.set_editor_property("query_priority", 100)

    settings = _make_bake_settings()
    success, bake_error = _result_with_error(
        editor_library.bake_volume(volume, settings), "BakeVolume"
    )
    if not success:
        raise DeploymentError(f"BakeVolume failed for {map_name}: {bake_error}")
    if not data.get_editor_property("nodes") or not data.get_editor_property("cells"):
        raise DeploymentError(
            f"BakeVolume reported success for {map_name} but produced no nodes/cells"
        )
    valid, validation_error = _result_with_error(
        editor_library.validate_navigation_data(data), "ValidateNavigationData"
    )
    if not valid:
        raise DeploymentError(
            f"FlightNav validation failed for {map_name}: {validation_error}"
        )
    source_valid, source_validation_error = _result_with_error(
        editor_library.validate_volume(volume, settings), "ValidateVolume"
    )
    if not source_valid:
        raise DeploymentError(
            f"FlightNav source validation failed for {map_name}: "
            f"{source_validation_error}"
        )

    # Volume may be an external actor. Save only the exact world/data/actor
    # packages dirtied by this unit, and reject concurrent unrelated edits.
    expected_packages = [_outermost(world), _outermost(data), _outermost(volume)]
    saved_packages = _save_exact_packages(
        expected_packages, f"FlightNav deployment for {map_name}"
    )

    record = {
        "map": map_path,
        "data_asset": data_path,
        "data_created": data_created,
        "volume_created": volume_created,
        "volume_label": _actor_label(volume),
        "volume_bounds": volume_bounds,
        "bounds_audit": bounds_audit,
        "bake_settings": dict(BAKE_SETTINGS),
        "navigation_data": _navigation_data_record(data),
        "saved_packages": saved_packages,
        "valid": True,
    }
    return record


def _restore_map(original_map: str, report: dict[str, Any]) -> None:
    if not original_map.startswith("/Game/"):
        report["restore"] = {
            "success": False,
            "reason": f"Original editor world was not a saved /Game package: {original_map}",
        }
        return
    dirty = _dirty_package_names()
    if dirty:
        report["restore"] = {
            "success": False,
            "reason": "Refusing to switch maps while packages remain dirty",
            "dirty_packages": dirty,
        }
        return
    world = unreal.EditorLoadingAndSavingUtils.load_map(original_map)
    report["restore"] = {
        "success": world is not None,
        "map": original_map,
    }


def main() -> dict[str, Any]:
    report: dict[str, Any] = {
        "schema": SCHEMA_VERSION,
        "success": False,
        "owner_marker": OWNER_MARKER,
        "engine_version": unreal.SystemLibrary.get_engine_version(),
        "project_dir": PROJECT_DIR,
        "target_maps": list(TARGET_MAPS),
        "committed_maps": [],
        "maps": [],
    }
    editor_subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    original_world = editor_subsystem.get_editor_world()
    original_map = _package_name(_outermost(original_world)) if original_world else ""
    report["original_map"] = original_map
    # Never retain a Python wrapper to a UWorld across LoadMap.
    original_world = None

    try:
        _require_no_pie(editor_subsystem)
        _require_clean_editor("deployment preflight")
        if not _asset_exists(SOURCE_MESH):
            raise DeploymentError(f"Source mesh is missing: {SOURCE_MESH}")
        for map_path in TARGET_MAPS:
            if not _asset_exists(map_path):
                raise DeploymentError(f"Target map is missing: {map_path}")

        data_class, volume_class, editor_library = _flightnav_types()
        _make_bake_settings()  # Reflection preflight before the first write.
        report["preflight"] = {"success": True, "dirty_packages": []}

        if SKIP_DERIVED_MESH:
            report["derived_mesh"] = {
                "skipped": True,
                "reason": "GULI_FLIGHTNAV_SKIP_DERIVED_MESH=1",
            }
        else:
            _deploy_derived_mesh(report)
        for map_path in TARGET_MAPS:
            map_record = _deploy_map(
                map_path,
                data_class,
                volume_class,
                editor_library,
                editor_subsystem,
                actor_subsystem,
            )
            report["maps"].append(map_record)
            report["committed_maps"].append(map_path)
            _atomic_write_json(REPORT_PATH, report)

        _require_clean_editor("after deployment")
        report["success"] = True
    except Exception as error:
        report["success"] = False
        report["error"] = str(error)
        report["traceback"] = traceback.format_exc()
        report["dirty_packages_after_failure"] = _dirty_package_names()
        unreal.log_error("Wingman/FlightNav asset deployment failed: " + str(error))
    finally:
        # A failed map unit is never saved by the script. If it is still dirty,
        # keep the editor on that map instead of risking an implicit save while
        # restoring the caller's original map; the report names every package.
        try:
            if report["success"]:
                _restore_map(original_map, report)
        except Exception as restore_error:
            report["restore"] = {
                "success": False,
                "reason": str(restore_error),
                "traceback": traceback.format_exc(),
            }
            report["success"] = False
        _atomic_write_json(REPORT_PATH, report)

    if not report["success"]:
        raise DeploymentError(report.get("error", "Deployment or map restore failed"))
    unreal.log("Wingman/FlightNav asset deployment report: " + REPORT_PATH)
    return report


RESULT = main()
