"""Final code handoff: save the existing Mass map and read back only owned fixture entities.

No PIE, automation, screenshots, navigation rebuild, or native compilation.
"""
import json
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'Artifacts/SceneUI20261008'
OUT.mkdir(exist_ok=True)
MAP='/Game/Maps/LVL_CommanderMassPrototype'
TAG='SceneUIEnvironmentReview20261008'
FOLDER='GuLiStrike/Review/SceneUIEnvironment'
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
api=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
lib=unreal.EditorAssetLibrary
edit=unreal.MaterialEditingLibrary
report={'success':False,'map':MAP,'native_build_executed':False,'gameplay_started':False,
        'runtime_effect_verified':False,'readback_after_map_reload':False,'actors':[]}

def make(label,cls,location):
    found=[a for a in api.get_all_level_actors() if a.get_actor_label()==label]
    assert len(found)<=1,label
    actor=found[0] if found else api.spawn_actor_from_class(cls,location)
    assert isinstance(actor,cls),label
    if found:assert TAG in [str(t) for t in actor.tags],label
    actor.modify();actor.set_actor_label(label);actor.set_folder_path(FOLDER)
    actor.set_editor_property('tags',[TAG])
    actor.set_actor_location_and_rotation(location,unreal.Rotator(),False,True)
    return actor

def ground(x,y):
    ignored=[a for a in api.get_all_level_actors() if TAG in [str(t) for t in a.tags]]
    hit=unreal.SystemLibrary.line_trace_single(editor.get_editor_world(),unreal.Vector(x,y,30000),unreal.Vector(x,y,-15000),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ignored,unreal.DrawDebugTrace.NONE)
    assert hit and hit.to_tuple()[0],(x,y,'no terrain')
    return hit.to_tuple()[5]

def background_material(name,color):
    folder='/Game/GuLiStrike/Review/LocalTeamColor_20261008/Environment'
    path=folder+'/'+name
    mat=unreal.load_asset(path) if lib.does_asset_exist(path) else None
    if mat:assert lib.get_metadata_tag(mat,'GuLi.Owner')==TAG
    else:mat=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,folder,unreal.Material,unreal.MaterialFactoryNew())
    lib.set_metadata_tag(mat,'GuLi.Owner',TAG)
    # Snapshot deletion avoids the engine's iterator invalidation in delete_all_material_expressions.
    edit.get_num_material_expressions(mat)
    nodes=[n for n in unreal.ObjectIterator(unreal.MaterialExpression) if n.get_path_name().startswith(mat.get_path_name()+':')]
    for n in nodes:edit.delete_material_expression(mat,n)
    mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_OPAQUE)
    value=edit.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector)
    value.set_editor_property('constant',color)
    assert edit.connect_material_property(value,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.recompile_material(mat);assert lib.save_loaded_asset(mat,False)
    return mat

try:
    assert editor.get_game_world() is None,'Leave the existing game session untouched'
    world=editor.get_editor_world()
    assert world.get_path_name().split('.')[0]==MAP
    static=json.loads((OUT/'static-review.json').read_text(encoding='utf8'))
    assert static['success'],'Complete code static review before the final scene step'
    mode=world.get_world_settings().get_editor_property('default_game_mode')
    assert mode.get_path_name()=='/Script/GuLiStrike.GuLiCommanderGameMode'
    controller=unreal.get_default_object(mode).get_editor_property('player_controller_class')
    assert controller.get_path_name()=='/Script/GuLiStrike.GuLiCommanderPlayerController'
    # Keep every previously authored deployment. Summon/construction-only types use their real Q/B entrances.
    for side,team,x in [('Blue',unreal.GuLiTeam.BLUE,-24000),('Red',unreal.GuLiTeam.RED,-14000)]:
        for key,unit,y in [('Pioneer',1,79000),('WM01',2,73000)]:
            actor=make('SceneUI_'+side+'_'+key,unreal.GuLiCommanderDeploymentPoint,ground(x,y))
            for field,value in dict(team=team,unit_type_id=unit,rows=1,columns=1,spacing_centimeters=2400,
                                    allow_automatic_fire=False,start_idle=True).items():actor.set_editor_property(field,value)
    soldiers=json.loads((ROOT/'Data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf-8-sig'))
    ring_mesh=unreal.load_asset('/Game/Commander/Units/SM_CommanderUnitRing')
    ring_material=unreal.load_asset('/Game/Commander/UI/M_CommanderUnitRing')
    assert ring_mesh and ring_material and edit.get_num_material_expressions(ring_material)==12
    radius_specs=[]
    for i,unit in enumerate((1,2,5,6)):
        row=next(r for r in soldiers if r['Id']==unit)
        radius=float(row.get('MassAvoidanceRadiusMeters',0))*100
        if radius<=0:radius=float(row['ModelWidthMeters'])*50
        actor=make('SceneUI_Radius_'+row['Name'],unreal.StaticMeshActor,ground(-28000+i*4500,67500)+unreal.Vector(0,0,7))
        actor.set_editor_property('is_editor_only_actor',True)
        actor.set_actor_scale3d(unreal.Vector(radius/48,radius/48,.004))
        comp=actor.static_mesh_component;comp.modify();comp.set_static_mesh(ring_mesh);comp.set_material(0,ring_material)
        comp.set_collision_profile_name('NoCollision',True);comp.set_cast_shadow(False)
        comp.set_editor_property('can_ever_affect_navigation',False)
        radius_specs.append({'unit_id':unit,'label':actor.get_actor_label(),'outer_radius_cm':radius,'inner_radius_cm':radius-20,'width_cm':20})
    origin=ground(-19000,76000)
    for label,settings in [
        ('SceneUI_Profile_Night',dict(auto_exposure_bias=-8.0,bloom_intensity=0.0)),
        ('SceneUI_Profile_ExposureBloom',dict(auto_exposure_bias=8.0,bloom_intensity=15.0)),
        ('SceneUI_Profile_ColorGrade',dict(auto_exposure_bias=0.0,color_saturation=unreal.Vector4(.2,1.8,.2,1),
            color_contrast=unreal.Vector4(2,2,2,1),color_gamma=unreal.Vector4(.7,1.4,.7,1)))]:
        actor=make(label,unreal.PostProcessVolume,origin)
        for key,value in dict(unbound=True,enabled=False,blend_weight=1.0,priority=150.0).items():actor.set_editor_property(key,value)
        pp=actor.get_editor_property('settings')
        values=dict(auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,auto_exposure_apply_physical_camera_exposure=False,**settings)
        for key,value in values.items():pp.set_editor_property('override_'+key,True);pp.set_editor_property(key,value)
        actor.set_editor_property('settings',pp)
    fog=make('SceneUI_Profile_DenseFog',unreal.ExponentialHeightFog,origin)
    fog_component=fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    for key,value in dict(fog_density=.15,fog_height_falloff=.02,fog_max_opacity=1.0,
                         fog_inscattering_luminance=unreal.LinearColor(.35,.03,.6,1)).items():fog_component.set_editor_property(key,value)
    fog_component.set_visibility(False);fog.set_actor_hidden_in_game(True)
    light=make('SceneUI_Profile_ColoredLight',unreal.PointLight,origin+unreal.Vector(0,0,2200))
    light_component=light.get_component_by_class(unreal.PointLightComponent)
    light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    light_component.set_editor_property('intensity',100000.0)
    light_component.set_editor_property('attenuation_radius',30000.0)
    light_component.set_light_color(unreal.LinearColor(1,.01,.6,1))
    light_component.set_visibility(False);light.set_actor_hidden_in_game(True)
    for key,color,x in [('Dark',unreal.LinearColor(.004,.004,.004,1),-24000),('Light',unreal.LinearColor(.85,.85,.85,1),-14000)]:
        mat=background_material('M_SceneUIBackground_'+key,color)
        actor=make('SceneUI_Background_'+key,unreal.StaticMeshActor,ground(x,79000)+unreal.Vector(0,0,2))
        comp=actor.static_mesh_component;comp.modify();comp.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Plane'));comp.set_material(0,mat)
        actor.set_actor_scale3d(unreal.Vector(45,45,1));comp.set_collision_profile_name('NoCollision',True)
        comp.set_editor_property('can_ever_affect_navigation',False);comp.set_cast_shadow(False)
        comp.set_visibility(False);actor.set_actor_hidden_in_game(True)
    camera=make('SceneUI_ReviewCamera',unreal.CameraActor,origin+unreal.Vector(0,-8500,15000))
    camera.set_editor_property('is_editor_only_actor',True)
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(),origin),False)
    camera.camera_component.set_editor_property('projection_mode',unreal.CameraProjectionMode.ORTHOGRAPHIC)
    camera.camera_component.set_editor_property('ortho_width',18000.0)
    notes=make('SceneUI_Notes',unreal.TextRenderActor,origin+unreal.Vector(0,5000,500))
    notes.set_editor_property('is_editor_only_actor',True)
    notes.text_render.set_text('SCENE UI REVIEW: OWN BLUE / ENEMY RED / 20cm\nQ: SWEEPER / B: BI ZHI MAO\nENVIRONMENT PROFILES DISABLED BY DEFAULT\nNEW NATIVE UI REQUIRES AUTHORIZED BUILD')
    notes.text_render.set_world_size(100)
    assert levels.save_current_level(),'Map save failed'
    assert unreal.EditorLoadingAndSavingUtils.load_map(MAP),'Saved map reload failed'
    report['readback_after_map_reload']=True
    for actor in api.get_all_level_actors():
        if TAG not in [str(t) for t in actor.tags]:continue
        entry={'label':actor.get_actor_label(),'class':actor.get_class().get_path_name(),
               'position_cm':list(actor.get_actor_location().to_tuple()),'editor_only':actor.get_editor_property('is_editor_only_actor')}
        if isinstance(actor,unreal.GuLiCommanderDeploymentPoint):
            entry.update(unit_id=actor.unit_type_id,team=str(actor.team),count=actor.rows*actor.columns,
                         automatic_fire=actor.allow_automatic_fire,start_idle=actor.start_idle)
            assert actor.rows*actor.columns==1 and not actor.allow_automatic_fire and actor.start_idle
        if isinstance(actor,unreal.PostProcessVolume):
            pp=actor.get_editor_property('settings')
            entry.update(enabled=actor.get_editor_property('enabled'),unbound=actor.get_editor_property('unbound'),
                         exposure_bias=pp.get_editor_property('auto_exposure_bias'),bloom_intensity=pp.get_editor_property('bloom_intensity'))
            assert not entry['enabled'] and entry['unbound']
        for cls in (unreal.StaticMeshComponent,unreal.ExponentialHeightFogComponent,unreal.PointLightComponent,unreal.CameraComponent):
            comp=actor.get_component_by_class(cls)
            if not comp:continue
            entry['component']={'class':comp.get_class().get_name(),'visible':comp.get_editor_property('visible')}
            if isinstance(comp,unreal.StaticMeshComponent):
                entry['component'].update(mesh=comp.static_mesh.get_path_name(),material=comp.get_material(0).get_path_name(),scale=list(actor.get_actor_scale3d().to_tuple()))
                assert comp.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION
            if isinstance(comp,unreal.ExponentialHeightFogComponent):entry['component']['density']=comp.get_editor_property('fog_density')
            if isinstance(comp,unreal.PointLightComponent):entry['component']['intensity']=comp.get_editor_property('intensity')
            if isinstance(comp,unreal.CameraComponent):entry['component']['ortho_width']=comp.get_editor_property('ortho_width')
        report['actors'].append(entry)
    assert len(report['actors'])==17,len(report['actors'])
    for spec in radius_specs:
        entry=next(e for e in report['actors'] if e['label']==spec['label'])
        assert abs(entry['component']['scale'][0]*48-spec['outer_radius_cm'])<.001
    report.update(success=True,radius_references=radius_specs,game_mode=mode.get_path_name(),controller=controller.get_path_name(),
        scenario_center_cm=list(origin.to_tuple()),environment_profiles_disabled_by_default=True,
        existing_deployments_preserved=True,model_comparison_resources_runtime_integrated=False,
        sweep_entry='Select Pioneer and press Q',bi_zhi_mao_entry='Commander B construction with engineering vehicle')
except Exception:
    report['error']=traceback.format_exc()
(OUT/'scene-readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.log(str({'success':report['success'],'actors':len(report['actors']),'error':report.get('error')}))
