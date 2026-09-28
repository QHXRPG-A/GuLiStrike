"""Export actual rendered cards, including editable UMG, from a settled preview.

Requires the War Machine review PIE to be open with all cards at their front pose.
SceneCapture renders only each selected card; no artwork pixels are edited here.
"""
import json
from pathlib import Path
import unreal

OUT=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/Cel_Closeups_v3/Previews')
player=next(p for p in unreal.ObjectIterator(unreal.PlayerController)
            if not p.get_name().startswith('Default__') and p.get_viewport_size()[0]>0)
world=player.get_world()
director=player.get_editor_property('Director')
assert director.get_editor_property('Phase')==1, 'Capture the settled, front-facing selection stage'
gameplay=unreal.get_default_object(unreal.GameplayStatics)
camera=director.get_component_by_class(unreal.CameraComponent)
paths=[]
for index,name in enumerate(['FireRate','MissileDamage','HighSpeed']):
    card=director.get_editor_property('Card'+str(index))
    transform=unreal.Transform(location=unreal.Vector(card.get_actor_location().x,140,0),
                               rotation=unreal.Rotator(yaw=-90))
    actor=gameplay.call_method('BeginDeferredActorSpawnFromClass',
        (world,unreal.SceneCapture2D.static_class(),transform,
         unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN,None,
         unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
    try:
        gameplay.call_method('FinishSpawningActor',
            (actor,transform,unreal.SpawnActorScaleMethod.MULTIPLY_WITH_ROOT))
        capture=actor.get_component_by_class(unreal.SceneCaptureComponent2D)
        capture.set_editor_property('projection_type',unreal.CameraProjectionMode.ORTHOGRAPHIC)
        capture.set_editor_property('ortho_width',32.*director.get_editor_property('CardScale'))
        capture.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        capture.set_editor_property('capture_every_frame',False)
        capture.set_editor_property('primitive_render_mode',unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST)
        capture.show_only_actor_components(card,False)
        capture.set_editor_property('post_process_settings',camera.post_process_settings)
        target=unreal.RenderingLibrary.create_render_target2d(world,1024,1536,
            unreal.TextureRenderTargetFormat.RTF_RGBA8_SRGB,unreal.LinearColor(0,0,0,0),False)
        target.set_editor_property('target_gamma',2.2)
        capture.set_editor_property('texture_target',target)
        capture.capture_scene()
        filename=name+'-actual-card.png'
        unreal.RenderingLibrary.export_render_target(world,target,str(OUT),filename)
        paths.append(str(OUT/filename))
    finally:
        actor.destroy_actor()
report={'source':'Live UE SceneCapture; actual parallax material and UMG text',
        'projection':'orthographic','render_target':'RTF_RGBA8_SRGB','gamma':2.2,'images':paths}
(OUT/'full-card-exports.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
