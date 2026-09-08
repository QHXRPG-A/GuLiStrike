"""Read-only validation for one active 2-client + dedicated-server building PIE."""

import json
import traceback

import unreal


def vector_tuple(vector):
    return [float(value) for value in vector.to_tuple()]


def is_one(vector):
    return all(abs(value - 1.0) <= 0.001 for value in vector)


result = {"success": False, "worlds": [], "errors": []}
try:
    worlds = sorted(
        [
            world
            for world in unreal.ObjectIterator(unreal.World)
            if "UEDPIE_" in world.get_path_name()
        ],
        key=lambda world: world.get_path_name(),
    )
    if len(worlds) != 3:
        raise RuntimeError(f"Expected three PIE worlds, found {len(worlds)}")

    signatures = []
    for world in worlds:
        buildings = unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.GuLiPlacedBuilding
        )
        entry = {
            "world": world.get_path_name(),
            "expected_role": "dedicated_server" if "UEDPIE_0_" in world.get_path_name() else "client",
            "building_count": len(buildings),
        }
        if len(buildings) != 1:
            result["worlds"].append(entry)
            result["errors"].append(
                f"{entry['world']} has {len(buildings)} buildings instead of one"
            )
            continue

        building = buildings[0]
        boxes = building.get_components_by_class(unreal.BoxComponent)
        meshes = building.get_components_by_class(unreal.StaticMeshComponent)
        nav_modifiers = building.get_components_by_class(unreal.NavModifierComponent)
        if len(boxes) != 1 or len(meshes) != 1 or len(nav_modifiers) != 1:
            raise RuntimeError(
                f"{entry['world']} component counts are box={len(boxes)}, "
                f"mesh={len(meshes)}, nav={len(nav_modifiers)}"
            )

        box = boxes[0]
        mesh = meshes[0]
        nav_modifier = nav_modifiers[0]
        building_type = str(building.get_editor_property("building_type"))
        team = str(building.get_editor_property("team"))
        builder_guid = building.get_editor_property("builder_player_guid").to_string()
        actor_scale = vector_tuple(building.get_actor_scale3d())
        visual_scale = vector_tuple(mesh.get_editor_property("relative_scale3d"))
        box_extent = vector_tuple(box.get_unscaled_box_extent())
        mesh_asset = mesh.get_editor_property("static_mesh")
        area_class = nav_modifier.get_editor_property("area_class")
        signature = [building_type, team, builder_guid]
        signatures.append(signature)
        entry.update(
            {
                "actor": building.get_name(),
                "type": building_type,
                "team": team,
                "builder_guid": builder_guid,
                "replicates": bool(building.get_editor_property("replicates")),
                "net_dormancy": str(building.get_editor_property("net_dormancy")),
                "actor_scale": actor_scale,
                "collision_profile": str(box.get_collision_profile_name()),
                "collision_enabled": str(box.get_collision_enabled()),
                "collision_object_type": str(box.get_collision_object_type()),
                "collision_extent": box_extent,
                "visual_scale": visual_scale,
                "visual_collision": str(mesh.get_collision_enabled()),
                "visual_mesh": mesh_asset.get_path_name() if mesh_asset else None,
                "nav_area": area_class.get_path_name() if area_class else None,
            }
        )
        result["worlds"].append(entry)

        checks = {
            "replicates": entry["replicates"],
            "outpost_type": "OUTPOST" in building_type,
            "assigned_team": "UNASSIGNED" not in team,
            "valid_guid": builder_guid not in ("", "00000000000000000000000000000000"),
            "actor_scale_one": is_one(actor_scale),
            "visual_scale_one": is_one(visual_scale),
            "block_all": entry["collision_profile"] == "BlockAll",
            "world_static": "WORLD_STATIC" in entry["collision_object_type"].upper(),
            "query_and_physics": "QUERY_AND_PHYSICS" in entry["collision_enabled"].upper(),
            "outpost_extent": all(abs(value - 1000.0) <= 1.0 for value in box_extent),
            "visual_no_collision": "NO_COLLISION" in entry["visual_collision"].upper(),
            "outpost_mesh": entry["visual_mesh"]
            == "/Game/GuLiStrike/Buildings/Meshes/SM_OutpostPlaceholder.SM_OutpostPlaceholder",
            "nav_area_null": entry["nav_area"] == "/Script/NavigationSystem.NavArea_Null",
        }
        failed_checks = [name for name, passed in checks.items() if not passed]
        if failed_checks:
            result["errors"].append(
                f"{entry['world']} failed checks: {', '.join(failed_checks)}"
            )

    if len(signatures) == 3 and any(signature != signatures[0] for signature in signatures[1:]):
        result["errors"].append("Replicated type/team/builder GUID differ between PIE worlds")
    result["success"] = len(result["errors"]) == 0 and len(signatures) == 3
except Exception as exc:  # noqa: BLE001
    result["errors"].append(repr(exc))
    result["traceback"] = traceback.format_exc()

unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=False))
