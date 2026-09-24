import json, traceback, unreal
from pathlib import Path
root='/Game/GuLiStrike/Cards/RevealDemo'
report={}
try:
    unreal.EditorLoadingAndSavingUtils.load_map(root+'/Maps/LVL_CardRevealDemo')
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    b=unreal.load_asset(root+'/Blueprints/BP_ParallaxRevealCard')
    card=actors.spawn_actor_from_class(b.generated_class(),unreal.Vector())
    report['actors']=[]
    for a in actors.get_all_level_actors():
        if 'CardReveal' in a.get_class().get_name() or a==card:
            report['actors'].append({'class':a.get_class().get_name(),'components':[{
                'name':c.get_name(),'parent':str(c.get_attach_parent()),'relative':str(c.get_relative_transform()),
                'world':str(c.get_world_transform()),
                'mesh':str(c.get_editor_property('static_mesh')) if isinstance(c,unreal.StaticMeshComponent) else None,
                'materials':[str(m) for m in c.get_materials()] if isinstance(c,unreal.MeshComponent) else []}
                for c in a.get_components_by_class(unreal.SceneComponent)]})
    report['template_camera']=str(unreal.BlueprintService.get_all_component_properties(root+'/Blueprints/BP_CardRevealDirector','PresentationCamera'))
    report['card_bounds']=str(card.get_actor_bounds(False,True))
except Exception:report['error']=traceback.format_exc()
Path('D:/UE5.7/test1/ArtSource/UI/CardRevealDemo/scene-inspection.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({'success':'error' not in report}))
