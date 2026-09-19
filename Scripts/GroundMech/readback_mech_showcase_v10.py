"""Read the saved gallery and its eight entries in a fresh source-editor process."""
import unreal,json,traceback
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/UE_AllAssets_v10')
MAP='/Game/GuLiStrike/Mechs/StyleShowcase/LVL_MechAsset_Showcase'
result={'success':False,'map':MAP}
try:
    assert '-MechAllAssetsReadbackWorker' in unreal.SystemLibrary.get_command_line()
    editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assert editor.get_editor_world().get_path_name().split('.')[0]==MAP
    assert unreal.EditorAssetLibrary.get_metadata_tag(editor.get_editor_world(),'GuLi.StyleOwner')=='GuLi.MechAllAssets.v10'
    expected=['Mecha_01','Mecha_02','Mech_Lightest','SpiderMech','FireWeapon_01','MissileWeapon_01','Machinegun_lvl1','Missile_01']
    rows={}
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    for name in expected:
        matches=[a for a in actors if a.get_actor_label()=='MSS_'+name];assert len(matches)==1
        a=matches[0]
        assert not a.is_temporarily_hidden_in_editor()
        assert list(a.get_actor_scale3d().to_tuple())==[1.,1.,1.]
        rows[name]={'class':a.get_class().get_path_name(),'location_cm':list(a.get_actor_location().to_tuple()),'visible':True,'actor_scale':[1,1,1]}
        if name in ('Mecha_01','Mecha_02'):
            comp=a.get_component_by_class(unreal.SkeletalMeshComponent)
            anim=comp.get_editor_property('animation_data').anim_to_play
            assert '_Idle' in anim.get_name()
            rows[name]['saved_idle']=anim.get_path_name()
    assert not any('_SourcePose' in a.get_actor_label() for a in actors)
    result.update(success=True,assets=rows,count=len(rows),source_pose_helpers_removed=True)
except Exception:result['error']=traceback.format_exc()
(OUT/'ue_gallery_saved_readback.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
unreal.SystemLibrary.quit_editor()
