"""Install the two damage modifiers and save their review station in the existing Ground range."""
import hashlib
import io
import json
import os
import struct
import sys
import tempfile
import traceback
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
ROGUE = ROOT / "Scripts/GroundMech/Rogue"
sys.path.insert(0, str(ROGUE))
sys.path.insert(0, str(ROGUE.parent))
import install_ai_engine as base
import icon_assets

CARDS = ("EnhancedAOE", "DamageOverTime")
EFFECTS = tuple("Ground" + key for key in CARDS)
NOTE = "GroundDamageBonuses_Review"
INSTRUCTIONS = (
    "红队游骑兵。用 F4 或 gs.GM.Card.Set 17.01 max 和 gs.GM.Card.Set 18.01 max 取得两张牌。"
    "爆破套餐的爆炸伤害满级增加38%。另一局取得强化子弹、二级灼烧子弹和一级灼烧爆裂子弹。"
    "对 GroundBuff_Health750 施加二级灼烧，持续增伤满级使单跳由22.5升至26.55。两张增伤牌满级时，一级爆燃对每个目标造成宿主当时已损生命 × 0.02 × 1.38 × 1.18。"
    "按住扳机的连续直击和分裂枪管同时锁定的多个目标都按各自的单次命中结算。"
)
REPORT = ROOT / "Artifacts/GroundDamageBonus_20261123/install.json"


def check_catalog(manifest, wanted_cards, wanted_configs):
    numbers = manifest["numbers"]
    data = bytes.fromhex("".join(manifest["strings"][f"ground_rogue/catalog/{i}"]
                                 for i in range(int(numbers["ground_rogue/catalog/chunks"]))))
    stream = io.BytesIO(data)
    def integer(s):
        return struct.unpack("<I", s.read(4))[0]
    def blob(s):
        length = integer(s)
        payload = s.read(length)
        base.require(len(payload) == length, "Truncated Ground catalog")
        return payload
    def array(s, get):
        return [get(s) for _ in range(integer(s))]
    def text(s):
        return blob(s).decode("utf-8")
    version = integer(stream)
    base.require(version in (7, 8), "Unknown Ground card catalog version")
    cards = {}
    for _ in range(integer(stream)):
        key = text(stream)
        cards[key] = (integer(stream), array(stream, text), array(stream, text), array(stream, integer))
    effects = io.BytesIO(blob(stream))
    if version == 8:
        for key in cards:
            levels = array(stream, integer)
            if key in wanted_cards:
                expected = wanted_cards[key][0]["RequiredCardLevels"]
                base.require(levels == list(map(int, expected)), key + " prerequisite levels")
    base.require(not stream.read() and integer(effects) == 1, "Invalid effect catalog")
    definitions = {}
    for _ in range(integer(effects)):
        effect_id, effect_version = integer(effects), integer(effects)
        definitions[effect_id] = (text(effects), effect_version, blob(effects))
    base.require(not effects.read(), "Trailing effect data")
    for key, (source_card, effect_id, profile) in wanted_cards.items():
        expected = (source_card["MaxAcquisitions"], source_card["RequiredCardIds"],
                    source_card["ExcludedCardIds"], [effect_id])
        base.require(cards.get(key) == expected, key + " binding")
        config = wanted_configs[profile]
        increments = list(map(float, config["DamageBonusIncrements"]))
        encoded = struct.pack("<BII", config["DamageBonusAxis"], 0, len(increments)) + struct.pack("<" + "f"*len(increments), *increments)
        base.require(definitions.get(effect_id) == ("ground/damage_bonus", 1, encoded), key + " rank data")
    prefix = "ground_rogue/field_damage_traits/"
    fields = {key[len(prefix):]: int(value) for key, value in numbers.items() if key.startswith(prefix)}
    return {"catalog_sha256": hashlib.sha256(data).hexdigest(), "card_count": len(cards),
            "field_damage_traits": fields, "card_ids": sorted(wanted_cards)}


def publish_manifests():
    cards = base.rows(unreal.load_asset(base.DATA+"DT_GuLiStrikeRogueCards_Cards"))
    effects = base.rows(unreal.load_asset(base.DATA+"DT_GuLiStrikeRogueCards_Effects"))
    configs = base.rows(unreal.load_asset(base.DATA+"DT_GuLiStrikeRogueCards_GroundWeaponEffects"))
    selected = {}
    for suffix, profile in zip(CARDS, EFFECTS):
        card = cards["GroundMech_"+suffix+"_Lv1"]
        effect = effects[profile]
        base.require(card["EffectIds"] == [str(effect["Id"])] and effect["ConfigId"] == configs[profile]["Id"], profile)
        selected[card["Id"]] = (card, effect["Id"], profile)
    folder = ROOT / "Config/Hybrid"
    results = {}
    with tempfile.TemporaryDirectory(dir=folder) as stage:
        for name, size, trial in (("200",200,False),("600",600,False),("NoSurvivors",600,True)):
            path = Path(stage) / ("RuntimeManifest-" + name + ".json")
            for result in (json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(size,str(path),trial)),
                           json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(path)))):
                base.require(result.get("success"), str(result))
            manifest = json.loads(path.read_text(encoding="utf-8-sig"))
            results[path.name] = dict(check_catalog(manifest, selected, configs), config_hash=manifest["config_hash"])
        base.require(len({v["catalog_sha256"] for v in results.values()}) == 1, "Map catalogs differ")
        for name in results:
            os.replace(Path(stage)/name, folder/name)
    return results


def prepare_scene():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = editor.get_editor_world()
    base.require(not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), "Open map has unsaved edits")
    if world.get_path_name().split(".")[0] != base.MAP:
        base.require(level.load_level(base.MAP), base.MAP)
        world = editor.get_editor_world()
    labels = {"GroundRogue_Entry","GroundBuff_Health750","GroundBuff_Shield250","GroundBuff_LargeBurnHost","GroundBuff_FriendlyInBurst",NOTE}
    def entities():
        return {a.get_actor_label():a for a in actors.get_all_level_actors() if a.get_actor_label() in labels}
    current = entities()
    base.require(labels-{NOTE} <= set(current), "Missing review entities: " + str(labels-{NOTE}-set(current)))
    marker = current.get(NOTE)
    if marker is None:
        position = current["GroundBuff_Health750"].get_actor_location() + unreal.Vector(0,0,100)
        marker = base.require(actors.spawn_actor_from_class(unreal.Note,position), NOTE)
        marker.set_actor_label(NOTE)
    base.require(isinstance(marker,unreal.Note), NOTE)
    if str(marker.get_editor_property("text")) != INSTRUCTIONS:
        marker.modify()
        marker.set_editor_property("text",INSTRUCTIONS)
        base.require(unreal.EditorLoadingAndSavingUtils.save_map(world,base.MAP), "Save review map")
    base.require(level.load_level(base.MAP), "Reload review map")
    saved=entities()
    base.require(labels == set(saved) and str(saved[NOTE].get_editor_property("text")) == INSTRUCTIONS, "Saved entities")
    summary = {}
    for label,actor in saved.items():
        record = {"class":actor.get_class().get_name(), "position":list(actor.get_actor_location().to_tuple())}
        if label.startswith("GroundBuff_"):
            base.require(isinstance(actor,unreal.GuLiCommanderDeploymentPoint),label)
            record["team"]=str(actor.get_editor_property("team"))
            for field in ("initial_max_health_override","initial_shield","use_in_population_manifest","start_idle"):
                record[field]=actor.get_editor_property(field)
            base.require(record["use_in_population_manifest"] and record["start_idle"],label)
        summary[label]=record
    base.require(summary["GroundBuff_Health750"]["initial_max_health_override"] == 750 and
                 summary["GroundBuff_Health750"]["initial_shield"] == 0, "750 HP baseline")
    base.require(summary["GroundBuff_FriendlyInBurst"]["team"] != summary["GroundBuff_Health750"]["team"], "Opposing teams")
    return {"map":base.MAP, "entities":summary}


def main():
    report={}
    try:
        base.require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),"Active play session")
        unreal.GuLiStrikeRogueCardsGroundWeaponEffectsRow().get_editor_property("damage_bonus_increments")
        report["icon"] = icon_assets.import_catalog(ROOT,[19])
        texts = {"Card.GroundMech."+key+"."+suffix:None for key in CARDS for suffix in ("Title","Description")}
        scope = {
            "GuLiStrikeIcons_Icons": {"DamageOverTime": None},
            "GuLiStrikeGameTexts_Texts": texts,
            "GuLiStrikeRogueCards_GroundWeaponEffects": dict.fromkeys(EFFECTS),
            "GuLiStrikeRogueCards_Effects": dict.fromkeys(EFFECTS),
            "GuLiStrikeRogueCards_Cards": {"GroundMech_" + key + "_Lv1": None for key in CARDS},
        }
        report["tables"] = base.install_tables(scope)
        report["scene"] = prepare_scene()
        report["manifests"] = publish_manifests()
        report["success"] = True
    except Exception:
        report["error"]=traceback.format_exc()
        raise
    finally:
        REPORT.parent.mkdir(parents=True,exist_ok=True)
        REPORT.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")


if __name__ == "__main__":
    main()
