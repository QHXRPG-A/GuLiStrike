"""Install the three Ground targeting cards, their endpoint VFX and their range."""
import io
import json
import math
import os
from pathlib import Path
import runpy
import struct
import sys
import tempfile
import traceback

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
SCRIPTS = ROOT / "Scripts/GroundMech/Rogue"
sys.path.insert(0, str(SCRIPTS))
import install_ai_engine as base

REPORT = ROOT / "Artifacts/GroundTargetingCards/install.json"
CARDS = ["GroundMech_AIEngine_Lv1", "GroundMech_SplitBarrel_Lv1", "GroundMech_ElectromagneticTech_Lv1"]
EFFECTS = ["GroundAIEngine", "GroundSplitBarrel", "GroundElectromagnetic"]
TEXTS = ["Card.GroundMech.AIEngine.Description"] + [
    "Card.GroundMech." + card + "." + role
    for card in ("SplitBarrel", "ElectromagneticTech") for role in ("Title", "Description")]
SCOPE = {
    "GuLiStrikeRogueCards_Cards": dict.fromkeys(CARDS),
    "GuLiStrikeRogueCards_Effects": dict.fromkeys(EFFECTS),
    "GuLiStrikeRogueCards_GroundWeaponEffects": dict.fromkeys(EFFECTS),
    "GuLiStrikeGameTexts_Texts": dict.fromkeys(TEXTS),
    "GuLiStrikeVfx_Effects": {"GroundElectromagneticArc": None},
}
NOTE = "GroundAIEngine_Instructions"
OBSERVE = "GroundRogue_SplitObserve"
INSTRUCTIONS = ("红队 Ground 席位游骑兵。F4 取得强化子弹三级，再选人工智能引擎。锁满前按住开火应无子弹。"
                "分裂枪管一至三级，每次枪管动作最多分别攻击 2、3、5 个已经锁满的不同敌人。"
                "在 GroundRogue_SplitObserve 侧看蓝靶；移开准星、让目标离圈或走进墙后，观察剩余枪数。"
                "选电磁技术后，逐段观察枪口至命中点的闪电与折射。Enemy_05 带盾，Enemy_06 按兵种初始行为移动。")


def prepare_scene():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world = editor.get_editor_world()
    if world.get_path_name().split(".")[0] != base.MAP:
        base.require(not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), "Open map has pending edits")
        base.require(level.load_level(base.MAP), base.MAP)
        world = editor.get_editor_world()
    enemies = ["GroundRogue_Enemy_" + format(n, "02") for n in range(1, 10)]
    labels = set(enemies) | {"GroundRogue_Entry", "GroundRogue_Friendly", "GroundRogue_Wall", "GroundAmmo_BlockingWall",
                              "GroundRogue_BehindWall", NOTE, OBSERVE}
    def entities():
        return {a.get_actor_label(): a for a in actors.get_all_level_actors() if a.get_actor_label() in labels}
    selected = entities()
    base.require(labels - {OBSERVE} <= set(selected), "Missing range entities " + str(labels-set(selected)))
    if OBSERVE not in selected:
        marker = base.require(actors.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(-2100,48700,1149),
                              unreal.Rotator(0,0,126)), OBSERVE)
        marker.set_actor_label(OBSERVE)
    for n, label in enumerate(enemies, 1):
        actor = selected[label]
        base.require(isinstance(actor, unreal.GuLiCommanderDeploymentPoint) and
                     actor.get_editor_property("team") == unreal.GuLiTeam.BLUE, label)
        if n > 6:
            continue
        for key, value in {"use_in_population_manifest": True, "allow_automatic_fire": False,
                           "start_idle": n != 6, "initial_max_health_override": 100000.,
                           "initial_shield": 55. if n == 5 else 0.}.items():
            if actor.get_editor_property(key) != value:
                actor.modify()
                actor.set_editor_property(key, value)
    note = selected[NOTE]
    if str(note.get_editor_property("text")) != INSTRUCTIONS:
        note.modify()
        note.set_editor_property("text", INSTRUCTIONS)
    base.require(unreal.EditorLoadingAndSavingUtils.save_map(world, base.MAP), "Save range")
    base.require(level.load_level(base.MAP), "Reload saved range")
    selected = entities()
    base.require(set(selected) == labels and str(selected[NOTE].get_editor_property("text")) == INSTRUCTIONS, "Saved labels")
    report = {}
    for label, actor in selected.items():
        row = {"class": actor.get_class().get_name(), "position": list(actor.get_actor_location().to_tuple())}
        if isinstance(actor, unreal.GuLiCommanderDeploymentPoint):
            row.update({key: actor.get_editor_property(key) for key in ("start_idle", "allow_automatic_fire",
                        "use_in_population_manifest", "initial_shield", "initial_max_health_override")})
            row["team"] = str(actor.get_editor_property("team"))
        report[label] = row
    for n, label in enumerate(enemies[:6], 1):
        row = report[label]
        base.require(row["use_in_population_manifest"] and not row["allow_automatic_fire"] and
                     row["start_idle"] == (n != 6) and row["initial_shield"] == (55 if n == 5 else 0), label)
    return report


def check_catalog(manifest):
    count = int(manifest["numbers"]["ground_rogue/catalog/chunks"])
    raw = bytes.fromhex("".join(manifest["strings"][f"ground_rogue/catalog/{i}"] for i in range(count)))
    def integer(stream):
        return struct.unpack("<I", stream.read(4))[0]
    def blob(stream):
        size = integer(stream)
        data = stream.read(size)
        base.require(len(data) == size or size == 0, "Truncated catalog")
        return data
    def array(stream, field):
        return [field(stream) for _ in range(integer(stream))]
    stream = io.BytesIO(raw)
    base.require(integer(stream) == 8, "Ranked catalog version")
    cards = {}
    for _ in range(integer(stream)):
        key = blob(stream).decode()
        cards[key] = [integer(stream), array(stream, lambda s: blob(s).decode()),
                      array(stream, lambda s: blob(s).decode()), array(stream, integer)]
    effects = io.BytesIO(blob(stream))
    for card in cards.values():
        card.append(array(stream, integer))
    base.require(not stream.read(), "Unexpected catalog suffix")
    ref = base.rows(unreal.load_asset(base.DATA+"DT_GuLiStrikeRogueCards_Cards"))
    for name in CARDS:
        row=ref[name]
        expected=[row["MaxAcquisitions"], row["RequiredCardIds"], row["ExcludedCardIds"],
                  list(map(int,row["EffectIds"])), list(map(int,row["RequiredCardLevels"]))]
        base.require(cards[row["Id"]] == expected, name+" runtime prerequisites")
    integer(effects)
    parameters = {}
    for _ in range(integer(effects)):
        ident, version = integer(effects), integer(effects)
        handler = blob(effects).decode()
        parameters[handler] = (ident, version, blob(effects))
    base.require(not effects.read(), "Unexpected effect suffix")
    cfg = base.rows(unreal.load_asset(base.DATA+"DT_GuLiStrikeRogueCards_GroundWeaponEffects"))
    definitions = base.rows(unreal.load_asset(base.DATA+"DT_GuLiStrikeRogueCards_Effects"))
    ai, split, em = [cfg[k] for k in EFFECTS]
    seconds = list(map(float,ai["LockSeconds"]))
    ranges = list(map(float,ai["LockRangeCentimeters"]))
    capacities = list(map(int,split["LockTargetCounts"]))
    slow = list(zip(map(float,em["SlowFractions"]), map(float,em["SlowDurations"]), map(float,em["ShieldDamageBonuses"])))
    expected = [
        struct.pack("<II",0,len(seconds))+b"".join(struct.pack("<ff",s,cm) for s,cm in zip(seconds,ranges))
        +struct.pack("<ff",ai["LockAperturePixels"],ai["LockHexPixels"]),
        struct.pack("<II",0,len(capacities))+b"".join(struct.pack("<I",n) for n in capacities),
        struct.pack("<III",0,em["ArcVfxId"],len(slow))+b"".join(struct.pack("<fff",*rank) for rank in slow)]
    for name, payload in zip(EFFECTS,expected):
        row = definitions[name]
        base.require(parameters.get(row["Handler"]) == (row["Id"],row["Version"],payload), name+" runtime parameters")
    return {row["Id"]: cards[row["Id"]] for row in (ref[k] for k in CARDS)}


def export_manifests(entities):
    folder = ROOT/"Config/Hybrid"
    checked = {}
    with tempfile.TemporaryDirectory(dir=folder) as stage:
        for key, population, trial in (("200",200,False),("600",600,False),("NoSurvivors",600,True)):
            path = Path(stage)/("RuntimeManifest-"+key+".json")
            result=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population,str(path),trial))
            base.require(result.get("success"),str(result))
            result=json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(path)))
            base.require(result.get("success"),str(result))
            manifest=json.loads(path.read_text(encoding="utf-8-sig"))
            cards=check_catalog(manifest)
            for n in range(1,7):
                name=f"GroundRogue_Enemy_{n:02}"
                x,y,_=entities[name]["position"]
                targets=[o for o in manifest["objects"] if o["kind"]==1 and o["definition"]=="unit/1"
                         and o["team"]==2 and float(o["health"])==100000
                         and math.hypot(x-float(o["x"]),y-float(o["y"]))<=220]
                base.require(len(targets)==1, key+"/"+name+" deployment")
                target=targets[0]
                prefix="deployment/"+target["id"].upper()+"/"
                base.require(float(target["shield"])==entities[name]["initial_shield"] and
                             int(manifest["numbers"][prefix+"start_idle"])==int(entities[name]["start_idle"]) and
                             int(manifest["numbers"][prefix+"automatic_fire"])==0,name)
            checked[path.name]={"config_hash":manifest["config_hash"],"cards":cards,"population":population}
        for name in checked:
            os.replace(Path(stage)/name,folder/name)
    return checked


def main():
    result = {}
    try:
        base.require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),"Active play session")
        unreal.GuLiStrikeRogueCardsGroundWeaponEffectsRow().get_editor_property("lock_target_counts")
        result["arc"] = runpy.run_path(str(SCRIPTS/"author_electromagnetic_arc.py"))["main"]()
        base.SCOPE = SCOPE
        result["tables"] = base.install_tables()
        result["entities"] = prepare_scene()
        result["map"] = base.MAP
        result["manifests"] = export_manifests(result["entities"])
        result["success"] = True
    except Exception:
        result["error"] = traceback.format_exc()
        raise
    finally:
        REPORT.parent.mkdir(parents=True,exist_ok=True)
        REPORT.write_text(json.dumps(result,ensure_ascii=False,indent=2,default=str),encoding="utf-8")


if __name__ == "__main__":
    main()
