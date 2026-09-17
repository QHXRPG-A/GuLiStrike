"""Restore readable midtones and shadow fill in the current commander level."""
import unreal,json,traceback
from pathlib import Path
ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/StyleAdjust_20260917'
REPORT={'success':False,'changes':[]}
def run():
    assert not unreal.WidgetService.is_pie_running()
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name()=='/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    volumes=[a for a in actors if isinstance(a,unreal.PostProcessVolume) and a.get_name()=='PostProcessVolume_2']
    assert len(volumes)==1
    pp=volumes[0];settings=pp.get_editor_property('settings')
    backup=OUT/'level_lighting_rollback.json'
    skylight=next(a.get_component_by_class(unreal.SkyLightComponent) for a in actors if a.get_name()=='SkyLight_1')
    if not backup.exists():
        backup.write_text(json.dumps({'map':world.get_path_name(),'post_process_actor':pp.get_name(),'post_process_settings':settings.export_text(),'sky_lower_color':skylight.get_editor_property('lower_hemisphere_color').export_text()},ensure_ascii=False,indent=2),encoding='utf8')
    values={'color_gamma':unreal.Vector4(1,1,1,1),'color_contrast':unreal.Vector4(1,1,1,1),'color_gamma_midtones':unreal.Vector4(1,1,1,1),'scene_color_tint':unreal.LinearColor(1,1,1,1),'white_temp':6500.,'white_tint':0.,'film_slope':0.78,'film_toe':0.45,'vignette_intensity':0.12,'ambient_occlusion_intensity':0.35}
    pp.modify()
    for k,v in values.items():
        REPORT['changes'].append({'property':k,'before':str(settings.get_editor_property(k)),'after':str(v)})
        settings.set_editor_property('override_'+k,True);settings.set_editor_property(k,v)
    pp.set_editor_property('settings',settings)
    skylight.modify();skylight.set_editor_property('lower_hemisphere_color',unreal.LinearColor(.12,.15,.20,1))
    REPORT['sky_lower_color']=str(skylight.get_editor_property('lower_hemisphere_color'))
    REPORT['post_process_readback']={k:str(pp.get_editor_property('settings').get_editor_property(k)) for k in values}
    REPORT.update(success=True,map=world.get_path_name(),saved=False,sun_intensity_unchanged=True,exposure_limits_unchanged=True)
try:run()
except Exception:REPORT['error']=traceback.format_exc()
(OUT/'level_lighting_adjustment.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(REPORT))
