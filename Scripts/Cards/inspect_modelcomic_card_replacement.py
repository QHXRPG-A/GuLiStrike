import unreal
import json
import traceback
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/ModelComic_v9/Production/Inspection')
OUT.mkdir(parents=True,exist_ok=True)
r={'success':False}
try:
    w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    r['world']=w.get_path_name()
    r['pie']=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    r['actors']=[{'label':a.get_actor_label(),'class':a.get_class().get_path_name()} for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()]
    r['dirty_maps']=[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    r['map_exists']=unreal.EditorAssetLibrary.does_asset_exist('/Game/GuLiStrike/Cards/WarMachineTarot/Maps/LVL_WarMachineTarotReview')
    bp=unreal.load_asset('/Game/GuLiStrike/Cards/RevealDemo/Blueprints/BP_ParallaxRevealCard')
    r['card_blueprint']=unreal.BlueprintService.get_blueprint_info(bp.get_path_name()).to_json() if False else bp.get_path_name()
    r['success']=True
except Exception:r['error']=traceback.format_exc()
(OUT/'editor-before.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(r,ensure_ascii=False))
