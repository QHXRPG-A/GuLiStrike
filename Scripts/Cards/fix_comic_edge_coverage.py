"""Repair only comic environment coverage and the frame underlap; preserve depth."""
import json
import sys
import traceback
from pathlib import Path
import unreal
sys.path.insert(0,'D:/UE5.7/test1/Scripts/Cards')
from comic_card_edge_coverage import configure_artwork_clip,SPEED_COVERAGE_LAYOUTS,ARTWORK_CLIP_CODE

ROOT='/Game/GuLiStrike/Cards/WarMachineTarot'
MAP=ROOT+'/Maps/LVL_WarMachineTarotReview'
OUT=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/Comic_Closeups_v6/Production/EdgeCoverageFix')
r={'success':False,'before':{},'after':{}}
try:
    assert not unreal.WidgetService.is_pie_running()
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().startswith(MAP+'.')
    d=next(a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if a.get_actor_label()=='WarMachine_CardDirector')
    front=unreal.load_asset(ROOT+'/Materials/MI_HighSpeed_Comic_v6')
    parent=unreal.load_asset(ROOT+'/Materials/M_CelCardParallax_Comic_v6')
    mel=unreal.MaterialEditingLibrary
    for i in SPEED_COVERAGE_LAYOUTS:
        r['before'][i]={k:list(mel.get_material_instance_vector_parameter_value(front,f'Layer {i} {k}').to_tuple()) for k in ['Offset','Scale']}
    configure_artwork_clip(parent)
    assert unreal.EditorAssetLibrary.save_loaded_asset(parent,False)
    for i,(offset,scale) in SPEED_COVERAGE_LAYOUTS.items():
        assert unreal.MaterialService.set_instance_vector_parameter(front.get_path_name(),f'Layer {i} Offset',*offset,0.,0.)
        assert unreal.MaterialService.set_instance_vector_parameter(front.get_path_name(),f'Layer {i} Scale',*scale,1.,1.)
        r['after'][i]={'offset':offset,'scale':scale}
    mel.update_material_instance(front)
    assert unreal.EditorAssetLibrary.save_loaded_asset(front,False)
    r['preserved']={'depth':mel.get_material_instance_scalar_parameter_value(front,'Layers global depth'),
        'maximum_tilt':d.get_editor_property('MaximumTilt'),'area':d.get_editor_property('CardAreaMultiplier'),'thickness':d.get_editor_property('CardThicknessMultiplier'),
        'main_body_offset':list(mel.get_material_instance_vector_parameter_value(front,'Layer 3 Offset').to_tuple()),
        'main_body_scale':list(mel.get_material_instance_vector_parameter_value(front,'Layer 3 Scale').to_tuple())}
    assert r['preserved']['depth']==4 and r['preserved']['maximum_tilt']==16
    assert r['preserved']['area']==2 and r['preserved']['thickness']==2
    r['aperture_clip']=ARTWORK_CLIP_CODE
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,MAP)
    r['map_saved']=MAP
    r['success']=True
except Exception:r['error']=traceback.format_exc()
(OUT/'repair.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
print(json.dumps(r))
