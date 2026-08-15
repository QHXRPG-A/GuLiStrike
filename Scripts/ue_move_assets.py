"""Move Variant_TwinStick assets to /Game/GuLiStrike, keep folder structure.

Maps old subfolders -> new subfolders. MI_Colorway from /Game/TopDown -> /Game/GuLiStrike/Materials.
Leaves redirectors at old paths (fixup comes next).
Writes report to D:/UE_5.7/test1/Saved/move_report.json
"""
import unreal, json, traceback

out = {"moved": [], "failed": [], "skipped": []}
try:
    at = unreal.AssetToolsHelpers.get_asset_tools()
    ar = unreal.AssetRegistryHelpers.get_asset_registry()

    # (asset_path, new_package_path, new_name)  name unchanged except where noted
    MOVES = [
        # Blueprints root
        ("/Game/Variant_TwinStick/Blueprints/BP_TwinStickCharacter", "/Game/GuLiStrike/Blueprints", "BP_TwinStickCharacter"),
        ("/Game/Variant_TwinStick/Blueprints/BP_TwinStickPlayerController", "/Game/GuLiStrike/Blueprints", "BP_TwinStickPlayerController"),
        ("/Game/Variant_TwinStick/Blueprints/BP_TwinStickGameMode", "/Game/GuLiStrike/Blueprints", "BP_TwinStickGameMode"),
        ("/Game/Variant_TwinStick/Blueprints/BP_TwinStickProjectile", "/Game/GuLiStrike/Blueprints", "BP_TwinStickProjectile"),
        ("/Game/Variant_TwinStick/Blueprints/BP_TwinStickAoEAttack", "/Game/GuLiStrike/Blueprints", "BP_TwinStickAoEAttack"),
        ("/Game/Variant_TwinStick/Blueprints/BP_TwinStickPickup", "/Game/GuLiStrike/Blueprints", "BP_TwinStickPickup"),
        ("/Game/Variant_TwinStick/Blueprints/BP_AssetGuideline_StateTree", "/Game/GuLiStrike/Blueprints", "BP_AssetGuideline_StateTree"),
        # Blueprints/AI
        ("/Game/Variant_TwinStick/Blueprints/AI/BP_TwinStickAIController", "/Game/GuLiStrike/Blueprints/AI", "BP_TwinStickAIController"),
        ("/Game/Variant_TwinStick/Blueprints/AI/BP_TwinStickNPC", "/Game/GuLiStrike/Blueprints/AI", "BP_TwinStickNPC"),
        ("/Game/Variant_TwinStick/Blueprints/AI/BP_TwinStickNPCDestruction", "/Game/GuLiStrike/Blueprints/AI", "BP_TwinStickNPCDestruction"),
        ("/Game/Variant_TwinStick/Blueprints/AI/BP_TwinStickSpawner", "/Game/GuLiStrike/Blueprints/AI", "BP_TwinStickSpawner"),
        ("/Game/Variant_TwinStick/Blueprints/AI/ST_TwinStickNPC", "/Game/GuLiStrike/Blueprints/AI", "ST_TwinStickNPC"),
        # Input
        ("/Game/Variant_TwinStick/Input/Actions/IA_Action_Dash", "/Game/GuLiStrike/Input/Actions", "IA_Action_Dash"),
        ("/Game/Variant_TwinStick/Input/Actions/Interact", "/Game/GuLiStrike/Input/Actions", "Interact"),
        ("/Game/Variant_TwinStick/Input/Actions/MouseAim", "/Game/GuLiStrike/Input/Actions", "MouseAim"),
        ("/Game/Variant_TwinStick/Input/Actions/Move", "/Game/GuLiStrike/Input/Actions", "Move"),
        ("/Game/Variant_TwinStick/Input/Actions/StickAim", "/Game/GuLiStrike/Input/Actions", "StickAim"),
        ("/Game/Variant_TwinStick/Input/Actions/UseItem", "/Game/GuLiStrike/Input/Actions", "UseItem"),
        ("/Game/Variant_TwinStick/Input/IMC_TwinStick", "/Game/GuLiStrike/Input", "IMC_TwinStick"),
        ("/Game/Variant_TwinStick/Input/IMC_TwinStick_MouseShoot", "/Game/GuLiStrike/Input", "IMC_TwinStick_MouseShoot"),
        # Input/Touch
        ("/Game/Variant_TwinStick/Input/Touch/BPI_TouchInterface_TwinStick", "/Game/GuLiStrike/Input/Touch", "BPI_TouchInterface_TwinStick"),
        ("/Game/Variant_TwinStick/Input/Touch/UI_Thumbstick", "/Game/GuLiStrike/Input/Touch", "UI_Thumbstick"),
        ("/Game/Variant_TwinStick/Input/Touch/UI_TouchInterface_TwinStick", "/Game/GuLiStrike/Input/Touch", "UI_TouchInterface_TwinStick"),
        # FX
        ("/Game/Variant_TwinStick/FX/M_BlinkAfterimage", "/Game/GuLiStrike/FX", "M_BlinkAfterimage"),
        ("/Game/Variant_TwinStick/FX/M_BlinkHeatwave", "/Game/GuLiStrike/FX", "M_BlinkHeatwave"),
        ("/Game/Variant_TwinStick/FX/NS_TopDownAction_Destruction", "/Game/GuLiStrike/FX", "NS_TopDownAction_Destruction"),
        # Meshes
        ("/Game/Variant_TwinStick/Meshes/SM_TargetBaseMesh", "/Game/GuLiStrike/Meshes", "SM_TargetBaseMesh"),
        ("/Game/Variant_TwinStick/Meshes/GC/GC_SM_TargetBaseMesh_Destruction", "/Game/GuLiStrike/Meshes/GC", "GC_SM_TargetBaseMesh_Destruction"),
        # Spell
        ("/Game/Variant_TwinStick/Spell/NewGameplayAbilityBlueprint", "/Game/GuLiStrike/Spell", "NewGameplayAbilityBlueprint"),
        # UI
        ("/Game/Variant_TwinStick/UI/UI_TwinStick", "/Game/GuLiStrike/UI", "UI_TwinStick"),
        # root material
        ("/Game/Variant_TwinStick/M_Glow", "/Game/GuLiStrike/Materials", "M_Glow"),
        # survivor from TopDown
        ("/Game/TopDown/MI_Colorway", "/Game/GuLiStrike/Materials", "MI_Colorway"),
    ]

    for old_pkg, new_dir, new_name in MOVES:
        try:
            if not unreal.EditorAssetLibrary.does_asset_exist(old_pkg):
                out["skipped"].append(old_pkg)
                continue
            loaded = unreal.EditorAssetLibrary.load_asset(old_pkg)
            ard = unreal.AssetRenameData(
                asset=loaded,
                new_package_path=new_dir,
                new_name=new_name,
            )
            ok = at.rename_assets([ard])
            if ok:
                out["moved"].append(old_pkg + " -> " + new_dir + "/" + new_name)
            else:
                out["failed"].append({"asset": old_pkg, "why": "rename_assets returned False"})
        except Exception as e:
            out["failed"].append({"asset": old_pkg, "why": str(e)})

    out["moved_count"] = len(out["moved"])
    out["failed_count"] = len(out["failed"])
    out["skipped_count"] = len(out["skipped"])
except Exception:
    out["fatal"] = traceback.format_exc()

with open(r"D:\UE_5.7\test1\Saved\move_report.json", "w") as f:
    f.write(json.dumps(out, indent=1))
print("REPORT_WRITTEN")
