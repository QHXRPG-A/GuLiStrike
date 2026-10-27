"""Read structured ricochet PIE state; never capture or inspect rendered output."""
import json
import traceback
from pathlib import Path

import unreal

OUT = Path("D:/UE5.7/test1/ArtSource/UI/GroundMechRogueCards/GroundMech_ChainRicochet/CardProduction_20261026_v1")
REPORT = {"success": False, "assistant_visual_test": False,
          "user_visual_review": "pending", "gameplay_modified": False}
try:
    REPORT["pie_running"] = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert REPORT["pie_running"]
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    assert world
    REPORT["world"] = world.get_path_name()
    assert "LVL_GroundMechRicochetReview" in REPORT["world"]
    directors = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
                 if a.get_class().get_name() == "BP_CardRevealDirector_C"]
    assert len(directors) == 1
    director = directors[0]
    REPORT["director"] = director.get_path_name()
    REPORT["phase"] = director.get_editor_property("Phase")
    REPORT["active_card_count"] = director.get_editor_property("ActiveCardCount")
    assert REPORT["active_card_count"] == 1 and REPORT["phase"] == 1
    imported = json.loads((OUT / "ue-import.json").read_text(encoding="utf-8"))
    REPORT["cards"] = []
    for index in range(3):
        card = director.get_editor_property("Card" + str(index))
        assert card
        face = next(c for c in card.get_components_by_class(unreal.StaticMeshComponent)
                    if c.get_name() == "CardFace")
        material = face.get_material(0)
        parent = material.get_editor_property("parent")
        assert material.get_path_name().split(".")[0] == imported["material"]
        assert parent.get_path_name().split(".")[0] == imported["parent"]
        REPORT["cards"].append({"index": index, "actor": card.get_path_name(),
                                "hidden": bool(card.get_editor_property("hidden")),
                                "face_visible": face.is_visible(),
                                "material": material.get_path_name(), "parent": parent.get_path_name()})
    assert not REPORT["cards"][0]["hidden"] and all(c["hidden"] for c in REPORT["cards"][1:])
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    REPORT["viewport_size"] = list(pc.get_viewport_size())
    REPORT["view_target"] = pc.get_view_target().get_path_name()
    assert pc.get_view_target() == director
    assert all(v > 0 for v in REPORT["viewport_size"])
    REPORT["maximum_tilt"] = director.get_editor_property("MaximumTilt")
    REPORT["success"] = True
except Exception:
    REPORT["error"] = traceback.format_exc()
(OUT / "ue-pie.json").write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.MCPythonHelper.submit_result(json.dumps(REPORT, ensure_ascii=False))
