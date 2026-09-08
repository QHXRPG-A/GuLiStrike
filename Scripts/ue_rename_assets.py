"""Rename TwinStick assets -> GuLiStrike, one at a time with save+verify per step.

GameMode renamed LAST (it is the active global default).
Each step: rename_assets -> ue_save_all_dirty -> verify new file exists on disk.
Report: D:/UE5.7/test1/Saved/rename_assets_report.json
"""
import unreal, json, os, traceback

out = {"steps": []}
RENAMES = [
    # (old_package, new_dir, new_name)
    ("/Game/GuLiStrike/Blueprints/BP_TwinStickProjectile", "/Game/GuLiStrike/Blueprints", "BP_GuLiStrikeProjectile"),
    ("/Game/GuLiStrike/Blueprints/BP_TwinStickAoEAttack", "/Game/GuLiStrike/Blueprints", "BP_GuLiStrikeAoEAttack"),
    ("/Game/GuLiStrike/Blueprints/BP_TwinStickPickup", "/Game/GuLiStrike/Blueprints", "BP_GuLiStrikePickup"),
    ("/Game/GuLiStrike/Blueprints/BP_TwinStickCharacter", "/Game/GuLiStrike/Blueprints", "BP_GuLiStrikeCharacter"),
    ("/Game/GuLiStrike/Blueprints/BP_TwinStickPlayerController", "/Game/GuLiStrike/Blueprints", "BP_GuLiStrikePlayerController"),
    ("/Game/GuLiStrike/Blueprints/BP_AssetGuideline_StateTree", "/Game/GuLiStrike/Blueprints", "BP_AssetGuideline_StateTree"),
    ("/Game/GuLiStrike/Blueprints/AI/BP_TwinStickAIController", "/Game/GuLiStrike/Blueprints/AI", "BP_GuLiStrikeAIController"),
    ("/Game/GuLiStrike/Blueprints/AI/BP_TwinStickNPC", "/Game/GuLiStrike/Blueprints/AI", "BP_GuLiStrikeNPC"),
    ("/Game/GuLiStrike/Blueprints/AI/BP_TwinStickNPCDestruction", "/Game/GuLiStrike/Blueprints/AI", "BP_GuLiStrikeNPCDestruction"),
    ("/Game/GuLiStrike/Blueprints/AI/BP_TwinStickSpawner", "/Game/GuLiStrike/Blueprints/AI", "BP_GuLiStrikeSpawner"),
    ("/Game/GuLiStrike/Blueprints/AI/ST_TwinStickNPC", "/Game/GuLiStrike/Blueprints/AI", "ST_GuLiStrikeNPC"),
    ("/Game/GuLiStrike/Input/IMC_TwinStick", "/Game/GuLiStrike/Input", "IMC_GuLiStrike"),
    ("/Game/GuLiStrike/Input/IMC_TwinStick_MouseShoot", "/Game/GuLiStrike/Input", "IMC_GuLiStrike_MouseShoot"),
    ("/Game/GuLiStrike/Input/Touch/BPI_TouchInterface_TwinStick", "/Game/GuLiStrike/Input/Touch", "BPI_TouchInterface_GuLiStrike"),
    ("/Game/GuLiStrike/Input/Touch/UI_TouchInterface_TwinStick", "/Game/GuLiStrike/Input/Touch", "UI_TouchInterface_GuLiStrike"),
    ("/Game/GuLiStrike/UI/UI_TwinStick", "/Game/GuLiStrike/UI", "UI_GuLiStrike"),
    # active GameMode last
    ("/Game/GuLiStrike/Blueprints/BP_TwinStickGameMode", "/Game/GuLiStrike/Blueprints", "BP_GuLiStrikeGameMode"),
]

CONTENT = "D:/UE5.7/test1/Content"
try:
    at = unreal.AssetToolsHelpers.get_asset_tools()
    for old_pkg, new_dir, new_name in RENAMES:
        step = {"old": old_pkg, "new": new_dir + "/" + new_name}
        try:
            if not unreal.EditorAssetLibrary.does_asset_exist(old_pkg):
                if unreal.EditorAssetLibrary.does_asset_exist(new_dir + "/" + new_name):
                    step["result"] = "already-done"
                else:
                    step["result"] = "missing"
                out["steps"].append(step)
                continue
            loaded = unreal.EditorAssetLibrary.load_asset(old_pkg)
            ard = unreal.AssetRenameData(asset=loaded, new_package_path=new_dir, new_name=new_name)
            ok = at.rename_assets([ard])
            step["rename"] = bool(ok)
            unreal.EditorAssetLibrary.save_directory(new_dir, only_if_is_dirty=False, recursive=True)
            new_rel = new_dir.replace("/Game/", "") + "/" + new_name + ".uasset"
            step["on_disk"] = os.path.exists(os.path.join(CONTENT, new_rel))
            step["result"] = "ok" if (ok and step["on_disk"]) else "check"
        except Exception as e:
            step["result"] = "error"
            step["error"] = str(e)[:200]
        out["steps"].append(step)
except Exception:
    out["fatal"] = traceback.format_exc()

with open(r"D:\UE5.7\test1\Saved\rename_assets_report.json", "w") as f:
    f.write(json.dumps(out, indent=1))
print("REPORT_WRITTEN")
