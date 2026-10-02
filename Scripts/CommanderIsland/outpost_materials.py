"""Editor-side candidate authoring. Production publishing is a separate action."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
FILES=ROOT/'ArtSource/Environment/GuLiStrike_CommanderIsland_1800m_v1/OutpostReview'
FILES.mkdir(parents=True,exist_ok=True)
BASE='/Game/GuLiStrike/FX/StrongholdOutpost'
CANDIDATE=BASE+'/Candidate_v1'
MAP='/Game/Maps/ArtReview/LVL_CommanderIslandOutpost_v1'
SOURCE='/Game/GuLiStrike/Buildings/Materials/M_OutpostConcrete'
MESH='/Game/GuLiStrike/Buildings/Meshes/SM_OutpostPlaceholder'
LIB=unreal.EditorAssetLibrary
EDIT=unreal.MaterialEditingLibrary
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
COLORS={'Neutral':(1,1,1),'Red':(.8,.035,.02),'Blue':(.01,.16,.9)}

def save(obj):
    LIB.set_metadata_tag(obj,'GuLi.Outpost.Revision','Candidate_v1')
    assert LIB.save_loaded_asset(obj,False),obj.get_path_name()

def node(mat,cls,x,y,**props):
    obj=EDIT.create_material_expression(mat,cls,x,y)
    assert obj
    for k,v in props.items():obj.set_editor_property(k,v)
    return obj

def connect(a,out,b,pin):
    assert EDIT.connect_material_expressions(a,out,b,pin)

def make_candidates():
    assert not LIB.does_asset_exist(CANDIDATE+'/M_OutpostGlow')
    body=LIB.duplicate_asset(SOURCE,CANDIDATE+'/M_OutpostGlow')
    color=node(body,unreal.MaterialExpressionVectorParameter,200,-400,
        parameter_name='GlowColor',default_value=unreal.LinearColor(1,1,1,1),group='Outpost')
    intensity=node(body,unreal.MaterialExpressionScalarParameter,200,-220,
        parameter_name='GlowIntensity',default_value=5.0,group='Outpost')
    rim=node(body,unreal.MaterialExpressionFresnel,200,0,exponent=2.5,base_reflect_fraction=.025)
    gain=node(body,unreal.MaterialExpressionMultiply,430,-160)
    connect(rim,'',gain,'A');connect(intensity,'',gain,'B')
    emissive=node(body,unreal.MaterialExpressionMultiply,650,-350)
    connect(color,'RGB',emissive,'A');connect(gain,'',emissive,'B')
    assert EDIT.connect_material_property(emissive,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.layout_material_expressions(body);EDIT.recompile_material(body);save(body)

    halo=TOOLS.create_asset('M_OutpostHalo',CANDIDATE,unreal.Material,unreal.MaterialFactoryNew())
    halo.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
    halo.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    halo.set_editor_property('two_sided',True)
    uv=node(halo,unreal.MaterialExpressionTextureCoordinate,-650,150)
    radial=node(halo,unreal.MaterialExpressionCustom,-400,150,
        code='float r=length(UV*2-1); return pow(saturate(1-r),2.4)*smoothstep(.12,.4,r);',
        output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1,desc='Soft halo with a clear center')
    inp=unreal.CustomInput();inp.set_editor_property('input_name','UV');radial.set_editor_property('inputs',[inp])
    connect(uv,'',radial,'UV')
    opacity=node(halo,unreal.MaterialExpressionScalarParameter,-400,350,
        parameter_name='HaloOpacity',default_value=.22,group='Outpost')
    alpha=node(halo,unreal.MaterialExpressionMultiply,-50,150)
    connect(radial,'',alpha,'A');connect(opacity,'',alpha,'B')
    assert EDIT.connect_material_property(alpha,'',unreal.MaterialProperty.MP_OPACITY)
    c=node(halo,unreal.MaterialExpressionVectorParameter,-400,-230,
        parameter_name='GlowColor',default_value=unreal.LinearColor(1,1,1,1),group='Outpost')
    gain=node(halo,unreal.MaterialExpressionScalarParameter,-400,-50,
        parameter_name='GlowIntensity',default_value=5.0,group='Outpost')
    emission=node(halo,unreal.MaterialExpressionMultiply,-50,-100)
    connect(c,'RGB',emission,'A');connect(gain,'',emission,'B')
    assert EDIT.connect_material_property(emission,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.layout_material_expressions(halo);EDIT.recompile_material(halo);save(halo)
    instances=[]
    for team,rgb in COLORS.items():
        for part,mat in [('Glow',body),('Halo',halo)]:
            mi=TOOLS.create_asset('MI_Outpost'+part+'_'+team,CANDIDATE,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
            EDIT.set_material_instance_parent(mi,mat)
            EDIT.set_material_instance_vector_parameter_value(mi,'GlowColor',unreal.LinearColor(*rgb,1))
            EDIT.update_material_instance(mi);save(mi);instances.append(mi.get_path_name())
    graphs={part:json.loads(unreal.MaterialNodeService.export_material_graph(CANDIDATE+'/M_Outpost'+part)) for part in ('Glow','Halo')}
    (FILES/'candidate_graphs.json').write_text(json.dumps(graphs,indent=2),encoding='utf-8')
    return {'success':True,'body':body.get_path_name(),'halo':halo.get_path_name(),'instances':instances,'textures_preserved':True}

def static_actor(label,mesh,material,location,scale,rotation=unreal.Rotator()):
    a=ACTORS.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*location),rotation)
    a.set_actor_label(label);a.set_folder_path('OutpostReview');a.set_actor_scale3d(unreal.Vector(*scale))
    c=a.get_editor_property('static_mesh_component');assert c.set_static_mesh(mesh)
    c.set_material(0,material);c.set_collision_profile_name('NoCollision',True);a.set_actor_enable_collision(False)
    return a

def preview():
    assert not LEVELS.is_in_play_in_editor()
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    previous=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name().split('.')[0]
    assert not LIB.does_asset_exist(MAP)
    assert LEVELS.new_level_from_template(MAP,'/Engine/Maps/Templates/Template_Default')
    mesh=unreal.load_asset(MESH);bounds=mesh.get_bounds();scale=1000/(2*bounds.box_extent.z)
    camera=unreal.Vector(0,-11000,6000);target=unreal.Vector(0,0,500)
    rotation=unreal.MathLibrary.find_look_at_rotation(camera,target)
    for a in ACTORS.get_all_level_actors():
        if isinstance(a,unreal.StaticMeshActor) and a.get_actor_label()=='Floor':a.set_actor_scale3d(unreal.Vector(160,80,1))
        if isinstance(a,unreal.DirectionalLight):a.get_editor_property('directional_light_component').set_editor_property('intensity',3.0)
    data=[]
    for i,team in enumerate(COLORS):
        x=(i-1)*3000
        base=(x-bounds.origin.x*scale,-bounds.origin.y*scale,-(bounds.origin.z-bounds.box_extent.z)*scale)
        static_actor('OutpostReview_'+team,mesh,unreal.load_asset(CANDIDATE+'/MI_OutpostGlow_'+team),base,(scale,scale,scale))
        center=unreal.Vector(x,0,500)
        halo_rotation=unreal.MathLibrary.make_rot_from_z(camera-center)
        static_actor('OutpostHalo_'+team,unreal.load_asset('/Engine/BasicShapes/Plane'),
            unreal.load_asset(CANDIDATE+'/MI_OutpostHalo_'+team),(x,0,500),(16,18,1),halo_rotation)
        data.append({'team':team,'ground_cm':[x,0,0],'height_cm':1000,'scale':scale})
    cam=ACTORS.spawn_actor_from_class(unreal.CameraActor,camera,rotation);cam.set_actor_label('OutpostReviewCamera')
    cam.get_component_by_class(unreal.CameraComponent).set_field_of_view(50)
    unreal.EditorLevelLibrary.set_level_viewport_camera_info(camera,rotation)
    unreal.ViewportService.set_view_mode('lit');unreal.ViewportService.set_game_view(True)
    unreal.ViewportService.set_exposure(True,0);unreal.ViewportService.set_realtime(True)
    ACTORS.set_selected_level_actors([])
    assert LEVELS.save_current_level()
    assert unreal.AutomationLibrary.take_high_res_screenshot(1920,1080,str(FILES/'candidate_team_glow.png'),cam,delay=1,force_game_view=True)
    result={'success':True,'map':MAP,'previous_map':previous,'actors':data,'screenshot':str(FILES/'candidate_team_glow.png'),'runtime_animation':'Not run; these are editor static candidates.'}
    (FILES/'candidate_preview.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    return result

def publish():
    # Call only after the user has reviewed Candidate_v1 and approved it.
    for part in ('Glow','Halo'):
        destination=BASE+'/M_Outpost'+part
        assert not LIB.does_asset_exist(destination)
        mat=LIB.duplicate_asset(CANDIDATE+'/M_Outpost'+part,destination);save(mat)
    return {'success':True,'published':[BASE+'/M_OutpostGlow',BASE+'/M_OutpostHalo']}

unreal.MCPythonHelper.submit_result(json.dumps({'materials':make_candidates,'preview':preview,'publish':publish}[ISLAND_ACTION]()))
