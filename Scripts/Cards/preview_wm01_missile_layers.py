"""Small editor-only composition captures. No PIE, map switch, save or oversized targets."""
import json
from pathlib import Path
import unreal

assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
output=Path('D:/UE5.7/test1/ArtSource/UI/WM01MissileCards/Production_v4/Previews')
output.mkdir(parents=True,exist_ok=True)
phase=globals().get('WM_MISSILE_PREVIEW_PHASE','prepare')
if phase=='prepare':
    assert not globals().get('WM_MISSILE_PREVIEW'), 'Export/cleanup the previous captures first.'
    WM_MISSILE_PREVIEW=[]
    cls=unreal.load_asset('/Game/GuLiStrike/CardSystem/RevealDemo/Blueprints/BP_ParallaxRevealCard').generated_class()
    frame=unreal.load_asset('/Game/GuLiStrike/CardSystem/WarMachineTarot/Materials/MI_WarMachineFrame')
    poses=globals().get('WM_MISSILE_PREVIEW_POSES',[(globals().get('WM_MISSILE_PREVIEW_TAG','center'),globals().get('WM_MISSILE_PREVIEW_TILT',(0.,0.)))])
    entries=[(name,tag,*tilt) for tag,tilt in poses for name in ('MissilePod','RainSalvo')]
    for i,(name,tag,hx,hy) in enumerate(entries):
        loc=unreal.Vector(300000+i*1000,300000,30000)
        card=actors.spawn_actor_from_class(cls,loc,transient=True)
        card.set_actor_label('Temp_WM01LayerPreview_'+name)
        mi=unreal.load_asset(f'/Game/GuLiStrike/Cards/Commander/WM01/{name}/Materials/MI_{name}_ModelComic_v1')
        card.call_method('SetArtwork',(mi,frame,True))
        card.call_method('SetPresentationSize',(2.,2.,True))
        card.get_editor_property('ConfirmationFlash').set_visibility(False,True)
        card.get_editor_property('HoverPivot').set_editor_property('relative_rotation',unreal.Rotator(roll=hy*16.,yaw=-hx*16.))
        capture_actor=actors.spawn_actor_from_class(unreal.SceneCapture2D,loc+unreal.Vector(0,150,0),unreal.Rotator(pitch=0,yaw=-90,roll=0),transient=True)
        capture_actor.set_actor_label('Temp_WM01LayerCapture_'+name)
        capture=capture_actor.get_component_by_class(unreal.SceneCaptureComponent2D)
        target=unreal.RenderingLibrary.create_render_target2d(world,512,768,unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB,unreal.LinearColor(.025,.025,.025,1),False)
        capture.set_editor_property('texture_target',target)
        capture.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        capture.set_editor_property('capture_every_frame',False)
        capture.set_editor_property('primitive_render_mode',unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
        capture.set_editor_property('fov_angle',22.)
        capture.show_only_actor_components(card,False)
        capture.capture_scene()
        WM_MISSILE_PREVIEW.append((name+'-'+tag,card,capture_actor,capture,target))
    print('Prepared '+str(len(entries))+' 512x768 editor composition captures.')
elif phase=='export':
    try:
        for name,card,capture_actor,capture,target in WM_MISSILE_PREVIEW:
            capture.capture_scene()
            unreal.RenderingLibrary.export_render_target(world,target,str(output),name+'.png')
        report={'exported':[str(output/(r[0]+'.png')) for r in WM_MISSILE_PREVIEW],'pie':False,'map_saved':False,'temporary_actors_cleaned':True}
        (output/'capture-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps(report))
    finally:
        for name,card,capture_actor,capture,target in WM_MISSILE_PREVIEW:
            actors.destroy_actor(capture_actor)
            actors.destroy_actor(card)
        WM_MISSILE_PREVIEW=[]
else:
    raise ValueError('Unknown preview phase')
