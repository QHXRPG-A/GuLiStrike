"""One-time fresh-editor readback of this delivery. Never saves UE packages."""
import unreal,json,traceback,os
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/StyleAdjust_20260917')
assert '-StyleAdjustReadbackWorker' in unreal.SystemLibrary.get_command_line()
r={'success':False,'pid':os.getpid(),'saved_packages':[],'checks':{}}
try:
    w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    r['world']=w.get_path_name()
    assert w.get_name()=='LVL_CommanderMassPrototype'
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    pp=next(a for a in actors if a.get_name()=='PostProcessVolume_2').settings
    r['checks']['neutral_gamma']=list(pp.color_gamma.to_tuple())==[1.,1.,1.,1.]
    r['checks']['neutral_contrast']=list(pp.color_contrast.to_tuple())==[1.,1.,1.,1.]
    r['checks']['neutral_scene_tint']=list(pp.scene_color_tint.to_tuple())==[1.,1.,1.,1.]
    r['checks']['fixed_exposure_preserved']=pp.auto_exposure_min_brightness==2. and pp.auto_exposure_max_brightness==2. and pp.auto_exposure_bias==1.25
    r['checks']['no_review_actors_saved']=not any(a.get_actor_label().startswith('StyleReview_') for a in actors)
    sk=next(a.get_component_by_class(unreal.SkyLightComponent) for a in actors if a.get_name()=='SkyLight_1')
    r['sky_lower_color']=list(sk.lower_hemisphere_color.to_tuple())
    r['checks']['sky_fill_saved']=all(abs(a-b)<1e-6 for a,b in zip(r['sky_lower_color'],[.12,.15,.20,1]))
    sub=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    r['models']={}
    for unit in ('Sweeper','WarMachine'):
        b='/Game/Commander/Units/Tactical/Cel/'+unit
        m=unreal.load_asset(b+'/Meshes/SM_'+unit+'_Cel');mat=unreal.load_asset(b+'/Materials/M_'+unit+'_Cel')
        r['models'][unit]={'triangles':[m.get_num_triangles(i) for i in range(sub.get_lod_count(m))],
            'sections':[m.get_num_sections(i) for i in range(sub.get_lod_count(m))],
            'materials':[x.material_interface.get_path_name() for x in m.static_materials],
            'textures':[t.get_path_name() for t in unreal.MaterialEditingLibrary.get_used_textures(mat)]}
    r['checks']['sweeper_one_body']=r['models']['Sweeper']['triangles'][0]==5456 and r['models']['Sweeper']['sections'][0]==1 and len(r['models']['Sweeper']['materials'])==1
    r['checks']['sweeper_no_line_mask']=not any('LineMask' in t for t in r['models']['Sweeper']['textures'])
    r['checks']['war_machine_ink_preserved']=r['models']['WarMachine']['triangles'][0]==27303 and any('Contour' in m for m in r['models']['WarMachine']['materials']) and any('LineMask' in t for t in r['models']['WarMachine']['textures'])
    fx='/Game/GuLiStrike/FX/CombatExplosions/'
    p=unreal.NiagaraService.get_parameter(fx+'NS_GroundDestruction_03','User.PresentationScale')
    r['ground_scale']=str(p.current_value);r['checks']['ground_scale_060']=abs(float(p.current_value)-.6)<1e-6
    r['checks']['air_no_added_scaling']=not unreal.NiagaraService.parameter_exists(fx+'NS_WingmanDestruction_05','User.PresentationScale')
    r['ground_render_bindings']=[]
    for o in unreal.ObjectIterator():
        if unreal.Object.get_path_name(o).startswith(fx+'NS_GroundDestruction_03.') and 'RendererProperties' in unreal.Object.get_class(o).get_name():
            prop='ScaleBinding' if 'Mesh' in o.get_class().get_name() else 'SpriteSizeBinding'
            r['ground_render_bindings'].append([o.get_outer().get_name(),o.get_editor_property('PositionBinding').export_text(),o.get_editor_property(prop).export_text()])
    r['checks']['ground_all_renderers_scaled']=len(r['ground_render_bindings'])==10 and all('PresentationPosition' in b[1] and 'Presentation' in b[2] for b in r['ground_render_bindings'])
    r['success']=all(r['checks'].values())
except Exception:r['error']=traceback.format_exc()
finally:
    (OUT/'saved_readback.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8')
    unreal.SystemLibrary.quit_editor()
