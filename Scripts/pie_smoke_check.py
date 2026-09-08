import json

import unreal

OUT = "D:/UE5.7/test1/Data/tmp_pie_check.json"
result = {"errors": []}
try:
    sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = sub.get_game_world()
    result["has_world"] = world is not None
    if world is None:
        raise RuntimeError("PIE world not ready")
    # 通用遍历，按名字匹配，避开类比较问题
    all_actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    result["world_actor_count"] = len(all_actors)
    ship = None
    for a in all_actors:
        if "GuLiStrikeShip" in a.get_name():
            ship = a
            break
    result["ship_name"] = ship.get_name() if ship else None
    if ship:
        result["class_name"] = str(ship.get_class().get_path_name())
        result["tuning_preset"] = str(ship.get_editor_property("tuning_preset"))
        for prop in ("hull_mass", "base_max_speed", "base_acceleration", "yaw_rate",
                     "boost_thrust_multiplier", "orient_to_movement", "hull_mesh_offset"):
            result[prop] = str(ship.get_editor_property(prop))
        parts = []
        for c in ship.get_components_by_class(unreal.GuLiStrikeShipPartComponent):
            info = {
                "part_id": str(c.get_editor_property("part_id")),
                "part_mass": c.get_editor_property("part_mass"),
                "display": str(c.get_editor_property("part_display_name")),
            }
            if isinstance(c, unreal.GuLiStrikeEnginePart):
                info["thrust"] = c.get_editor_property("thrust")
            if isinstance(c, unreal.GuLiStrikeWeaponPart):
                info["damage"] = c.get_editor_property("damage")
                info["fire_rate"] = c.get_editor_property("fire_rate")
                info["muzzle_offset"] = str(c.get_editor_property("muzzle_offset"))
                info["projectile_class"] = str(c.get_editor_property("projectile_class"))
            parts.append(info)
        result["parts"] = parts
except Exception as e:  # noqa: BLE001
    result["errors"].append(repr(e))
with open(OUT, "w", encoding="utf-8") as f:
    json.dump(result, f, ensure_ascii=False, indent=1)
result  # noqa
