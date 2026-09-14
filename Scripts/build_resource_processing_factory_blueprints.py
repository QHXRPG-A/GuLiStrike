"""Build the RPF runtime Blueprint and its separate presentation map.

Use a normal source-built Editor process after import_resource_processing_factory.
The generated runtime graphs call Engine functions only; VibeUE is an editor tool.
"""
from pathlib import Path
import json
import traceback
import argparse
import unreal

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'outputs/resource-processing-factory-20260909'
BASE='/Game/GuLiStrike/Buildings/ResourceProcessingFactory'
BP=BASE+'/Blueprints/BP_ResourceProcessingFactory'
OWNER='GuLiStrike.ResourceProcessingFactory.20260909'
B=unreal.BlueprintService


def check(value,message):
    if not value:raise RuntimeError(message)
    return value


def save(asset):
    unreal.EditorAssetLibrary.set_metadata_tag(asset,'GuLi.RPF.Owner',OWNER)
    check(unreal.EditorAssetLibrary.save_loaded_asset(asset,False),'Save '+asset.get_path_name())


def create(path):
    asset=unreal.load_asset(path)
    if asset:
        if unreal.EditorAssetLibrary.get_metadata_tag(asset,'GuLi.RPF.BlueprintVersion')=='1':return asset,False
        check(unreal.EditorAssetLibrary.get_metadata_tag(asset,'GuLi.RPF.Owner')==OWNER,'Asset is not managed by this builder: '+path)
        check(unreal.EditorAssetLibrary.delete_asset(path),'Remove incomplete managed Blueprint '+path)
    folder,name=path.rsplit('/',1)
    check(B.create_blueprint(name,'/Script/Engine.Actor',folder),'Create Blueprint '+path)
    for node in B.get_nodes_in_graph(path,'EventGraph'):
        if node.node_type=='K2Node_Event':B.delete_node(path,'EventGraph',node.node_id)
    asset=unreal.load_asset(path);save(asset)
    return asset,True


def comp(path,name,kind,parent=''):
    check(B.add_component(path,'/Script/Engine.'+kind,name,parent),'Add component '+name)


def prop(path,name,key,value):
    check(B.set_component_property(path,name,key,str(value)),'Component property '+name+'.'+key)


class Graph:
    def __init__(self,path,graph='EventGraph'):
        self.path=path;self.graph=graph;self.nodes=[];self.i=0
    def node(self,method,*args):
        self.i+=1
        n=check(getattr(B,method)(self.path,self.graph,*args,(self.i%8)*300,(self.i//8)*380),'Node '+method+' '+str(args))
        self.nodes.append(n);return n
    def call(self,cls,name):return self.node('add_function_call_node',cls if cls.startswith('/') else '/Script/Engine.'+cls,name)
    def var(self,name):return self.node('add_get_variable_node',name)
    def setvar(self,name):return self.node('add_set_variable_node',name)
    def math(self,name):return self.node('add_math_node',name,'Float')
    def cmp(self,name):return self.node('add_comparison_node',name,'Float')
    def event(self,name):return self.node('add_custom_event_node',name)
    def pins(self,n):return [p.pin_name for p in B.get_node_pins(self.path,self.graph,n)]
    def link(self,a,ap,b,bp):
        check(ap in self.pins(a) and bp in self.pins(b),'Pins '+ap+' '+str(self.pins(a))+' -> '+bp+' '+str(self.pins(b)))
        check(B.connect_nodes(self.path,self.graph,a,ap,b,bp),'Link '+ap+' -> '+bp)
    def value(self,n,p,v):
        check(p in self.pins(n),'Missing pin '+p+' '+str(self.pins(n)))
        check(B.set_node_pin_value(self.path,self.graph,n,p,str(v)),'Set pin '+p)


def compile_bp(path):
    c=B.compile_blueprint(path)
    check(c.success,'Blueprint compile: '+str(list(c.errors)))
    return {'success':c.success,'errors':list(c.errors),'warnings':list(c.warnings)}


def build_physics():
    mesh=check(unreal.load_asset(BASE+'/Meshes/SK_RPF_Door'),'Door mesh')
    sub=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    physics=mesh.get_editor_property('physics_asset')
    if not physics:physics=check(sub.create_physics_asset(mesh,True,0),'Create door physics')
    check(physics.get_path_name().startswith(BASE+'/'),'Physics asset outside factory root')
    # Non-editor UPROPERTY arrays are not visible to Python; their owned body
    # subobjects are. Automatic fitting gives us the correctly linked hinge body.
    bodies=[s for s in unreal.ObjectIterator(unreal.BodySetup) if s.get_outer()==physics]
    check(len(bodies)==1 and str(bodies[0].get_editor_property('bone_name'))=='door_hinge','Unexpected door physics hierarchy')
    body=bodies[0]
    hinge=unreal.SkeletonService.get_bone_transform(mesh.get_path_name(),'door_hinge',True)
    scale=hinge.scale3d
    check(abs(scale.x-scale.y)<.0001 and abs(scale.x-scale.z)<.0001 and scale.x>0,'Nonuniform hinge scale')
    box=unreal.KBoxElem()
    box.set_editor_property('center',hinge.inverse_transform_location(unreal.Vector(1180,0,820)))
    box.set_editor_property('rotation',hinge.rotation.inversed().rotator())
    for key,value in [('x',150),('y',3575),('z',1390)]:box.set_editor_property(key,value/scale.x)
    box.set_editor_property('name','DoorPanel_Rigid')
    box.set_editor_property('collision_enabled',unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    geometry=unreal.KAggregateGeom();geometry.set_editor_property('box_elems',[box])
    body.set_editor_property('agg_geom',geometry)
    body.set_editor_property('physics_type',unreal.PhysicsType.PHYS_TYPE_KINEMATIC)
    body.set_editor_property('consider_for_bounds',True)
    check(sub.assign_physics_asset(mesh,physics),'Assign door physics')
    save(physics);save(mesh)
    for name in ['Armor','Interior','Details']:
        mat=check(unreal.load_asset(BASE+'/Materials/M_RPF_'+name),'Material '+name)
        mat.set_editor_property('used_with_skeletal_mesh',True)
        mat.set_editor_property('used_with_nanite',True)
        unreal.MaterialEditingLibrary.recompile_material(mat);save(mat)
    (OUT/'ue_physics_report.json').write_text(json.dumps({'path':physics.get_path_name(),'bone':'door_hinge','kinematic':True,'box_cm':[150,3575,1390],'center_cm':[1180,0,820],'bone_scale':scale.x},indent=2),encoding='utf-8')


def build_factory():
    asset,fresh=create(BP)
    if not fresh:return finish_factory(asset)
    comp(BP,'FactoryRoot','SceneComponent')
    check(B.set_root_component(BP,'FactoryRoot'),'Factory root')
    prop(BP,'FactoryRoot','Mobility','Static')
    comp(BP,'Body','StaticMeshComponent','FactoryRoot')
    prop(BP,'Body','StaticMesh',BASE+'/Meshes/SM_RPF_Body.SM_RPF_Body')
    prop(BP,'Body','Mobility','Static')
    check(B.set_collision_settings(BP,'Body','QueryAndPhysics','WorldStatic','BlockAll',{}),'Body collision')
    comp(BP,'Door','SkeletalMeshComponent','FactoryRoot')
    prop(BP,'Door','SkinnedAsset',BASE+'/Meshes/SK_RPF_Door.SK_RPF_Door')
    prop(BP,'Door','Mobility','Movable')
    prop(BP,'Door','AnimationMode','AnimationSingleNode')
    animpath=BASE+'/Animations/A_RPF_Door_Open.A_RPF_Door_Open'
    prop(BP,'Door','AnimationData','(AnimToPlay="'+animpath+'",bSavedLooping=False,bSavedPlaying=False,SavedPosition=0.0,SavedPlayRate=1.0)')
    prop(BP,'Door','VisibilityBasedAnimTickOption','AlwaysTickPoseAndRefreshBones')
    check(B.set_collision_settings(BP,'Door','QueryAndPhysics','WorldDynamic','BlockAll',{}),'Door collision')
    for name,loc in [('EntryPoint','(X=3000,Y=0,Z=120)'),('UnloadPoint','(X=-700,Y=0,Z=120)')]:
        comp(BP,name,'SceneComponent','FactoryRoot');prop(BP,name,'RelativeLocation',loc);prop(BP,name,'Mobility','Static')
    for i,(x,y) in enumerate([(x,y) for x in [-1400,-400,600] for y in [-1500,1500]]):
        name='BayLight_%02d'%i;comp(BP,name,'RectLightComponent','FactoryRoot')
        for k,v in {'Mobility':'Movable','RelativeLocation':f'(X={x},Y={y},Z=1200)','RelativeRotation':f'(Pitch=-34,Yaw={90 if y<0 else -90},Roll=0)','Intensity':'18000','AttenuationRadius':'2600','SourceWidth':'300','SourceHeight':'130','LightColor':'(R=8,G=82,B=255,A=255)','CastShadows':'False'}.items():prop(BP,name,k,v)
    check(B.add_variable(BP,'DoorDuration','Float','3.0'),'DoorDuration')
    check(B.add_variable(BP,'DoorAlpha','Float','0.0'),'DoorAlpha')
    B.modify_variable(BP,'DoorDuration',new_category='Factory|Door',new_tooltip='Full travel time in seconds. Values below 0.05 are clamped.',set_instance_editable=1)
    for name in ['OnDoorOpened','OnDoorClosed']:check(B.add_event_dispatcher(BP,name),'Dispatcher '+name)
    g=Graph(BP)
    timeline=check(B.add_timeline(BP,'EventGraph','DoorMotion',3.0,False,False,False,1400,0),'Door timeline')
    check(B.add_timeline_float_track(BP,'DoorMotion','Alpha'),'Door timeline track')
    check(B.add_timeline_float_key(BP,'DoorMotion','Alpha',0,0,'Linear'),'Door timeline start')
    check(B.add_timeline_float_key(BP,'DoorMotion','Alpha',3,1,'Linear'),'Door timeline end')
    B.compile_blueprint(BP)
    door=g.var('Door');timelinevar=g.var('DoorMotion')
    begin=g.node('add_event_node','ReceiveBeginPlay')
    animation=g.call('SkeletalMeshComponent','SetAnimation');g.value(animation,'NewAnimToPlay',animpath)
    g.link(door,'Door',animation,'self');g.link(begin,'then',animation,'execute')
    position=g.call('SkeletalMeshComponent','SetPosition');g.link(door,'Door',position,'self');g.value(position,'InPos',0);g.value(position,'bFireNotifies','false');g.link(animation,'then',position,'execute')
    # Timeline samples the Blender-authored easing curve; its own Alpha is linear.
    setalpha=g.setvar('DoorAlpha');g.link(timeline,'Update',setalpha,'execute');g.link(timeline,'Alpha',setalpha,'DoorAlpha')
    sample=g.math('Multiply');g.link(timeline,'Alpha',sample,'A');g.value(sample,'B',3)
    update=g.call('SkeletalMeshComponent','SetPosition');g.link(door,'Door',update,'self');g.link(sample,'ReturnValue',update,'InPos');g.value(update,'bFireNotifies','false');g.link(setalpha,'then',update,'execute')
    requested={}
    for opening in [True,False]:
        name='RequestOpenDoor' if opening else 'RequestCloseDoor'
        ev=g.event(name);requested[name]=ev
        alpha=g.var('DoorAlpha');compare=g.cmp('Less' if opening else 'Greater');g.link(alpha,'DoorAlpha',compare,'A');g.value(compare,'B',1 if opening else 0)
        branch=g.node('add_branch_node');g.link(ev,'then',branch,'execute');g.link(compare,'ReturnValue',branch,'Condition')
        duration=g.var('DoorDuration');minimum=g.math('Max');g.link(duration,'DoorDuration',minimum,'A');g.value(minimum,'B',.05)
        rate=g.math('Divide');g.value(rate,'A',3);g.link(minimum,'ReturnValue',rate,'B')
        setrate=g.call('TimelineComponent','SetPlayRate');g.link(timelinevar,'DoorMotion',setrate,'self');g.link(rate,'ReturnValue',setrate,'NewRate');g.link(branch,'then',setrate,'execute')
        g.link(setrate,'then',timeline,'Play' if opening else 'Reverse')
    finished=g.node('add_branch_node');alpha=g.var('DoorAlpha');isopen=g.cmp('GreaterEqual');g.link(alpha,'DoorAlpha',isopen,'A');g.value(isopen,'B',.99999)
    g.link(isopen,'ReturnValue',finished,'Condition');g.link(timeline,'Finished',finished,'execute')
    for pin,name in [('then','OnDoorOpened'),('else','OnDoorClosed')]:
        delegate=g.node('add_call_delegate_node',name);g.link(finished,pin,delegate,'execute')
    # Public synchronous functions delegate to graph events that own the timeline.
    B.compile_blueprint(BP)
    for name,event in [('OpenDoor','RequestOpenDoor'),('CloseDoor','RequestCloseDoor')]:
        check(B.create_function(BP,name,False),'Function '+name)
        fg=Graph(BP,name)
        entries=[n for n in B.get_nodes_in_graph(BP,name) if 'FunctionEntry' in n.node_type]
        check(len(entries)==1,'Function entry '+name)
        call=fg.call(BP+'.BP_ResourceProcessingFactory_C',event);fg.link(entries[0].node_id,'then',call,'execute')
    # BlueprintReadOnly also disallows our own Set node. Keep storage private and
    # expose the normalized progress through a pure, read-only public function.
    B.modify_variable(BP,'DoorAlpha',new_category='Factory|Door',new_tooltip='Private normalized progress. Read externally with GetDoorAlpha().',set_private=1,set_blueprint_read_only=0)
    check(B.create_function(BP,'GetDoorAlpha',True),'Function GetDoorAlpha')
    check(B.add_function_output(BP,'GetDoorAlpha','DoorAlpha','Float'),'DoorAlpha return')
    fg=Graph(BP,'GetDoorAlpha')
    returns=[n for n in B.get_nodes_in_graph(BP,'GetDoorAlpha') if 'FunctionResult' in n.node_type]
    check(len(returns)==1,'DoorAlpha function return')
    state=fg.var('DoorAlpha');fg.link(state,'DoorAlpha',returns[0].node_id,'DoorAlpha')
    report=compile_bp(BP)
    unreal.EditorAssetLibrary.set_metadata_tag(asset,'GuLi.RPF.BlueprintVersion','1');save(asset)
    (OUT/'ue_blueprint_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return finish_factory(asset)


def finish_factory(asset):
    # Runtime access joins the existing apron to the terrain without adding vehicle logic to the factory.
    if not B.component_exists(BP,'AccessRamp'):
        check(B.add_component(BP,'/Script/GuLiStrike.GuLiGroundAccessRampComponent','AccessRamp','FactoryRoot'),'Add terrain access ramp')
        check(unreal.GuLiResourceAuthoringLibrary.normalize_blueprint_local_component_hierarchy(BP),'Normalize access ramp hierarchy')
        prop(BP,'AccessRamp','OverrideMaterials',"(Material'/Game/GuLiStrike/Buildings/ResourceProcessingFactory/Materials/M_RPF_Interior.M_RPF_Interior')")
    prop(BP,'EntryPoint','RelativeLocation','(X=4000,Y=-600,Z=120)')
    prop(BP,'UnloadPoint','RelativeLocation','(X=-500,Y=-600,Z=120)')
    compile_bp(BP);save(asset)
    revision=unreal.EditorAssetLibrary.get_metadata_tag(asset,'GuLi.RPF.InterfaceRevision')
    if revision=='3':return asset
    # UE 5.7 represents local SCS attachment through ChildNodes only. Older
    # BlueprintService builds also populated inherited-parent fields, which made
    # PostLoad diagnose the otherwise valid tree as cyclic. Reparenting is
    # idempotent and clears that obsolete duplicate representation.
    check(unreal.GuLiResourceAuthoringLibrary.normalize_blueprint_local_component_hierarchy(BP),'Normalize factory SCS hierarchy')
    if revision=='2':
        report=compile_bp(BP)
        unreal.EditorAssetLibrary.set_metadata_tag(asset,'GuLi.RPF.InterfaceRevision','3');save(asset)
        (OUT/'ue_blueprint_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        return asset
    fg=Graph(BP,'GetDoorAlpha');nodes=B.get_nodes_in_graph(BP,'GetDoorAlpha')
    entry=next(n for n in nodes if 'FunctionEntry' in n.node_type)
    result=next(n for n in nodes if 'FunctionResult' in n.node_type)
    fg.link(entry.node_id,'then',result.node_id,'execute')
    for i in range(6):prop(BP,'BayLight_%02d'%i,'CastShadows','True')
    report=compile_bp(BP)
    unreal.EditorAssetLibrary.set_metadata_tag(asset,'GuLi.RPF.InterfaceRevision','3');save(asset)
    (OUT/'ue_blueprint_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return asset


def build_controller():
    path=BASE+'/Demo/BP_RPF_ShowcaseController'
    asset,fresh=create(path)
    if not fresh:return finish_controller(asset,path)
    check(B.add_variable(path,'FactoryTarget',BP+'.BP_ResourceProcessingFactory_C'),'FactoryTarget')
    check(B.add_variable(path,'LoopDemonstration','bool','true'),'LoopDemonstration')
    B.modify_variable(path,'FactoryTarget',set_instance_editable=1)
    B.modify_variable(path,'LoopDemonstration',set_instance_editable=1)
    g=Graph(path);begin=g.node('add_event_node','ReceiveBeginPlay')
    wait=g.call('KismetSystemLibrary','Delay');g.value(wait,'Duration',.5);g.link(begin,'then',wait,'execute')
    target=g.var('FactoryTarget');opening=g.call(BP+'.BP_ResourceProcessingFactory_C','OpenDoor');g.link(target,'FactoryTarget',opening,'self');g.link(wait,'then',opening,'execute')
    loop=g.var('LoopDemonstration');branch=g.node('add_branch_node');g.link(loop,'LoopDemonstration',branch,'Condition');g.link(opening,'then',branch,'execute')
    hold=g.call('KismetSystemLibrary','Delay');g.value(hold,'Duration',4.5);g.link(branch,'then',hold,'execute')
    closing=g.call(BP+'.BP_ResourceProcessingFactory_C','CloseDoor');g.link(target,'FactoryTarget',closing,'self');g.link(hold,'then',closing,'execute')
    closed=g.call('KismetSystemLibrary','Delay');g.value(closed,'Duration',4.5);g.link(closing,'then',closed,'execute');g.link(closed,'then',opening,'execute')
    compile_bp(path);unreal.EditorAssetLibrary.set_metadata_tag(asset,'GuLi.RPF.BlueprintVersion','1');save(asset)
    return finish_controller(asset,path)


def finish_controller(asset,path):
    if unreal.EditorAssetLibrary.get_metadata_tag(asset,'GuLi.RPF.DemoRevision')=='2':return asset
    check(B.add_variable(path,'StartAutomatically','bool','true'),'Showcase automatic start')
    B.modify_variable(path,'StartAutomatically',set_instance_editable=1)
    for name in ['OpenedCount','ClosedCount']:
        check(B.add_variable(path,name,'int','0'),'Showcase completed cycles')
        B.modify_variable(path,name,new_category='Showcase|Events',new_tooltip='Completion events received by this demonstration instance.')
    g=Graph(path);nodes=B.get_nodes_in_graph(path,'EventGraph')
    events=[n for n in nodes if n.node_type=='K2Node_Event']
    begin=max(events,key=lambda n:n.pos_x)
    for node in events:
        if node.node_id!=begin.node_id and set(node.pin_names)=={'OutputDelegate','then'}:B.delete_node(path,'EventGraph',node.node_id)
    initial_wait=next(n for n in nodes if any(p.pin_name=='Duration' and p.default_value and abs(float(p.default_value)-.5)<.001 for p in B.get_node_pins(path,'EventGraph',n.node_id)))
    previous=begin.node_id
    for event,counter in [('OnDoorOpened','OpenedCount'),('OnDoorClosed','ClosedCount')]:
        bind=g.node('add_delegate_bind_on_variable','FactoryTarget',event)
        listener=g.event('Showcase_'+event)
        g.link(listener,'OutputDelegate',bind,'Delegate');g.link(previous,'then',bind,'execute');previous=bind
        value=g.var(counter);add=g.node('add_math_node','Add','Int');g.link(value,counter,add,'A');g.value(add,'B',1)
        write=g.setvar(counter);g.link(listener,'then',write,'execute');g.link(add,'ReturnValue',write,counter)
    branch=g.node('add_branch_node');auto=g.var('StartAutomatically')
    g.link(previous,'then',branch,'execute');g.link(auto,'StartAutomatically',branch,'Condition');g.link(branch,'then',initial_wait.node_id,'execute')
    compile_bp(path)
    unreal.EditorAssetLibrary.set_metadata_tag(asset,'GuLi.RPF.DemoRevision','2');save(asset)
    return asset


def build_map():
    map_path=BASE+'/Demo/LVL_RPF_Showcase'
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(map_path):
        check(level.load_level(map_path),'Load showcase map')
    else:check(level.new_level(map_path),'New showcase map')
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    present=actors.get_all_level_actors()
    if any(a.get_actor_label()=='RPF_HeroCamera' for a in present):
        dress_map();return map_path
    check(not any(a.get_actor_label().startswith('RPF_') for a in present),'Partial showcase requires inspection before rebuilding')
    # This phase needs a rendered editor viewport (RenderOffscreen is supported).
    # Run only the asset phase with NullRHI.
    def spawn(actor_class,location,rotation=unreal.Rotator()):
        return check(actors.spawn_actor_from_class(actor_class,location,rotation),'Spawn '+str(actor_class))
    cls=unreal.load_class(None,BP+'.BP_ResourceProcessingFactory_C')
    control=unreal.load_class(None,BASE+'/Demo/BP_RPF_ShowcaseController.BP_RPF_ShowcaseController_C')
    for i,(label,offset) in enumerate([('CLOSED',-8000),('ANIMATED',0),('OPEN',8000)]):
        actor=spawn(cls,unreal.Vector(0,offset,0));actor.set_actor_label('RPF_'+label)
        c=spawn(control,unreal.Vector(0,offset,0));c.set_actor_label('RPF_Demo_'+label)
        c.set_editor_property('FactoryTarget',actor);c.set_editor_property('LoopDemonstration',i==1);c.set_editor_property('StartAutomatically',i!=0)
        text=spawn(unreal.TextRenderActor,unreal.Vector(2800,offset,30),unreal.Rotator(0,0,0))
        tc=text.get_component_by_class(unreal.TextRenderComponent);tc.set_text(label);tc.set_world_size(180);tc.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    ground=spawn(unreal.StaticMeshActor,unreal.Vector(0,0,-55))
    gc=ground.static_mesh_component;gc.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'));ground.set_actor_scale3d(unreal.Vector(450,450,1))
    ground.set_actor_label('ShowcaseFloor')
    sun=spawn(unreal.DirectionalLight,unreal.Vector(0,0,5000),unreal.Rotator(-42,-35,0));sun.light_component.set_intensity(4)
    sky=spawn(unreal.SkyLight,unreal.Vector(0,0,5000));sky.light_component.set_intensity(.65)
    spawn(unreal.SkyAtmosphere,unreal.Vector(0,0,0))
    camera=spawn(unreal.CameraActor,unreal.Vector(8000,-9700,6500),unreal.Rotator(-24,130,0))
    camera.set_actor_label('RPF_HeroCamera')
    look=unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(),unreal.Vector(0,0,850));camera.set_actor_rotation(look,False)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(camera.get_actor_location(),look)
    settings=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_world_settings()
    settings.set_editor_property('default_game_mode',unreal.GameModeBase)
    spawn(unreal.PlayerStart,unreal.Vector(3600,0,250),unreal.Rotator(0,180,0))
    check(level.save_current_level(),'Save showcase map')
    dress_map()
    return map_path


def dress_map():
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    for actor in actors:
        if actor.get_class().get_name()=='BP_ResourceProcessingFactory_C':
            names=[light.get_name() for light in actor.get_components_by_class(unreal.RectLightComponent)]
            for name in names:
                light=next(c for c in actor.get_components_by_class(unreal.RectLightComponent) if c.get_name()==name)
                if not light.get_editor_property('cast_shadows'):light.set_editor_property('cast_shadows',True)
    floor=next(a for a in actors if a.get_actor_label()=='ShowcaseFloor')
    path=BASE+'/Demo/M_RPF_ShowcaseFloor';material=unreal.load_asset(path)
    if not material:
        material=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_RPF_ShowcaseFloor',BASE+'/Demo',unreal.Material,unreal.MaterialFactoryNew())
        color=unreal.MaterialEditingLibrary.create_material_expression(material,unreal.MaterialExpressionConstant3Vector,-250,0)
        color.set_editor_property('constant',unreal.LinearColor(.065,.08,.1,1))
        unreal.MaterialEditingLibrary.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
        rough=unreal.MaterialEditingLibrary.create_material_expression(material,unreal.MaterialExpressionConstant,-250,160);rough.set_editor_property('r',.83)
        unreal.MaterialEditingLibrary.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
        unreal.MaterialEditingLibrary.recompile_material(material);save(material)
    floor.static_mesh_component.set_material(0,material)
    floor.set_actor_scale3d(unreal.Vector(2000,2000,1))
    camera=next(a for a in actors if a.get_actor_label()=='RPF_HeroCamera')
    camera.set_actor_location(unreal.Vector(6600,-15100,5800),False,False)
    look=unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(),unreal.Vector(0,-8000,900));camera.set_actor_rotation(look,False)
    cc=camera.get_component_by_class(unreal.CameraComponent);cc.set_field_of_view(55)
    settings=cc.get_editor_property('post_process_settings');settings.set_editor_property('override_auto_exposure_bias',True);settings.set_editor_property('auto_exposure_bias',-.4);cc.set_editor_property('post_process_settings',settings)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(camera.get_actor_location(),look)
    check(unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level(),'Save showcase presentation')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase',choices=['assets','map','repair'],default='assets')
    options=parser.parse_args()
    try:
        if options.phase=='assets':
            build_physics();build_factory();build_controller()
            (OUT/'ue_blueprint_assets_complete.json').write_text(json.dumps({'blueprint':BP,'success':True},indent=2),encoding='utf-8')
        elif options.phase=='repair':
            asset=check(unreal.load_asset(BP),'Factory Blueprint')
            finish_factory(asset)
            (OUT/'ue_blueprint_repair_complete.json').write_text(json.dumps({'blueprint':BP,'success':True},indent=2),encoding='utf-8')
        else:
            path=build_map()
            (OUT/'ue_blueprint_complete.json').write_text(json.dumps({'blueprint':BP,'map':path,'success':True},indent=2),encoding='utf-8')
        (OUT/'ue_blueprint_error.txt').unlink(missing_ok=True)
    except Exception:
        (OUT/'ue_blueprint_error.txt').write_text(traceback.format_exc(),encoding='utf-8');unreal.log_error(traceback.format_exc())
    finally:unreal.SystemLibrary.quit_editor()


if __name__=='__main__':main()
