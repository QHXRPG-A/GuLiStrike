"""Install AI-engine DataTable rows and prepare the existing Ground range in an editor world."""
import json
import os
from pathlib import Path
import tempfile
import traceback
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
MAP="/Game/Maps/LVL_CommanderMassPrototype"
DATA="/Game/GuLiStrike/Data/"
REPORT=ROOT/"Artifacts/AIEngineHUD_20261122/install.json"
TABLES=unreal.DataTableFunctionLibrary
LIB=unreal.EditorAssetLibrary
SCOPE={
    "GuLiStrikeRogueCards_Cards":{"GroundMech_EnhancedBullet_Lv1":["MaxAcquisitions","Note"],"GroundMech_AIEngine_Lv1":None},
    "GuLiStrikeRogueCards_Effects":{"GroundAIEngine":None},
    "GuLiStrikeRogueCards_GroundWeaponEffects":{"GroundEnhancedBullet":["BulletDamageMultipliers","BulletRateMultipliers","BulletFlightVfxIds"],"GroundAIEngine":None},
    "GuLiStrikeGameTexts_Texts":{f"Card.GroundMech.{key}":None for key in ("EnhancedBullet.Description","AIEngine.Title","AIEngine.Description")},
    "GuLiStrikeGroundHUD_Elements":{"AIEngineOptics":None},
    "GuLiStrikeVfx_Effects":{"GroundEnhanced"+name:None for name in ("Gold","Violet","Silver")},
}
NOTE="GroundAIEngine_Instructions"
INSTRUCTIONS=("红队游骑兵。双靶演练可在 PIE 前用 gs.Review.GroundIsolateCombat 1 暂停周围士兵自动开火，结束后改回 0。"
              "F4 将强化子弹升到三级，再选人工智能引擎。大圆从十字准星展开。"
              "在 GroundRogue_Entry 将蓝靶 Enemy_03 和 Enemy_04 同时纳入大圆，锁定途中把准星移向另一靶，检查原靶进度连续。"
              "大圆半径为 342 设计像素。锁满时内圈消失，大圆跟随准星，每发只震目标六边形。"
              "连射时转动视角，让六边形中心先越过圆边。只要还有一条边或一个角碰着大圆，就继续射击原靶。"
              "六边形完全离圆、原靶遮挡、超距或死亡后重新选敌。")


def require(value,message):
    if not value:
        raise RuntimeError(message)
    return value


def object_path(value):
    return value.split("'",1)[1][:-1] if isinstance(value,str) and "'" in value and value.endswith("'") else value


def rows(table):
    return {r["Name"]:r for r in json.loads(TABLES.export_data_table_to_json_string(table))}


def same_value(actual,expected):
    if object_path(actual)==object_path(expected) or (expected=="" and actual=="None"):return True
    if isinstance(expected,str) and expected.startswith("(") and isinstance(actual,str):
        import re
        a=dict(re.findall(r"(\w+)=([^,)]+)",actual)); b=dict(re.findall(r"(\w+)=([^,)]+)",expected))
        if a.keys()==b.keys() and all(abs(float(a[k])-float(b[k]))<.0001 for k in a):return True
    if isinstance(expected,dict) and isinstance(actual,str):
        import re
        a=dict(re.findall(r"(\w+)=([^,)]+)",actual))
        if set(expected)==set(a) and all(abs(float(a[k])-v)<.0001 for k,v in expected.items()):return True
    return False


def install_tables(scope=None):
    if scope is None:
        unreal.GuLiStrikeRogueCardsGroundWeaponEffectsRow().get_editor_property("lock_seconds")
        unreal.GuLiStrikeVfxEffectsRow().get_editor_property("trail_tint_srgb")
        scope=SCOPE
    dirty={p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    packages={DATA+"DT_"+key for key in scope}|{DATA+"ST_GuLiStrikeGameTexts"}
    require(not dirty.intersection(packages),"Unsaved data packages: "+str(dirty.intersection(packages)))
    saved={}
    for key,edits in scope.items():
        name="DT_"+key
        table=require(unreal.load_asset(DATA+name),name)
        before=rows(table)
        proposed=before.copy()
        source={r["Name"]:r for r in json.loads((ROOT/"data/Json"/(name+".json")).read_text(encoding="utf-8"))}
        for row,columns in edits.items():
            proposed[row]=source[row] if columns is None else {**before[row],**{k:source[row][k] for k in columns}}
        if all(row in before and all(same_value(before[row].get(col),proposed[row][col])
                                     for col in columns or source[row].keys()) for row,columns in edits.items()):
            saved[name]=list(edits)
            continue
        problems=unreal.GuLiDataTableImportLibrary.update_from_json(table,json.dumps(list(proposed.values()),ensure_ascii=False))
        require(not problems,name+" import: "+str(list(problems)))
        after=rows(table)
        require(set(after)==set(proposed) and all(after[r]==before[r] for r in before if r not in edits),name+" unrelated rows")
        for r,cols in edits.items():
            for col in cols or source[r].keys():
                actual,expected=after[r][col],proposed[r][col]
                if same_value(actual,expected):continue
                raise RuntimeError(f"{name}/{r}/{col}: {actual!r} vs {expected!r}")
        if after!=before:
            require(LIB.save_loaded_asset(table,False),name+" save")
        saved[name]=list(edits)
    game_texts=unreal.load_asset(DATA+"DT_GuLiStrikeGameTexts_Texts")
    strings=require(unreal.load_asset(DATA+"ST_GuLiStrikeGameTexts"),"Public text")
    require(unreal.GuLiRogueCardPresentationLibrary.rebuild_game_text_string_table(strings,game_texts),"Public text rebuild")
    require(LIB.save_loaded_asset(strings,False),"Save public text")
    for key in scope["GuLiStrikeGameTexts_Texts"]:
        value=unreal.TextLibrary.text_from_string_table(unreal.Name(strings.get_path_name()),key)
        require(unreal.TextLibrary.text_is_from_string_table(value) and str(value)==rows(game_texts)[key]["Content"],"Public text "+key)
    return saved


def prepare_scene():
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world=editor.get_editor_world()
    if world.get_path_name().split(".")[0]!=MAP:
        require(not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(),"Unsaved map")
        require(level.load_level(MAP),"Open existing Ground range")
        world=editor.get_editor_world()
    dual_targets={"GroundRogue_Enemy_03","GroundRogue_Enemy_04"}
    labels=dual_targets|{"GroundRogue_Entry","GroundAmmo_ObserveDamage","GroundAmmo_PrimaryDamage","GroundAmmo_Friendly","GroundAmmo_BlockingWall",NOTE}
    def relevant():
        return {a.get_actor_label():a for a in sub.get_all_level_actors() if a.get_actor_label() in labels}
    actors=relevant()
    require(labels-{NOTE}<=set(actors),"Missing existing range entities "+str(labels-set(actors)))
    note=actors.get(NOTE)
    if not note:
        pos=actors["GroundAmmo_ObserveDamage"].get_actor_location()+unreal.Vector(0,0,100)
        note=require(sub.spawn_actor_from_class(unreal.Note,pos),"Editor note")
        note.set_actor_label(NOTE)
    require(isinstance(note,unreal.Note),NOTE)
    if str(note.get_editor_property("text"))!=INSTRUCTIONS:
        note.modify()
        note.set_editor_property("text",INSTRUCTIONS)
        require(unreal.EditorLoadingAndSavingUtils.save_map(world,MAP),"Save existing range")
    del note,actors,world
    require(level.load_level(MAP),"Reload existing range")
    actors=relevant()
    require(set(actors)==labels,"Missing saved range entities "+str(labels-set(actors)))
    require(str(actors[NOTE].get_editor_property("text"))==INSTRUCTIONS,"Saved AI engine instructions")
    enemy,friend=actors["GroundAmmo_PrimaryDamage"],actors["GroundAmmo_Friendly"]
    require(enemy.get_editor_property("team")==unreal.GuLiTeam.BLUE and friend.get_editor_property("team")==unreal.GuLiTeam.RED,"Target teams")
    require(all(actors[n].get_editor_property("team")==unreal.GuLiTeam.BLUE and actors[n].get_editor_property("start_idle") for n in dual_targets),"Two stationary blue targets")
    return {"map":MAP,"entities":{n:{"class":a.get_class().get_name(),"position":a.get_actor_location().to_tuple()} for n,a in actors.items()},
            "locked_pair":{n:{"team":str(actors[n].get_editor_property("team")),"idle":actors[n].get_editor_property("start_idle")} for n in sorted(dual_targets)},
            "enemy":str(enemy.get_editor_property("team")),"friendly":str(friend.get_editor_property("team"))}


def runtime_manifests():
    config=ROOT/"Config/Hybrid"
    validated={}
    with tempfile.TemporaryDirectory(dir=config) as directory:
        for name,count,trial in [("RuntimeManifest-200",200,False),("RuntimeManifest-600",600,False),("RuntimeManifest-NoSurvivors",600,True)]:
            filename=name+".json"
            staged=Path(directory)/filename
            result=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(count,str(staged),trial))
            require(result.get("success"),str(result))
            check=json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(staged)))
            require(check.get("success"),str(check))
            manifest=json.loads(staged.read_text(encoding="utf-8-sig"))
            chunks=int(manifest["numbers"]["ground_rogue/catalog/chunks"])
            catalog=bytes.fromhex("".join(manifest["strings"][f"ground_rogue/catalog/{n}"] for n in range(chunks)))
            require(b"ground/ai_engine" in catalog and b"14.01" in catalog,"Missing runtime card")
            validated[filename]={"objects":len(manifest["objects"]),"rules":len(manifest["rules"]),"config_hash":manifest["config_hash"]}
        for filename in validated:
            os.replace(Path(directory)/filename,config/filename)
    return validated


def main():
    result={}
    try:
        require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),"Active play session")
        result["tables"]=install_tables()
        result["scene"]=prepare_scene()
        result["manifests"]=runtime_manifests()
        result["success"]=True
    except Exception:
        result.update(success=False,error=traceback.format_exc())
        raise
    finally:
        REPORT.parent.mkdir(parents=True,exist_ok=True)
        REPORT.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding="utf8")


if __name__=="__main__":
    main()
