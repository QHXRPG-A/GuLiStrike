"""Author the electromagnetic mining vehicle and reusable collector assets in UE.

Run individual stages through Scripts/ue_exec.py; only explicit owned assets save.
"""
import json
import math
from pathlib import Path
import unreal

import sys
from pathlib import Path
sys.path.insert(0,str(Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))/"Scripts/Vfx"))
from vfx_registry import vfx_id, resource as vfx_resource, scale as vfx_scale, require_id, visual_variant
from configure_registry_bindings import configure_blueprints

OUT = Path('D:/UE5.7/test1/outputs/electromagnetic-miner')
BASE = '/Game/GuLiStrike/Vehicles/ElectromagneticMiner'
BP = BASE + '/BP_MiningVehicle_TransporterLvl2'
SOURCE = '/Game/MC_Vehicle_Constructor/Blueprints/Wheeled_Transporter_Lvl2'
FX = '/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Green'
S = unreal.BlueprintService
E = unreal.EditorAssetLibrary

def record(name, data):
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / name).write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')

def save(asset):
    if isinstance(asset,str): asset=unreal.load_asset(asset)
    assert asset and E.save_loaded_asset(asset, False), 'Cannot save '+str(asset)

def compile_bp():
    r=S.compile_blueprint(BP)
    record('blueprint-compile.json',dict(success=r.success, errors=list(r.errors), warnings=list(r.warnings)))
    assert r.success and r.num_errors==0, str(r)
    return r

def mat(name,color,metallic=0.0,roughness=.4,emissive=0.0):
    path=BASE+'/Materials/'+name
    existing=unreal.load_asset(path)
    if existing:return existing
    m=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,BASE+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    lib=unreal.MaterialEditingLibrary
    c=lib.create_material_expression(m,unreal.MaterialExpressionConstant3Vector,-420,-100)
    c.set_editor_property('constant',unreal.LinearColor(*color,1))
    assert lib.connect_material_property(c,'',unreal.MaterialProperty.MP_BASE_COLOR)
    for k,value in [(unreal.MaterialProperty.MP_METALLIC,metallic),(unreal.MaterialProperty.MP_ROUGHNESS,roughness)]:
        n=lib.create_material_expression(m,unreal.MaterialExpressionConstant,-400,140+int(value*100))
        n.set_editor_property('r',value);assert lib.connect_material_property(n,'',k)
    if emissive:
        mult=lib.create_material_expression(m,unreal.MaterialExpressionMultiply,-170,-230)
        mult.set_editor_property('const_b',emissive)
        assert lib.connect_material_expressions(c,'',mult,'A')
        assert lib.connect_material_property(mult,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.recompile_material(m);save(m)
    return m

def build_beam():
    if not unreal.load_asset(FX):assert E.duplicate_asset('/Game/Arc_Beam_VFX/VFX/NS_Beam',FX)
    # Reuse the existing green Arc Beam effect. Reduce its hot green value for a mining tool.
    settings=unreal.NiagaraService.get_all_editable_settings(FX)
    for p in settings.rapid_iteration_parameters:
        if p.setting_path.endswith('.Color.Color'):
            assert unreal.NiagaraService.set_parameter(FX,p.setting_path,'(R=0.25,G=18,B=0.8,A=1)')
    # Authored baseline is 5 cm; set 25 cm deterministically so reruns never compound.
    assert unreal.NiagaraService.set_parameter(FX,'Constants.Beam.BeamWidth.Beam Width','25.0')
    # Beam001 is a second noisy lightning ribbon. Keep the primary laser and endpoint sparks.
    assert unreal.NiagaraService.enable_emitter(FX,'Beam001',False)
    assert unreal.NiagaraService.enable_emitter(FX,'Spark001',False)
    r=unreal.NiagaraService.compile_with_results(FX)
    record('niagara-compile.json',dict(result=str(r)))
    assert r.success,str(r)
    assert unreal.NiagaraService.save_system(FX)
    return FX

def prop(comp,key,value):
    if key in ['StaticMesh','Asset']:
        obj=unreal.load_asset(value)
        assert obj,(key,value)
        value=obj.get_path_name()
    assert S.set_component_property(BP,comp,key,value),(comp,key,value)

def add(comp,kind,parent,location=None,rotation=None):
    if not S.component_exists(BP,comp):
        bp=unreal.load_asset(BP);sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
        lib=unreal.SubobjectDataBlueprintFunctionLibrary
        handles=sub.k2_gather_subobject_data_for_blueprint(bp)
        parent_handle=next(h for h in handles if str(lib.get_variable_name(lib.get_data(h)))==parent)
        params=unreal.AddNewSubobjectParams(parent_handle=parent_handle,new_class=getattr(unreal,kind),blueprint_context=bp)
        handle,reason=sub.add_new_subobject(params)
        assert lib.is_handle_valid(handle),(comp,str(reason))
        assert sub.rename_subobject(handle,unreal.Text(comp)),comp
    if location:prop(comp,'RelativeLocation','(X=%g,Y=%g,Z=%g)'%location)
    if rotation:prop(comp,'RelativeRotation','(Pitch=%g,Yaw=%g,Roll=%g)'%rotation)

def build_styled_collector():
    """Preserve vendor UVs/normals and weathered painted materials on the weapon."""
    path=BASE+'/Meshes/SM_TeslaCollector_Styled'
    if E.does_asset_exist(path):return path
    sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);parts=[]
    roots='/Game/MC_Vehicle_Constructor/Meshes/Sides/Weapons/'
    def piece(asset,pos,scale,rotation=(0,-90,0),material=None):
        a=sub.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*pos),unreal.Rotator(pitch=rotation[0],yaw=rotation[1],roll=rotation[2]),transient=True)
        a.set_actor_label('EMM_StyledPart_TEMP_'+str(len(parts)))
        a.static_mesh_component.set_static_mesh(unreal.load_asset(asset));a.set_actor_scale3d(unreal.Vector(*scale))
        if material:a.static_mesh_component.set_material(0,material)
        parts.append(a)
    # The original set's textured, bolted weapon casing supplies the mechanical base.
    piece(roots+'Weapon_Hive_Lvl3',(-46.81,41.18,0),(.65,.65,.65))
    rp=roots+'Rockets/Rocket_Parts/'
    piece(rp+'Weapon_Rocket_Parts_Rocket_Body_4',(72,0,0),(.9,1.48,.9))
    # Flattened armored capacitor caps carry the original paint, bolt and wear detail.
    for i in range(5):
        radius=32-i*1.5
        piece(rp+'Weapon_Rocket_Parts_Rocket_Nose_8',(27+i*23,0,0),(radius/22.691,.18,radius/22.691))
    glow=mat('M_Collector_Green_Subtle',(.018,.48,.045),.28,.4,2.8)
    for i in range(4):
        piece('/Engine/BasicShapes/Cylinder',(38.5+i*23,0,0),(.365,.365,.065),(90,0,0),glow)
    # A short armored aperture replaces the rocket nose, with green magnetic contacts.
    piece(rp+'Weapon_Rocket_Parts_Rocket_Nose_8',(135,0,0),(.98,.34,.98))
    piece('/Engine/BasicShapes/Sphere',(146,0,0),(.065,.245,.245),(0,0,0),glow)
    for y in [-19,19]:piece('/Engine/BasicShapes/Sphere',(143,y,0),(.075,.075,.12),(0,0,0),glow)
    opts=unreal.MergeStaticMeshActorsOptions(base_package_name=BASE+'/Meshes/TeslaCollector_Styled',new_actor_label='EMM_StyledMerged_TEMP',destroy_source_actors=True,spawn_merged_actor=True)
    settings=opts.mesh_merging_settings
    settings.pivot_point_at_zero=True;settings.merge_materials=False;settings.generate_light_map_uv=False
    settings.merge_physics_data=False;settings.lod_selection_type=unreal.MeshLODSelectionType.SPECIFIC_LOD;settings.specific_lod=0
    opts.mesh_merging_settings=settings
    a=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).merge_static_mesh_actors(parts,opts)
    assert a,'styled collector merge'
    mesh=a.static_mesh_component.static_mesh
    if mesh.get_path_name().split('.')[0]!=path:assert E.rename_asset(mesh.get_path_name().split('.')[0],path)
    save(path);sub.destroy_actor(a)
    record('styled-collector.json',dict(path=path,parts=len(parts),sources=[roots+'Weapon_Hive_Lvl3',rp+'Weapon_Rocket_Parts_Rocket_Body_4',rp+'Weapon_Rocket_Parts_Rocket_Nose_8'],material_policy='Preserved source materials, UVs, normals; merge LOD 0; green contact rings only'))
    return path

def build_preserved_vehicle():
    build_beam()
    mesh=build_styled_collector()
    if not E.does_asset_exist(BP):assert E.duplicate_asset(SOURCE,BP)
    # Remove only the individual rockets. Retain both original textured mounting racks.
    for name in ['Weapon_Rocket_4','Weapon_Rocket_5','Weapon_Rocket_6','Weapon_Rocket_7','Weapon_Rocket_8']:
        if S.component_exists(BP,name):assert S.remove_component(BP,name,True)
    compile_bp()
    for side,localx in [('L',75.842255),('R',-76.157745)]:
        add('CollectorPivot_'+side,'SceneComponent','Top_Turret_2x_Lvl1',(localx,15,79.41),(0,90,0))
        add('Collector_'+side,'StaticMeshComponent','CollectorPivot_'+side)
        prop('Collector_'+side,'StaticMesh',mesh)
        add('LaserMuzzle_'+side,'SceneComponent','CollectorPivot_'+side,(151,0,0))
        add('MiningLaser_'+side,'NiagaraComponent','LaserMuzzle_'+side)
        prop('MiningLaser_'+side,'Asset','None');prop('MiningLaser_'+side,'bAutoActivate','False')
    vars=[('VehicleBodyLengthCm','float','591.6596'),('MiningRangeBodyLengths','float','3.0'),
          ('MiningRangeCm','float','1774.9788'),('MiningActive','bool','False'),
          ('MiningTargetWorld','FVector','(X=0,Y=0,Z=0)'),('UseWorldTarget','bool','False'),
          ('MiningTargetLocal','FVector','(X=1770,Y=0,Z=95)'),('PreviewMining','bool','False')]
    for name,typ,value in vars:
        if not S.get_variable_info(BP,name):assert S.add_variable(BP,name,typ,value)
    # The gameplay pawn replicates visual state; each client creates its own presentation child.
    cdo=unreal.get_default_object(E.load_asset(BP).generated_class())
    cdo.set_editor_property('replicates',False)
    cdo.set_editor_property('replicate_movement',False)
    compile_bp();save(BP)
    # The original green arc template becomes a straight mining laser by disabling curl.
    assert unreal.NiagaraEmitterService.enable_module(FX,'Beam','CurlNoiseForce',False)
    result=unreal.NiagaraService.compile_with_results(FX);assert result.success,str(result)
    assert unreal.NiagaraService.save_system(FX)
    record('asset-paths.json',dict(vehicle=BP,collector=mesh,beam=FX,source=SOURCE,range_cm=1774.9788))
    configure_blueprints([BP])
    return BP

class Graph:
    """Use inspected native pin names; every edge and default is checked."""
    def __init__(self,name):
        self.name=name;self.nodes={};self.pins={};self.index=0
        for node in S.get_nodes_in_graph(BP,name):
            if node.node_type in ['K2Node_FunctionEntry','K2Node_FunctionResult']:
                self.keep('entry' if node.node_type.endswith('Entry') else 'result',node.node_id)
    def keep(self,key,node):
        assert node,(self.name,key)
        pins=S.get_node_pins(BP,self.name,node)
        assert pins,(self.name,key,'no pins')
        self.nodes[key]=node;self.pins[key]=[p.pin_name for p in pins]
        return key
    def call(self,key,cls,func):
        self.index+=1
        return self.keep(key,S.add_function_call_node(BP,self.name,cls,func,(self.index%6)*300,(self.index//6)*260))
    def get(self,key,var):
        self.index+=1
        return self.keep(key,S.add_get_variable_node(BP,self.name,var,(self.index%6)*300,-400-(self.index//6)*180))
    def set(self,key,var):
        self.index+=1
        return self.keep(key,S.add_set_variable_node(BP,self.name,var,(self.index%6)*300,(self.index//6)*260))
    def branch(self,key):return self.keep(key,S.add_branch_node(BP,self.name,700,0))
    def event(self,key,name):return self.keep(key,S.add_event_node(BP,self.name,name,0,self.index*300))
    def edge(self,a,ap,b,bp):
        assert ap in self.pins[a],(self.name,a,ap,self.pins[a])
        assert bp in self.pins[b],(self.name,b,bp,self.pins[b])
        assert S.connect_nodes(BP,self.name,self.nodes[a],ap,self.nodes[b],bp),(self.name,a,ap,b,bp)
    def val(self,key,pin,value):
        assert pin in self.pins[key],(self.name,key,pin,self.pins[key])
        assert S.set_node_pin_value(BP,self.name,self.nodes[key],pin,str(value)),(self.name,key,pin,value)
    def chain(self,*keys):
        for a,b in zip(keys,keys[1:]):self.edge(a,'then',b,'execute')
    def ret(self,key):
        if 'result' in self.nodes:self.edge(key,'then','result','execute')
    def report(self):return {'nodes':self.nodes,'pins':self.pins}

def build_graphs():
    functions=['UpdateCollectorL','UpdateCollectorR','ApplyMiningVisuals','StartMiningAt','StopMining']
    # Re-running this authoring stage replaces only these owned function graphs.
    for fn in functions:
        if S.get_function_info(BP,fn):assert S.delete_function(BP,fn)
        assert S.create_function(BP,fn)
    assert S.add_function_parameter(BP,'ApplyMiningVisuals','ShowMining','bool')
    assert S.add_function_parameter(BP,'StartMiningAt','TargetWorldLocation','FVector')
    compile_bp()
    reports={}
    for side in ['L','R']:
        g=Graph('UpdateCollector'+side)
        g.get('world','MiningTargetWorld');g.get('local','MiningTargetLocal');g.get('worldMode','UseWorldTarget')
        g.call('transform','Actor','GetTransform');g.call('toWorld','KismetMathLibrary','TransformLocation')
        g.edge('transform','ReturnValue','toWorld','T');g.edge('local','MiningTargetLocal','toWorld','Location')
        g.call('target','KismetMathLibrary','SelectVector')
        g.edge('world','MiningTargetWorld','target','A');g.edge('toWorld','ReturnValue','target','B');g.edge('worldMode','UseWorldTarget','target','bPickA')
        g.get('pivot','CollectorPivot_'+side);g.call('pivotPos','SceneComponent','K2_GetComponentLocation')
        g.edge('pivot','CollectorPivot_'+side,'pivotPos','self')
        g.call('aim','KismetMathLibrary','FindLookAtRotation');g.edge('pivotPos','ReturnValue','aim','Start');g.edge('target','ReturnValue','aim','Target')
        g.call('rotate','SceneComponent','K2_SetWorldRotation');g.edge('pivot','CollectorPivot_'+side,'rotate','self');g.edge('aim','ReturnValue','rotate','NewRotation')
        g.get('muzzle','LaserMuzzle_'+side);g.call('start','SceneComponent','K2_GetComponentLocation');g.edge('muzzle','LaserMuzzle_'+side,'start','self')
        g.call('offset','KismetMathLibrary','Subtract_VectorVector');g.edge('target','ReturnValue','offset','A');g.edge('start','ReturnValue','offset','B')
        g.get('range','MiningRangeCm');g.call('clamp','KismetMathLibrary','Vector_ClampSizeMax')
        g.edge('offset','ReturnValue','clamp','A');g.edge('range','MiningRangeCm','clamp','Max')
        g.call('end','KismetMathLibrary','Add_VectorVector');g.edge('start','ReturnValue','end','A');g.edge('clamp','ReturnValue','end','B')
        g.get('beam','MiningLaser_'+side);g.call('endpoint','NiagaraComponent','SetVariablePosition')
        g.edge('beam','MiningLaser_'+side,'endpoint','self');g.edge('end','ReturnValue','endpoint','InValue');g.val('endpoint','InVariableName','User.Beam End')
        g.call('show','SceneComponent','SetVisibility');g.edge('beam','MiningLaser_'+side,'show','self');g.val('show','bNewVisibility','true')
        g.call('activate','ActorComponent','Activate');g.edge('beam','MiningLaser_'+side,'activate','self');g.val('activate','bReset','false')
        g.chain('entry','rotate','endpoint','show','activate');g.ret('activate')
        S.add_comment_around_nodes(BP,g.name,'Aim collector at target; clamp beam to 3 vehicle lengths from its muzzle. User.Beam End is a world-space Niagara Position.',list(g.nodes.values()))
        reports[g.name]=g.report()
    compile_bp()
    g=Graph('ApplyMiningVisuals')
    g.call('scale','Actor','GetActorScale3D');g.call('split','KismetMathLibrary','BreakVector');g.edge('scale','ReturnValue','split','InVec')
    g.call('abs','KismetMathLibrary','Abs');g.edge('split','X','abs','A')
    g.get('body','VehicleBodyLengthCm');g.get('bodies','MiningRangeBodyLengths')
    g.call('size','KismetMathLibrary','Multiply_DoubleDouble');g.edge('body','VehicleBodyLengthCm','size','A');g.edge('abs','ReturnValue','size','B')
    g.call('range','KismetMathLibrary','Multiply_DoubleDouble');g.edge('size','ReturnValue','range','A');g.edge('bodies','MiningRangeBodyLengths','range','B')
    g.set('setRange','MiningRangeCm');g.edge('range','ReturnValue','setRange','MiningRangeCm')
    g.call('dedicated','KismetSystemLibrary','IsDedicatedServer');g.call('client','KismetMathLibrary','Not_PreBool');g.edge('dedicated','ReturnValue','client','A')
    g.call('allowed','KismetMathLibrary','BooleanAND');g.edge('entry','ShowMining','allowed','A');g.edge('client','ReturnValue','allowed','B')
    g.branch('branch');g.edge('allowed','ReturnValue','branch','Condition');g.chain('entry','setRange','branch')
    g.call('left','Self','UpdateCollectorL');g.call('right','Self','UpdateCollectorR');g.edge('branch','then','left','execute');g.chain('left','right');g.ret('right')
    previous=None
    for side in ['L','R']:
        g.get('beam'+side,'MiningLaser_'+side)
        g.call('off'+side,'ActorComponent','Deactivate');g.edge('beam'+side,'MiningLaser_'+side,'off'+side,'self')
        g.call('hide'+side,'SceneComponent','SetVisibility');g.edge('beam'+side,'MiningLaser_'+side,'hide'+side,'self');g.val('hide'+side,'bNewVisibility','false')
        if previous:g.edge(previous,'then','off'+side,'execute')
        else:g.edge('branch','else','off'+side,'execute')
        g.chain('off'+side,'hide'+side);previous='hide'+side
    g.ret(previous);reports[g.name]=g.report()
    compile_bp()
    g=Graph('StartMiningAt');g.set('target','MiningTargetWorld');g.set('mode','UseWorldTarget');g.set('active','MiningActive')
    g.edge('entry','TargetWorldLocation','target','MiningTargetWorld');g.val('mode','UseWorldTarget','true');g.val('active','MiningActive','true')
    g.call('update','Self','ApplyMiningVisuals');g.val('update','ShowMining','true')
    g.call('tick','Actor','SetActorTickEnabled');g.val('tick','bEnabled','true')
    g.chain('entry','target','mode','active','update','tick');g.ret('tick');reports[g.name]=g.report()
    g=Graph('StopMining');g.set('active','MiningActive');g.val('active','MiningActive','false')
    g.call('update','Self','ApplyMiningVisuals');g.val('update','ShowMining','false')
    g.call('tick','Actor','SetActorTickEnabled');g.val('tick','bEnabled','false')
    g.chain('entry','active','update','tick');g.ret('tick');reports[g.name]=g.report()
    compile_bp()
    # Fresh event graph; no original vehicle gameplay graph is changed.
    for node in S.get_nodes_in_graph(BP,'EventGraph'):assert S.delete_node(BP,'EventGraph',node.node_id)
    g=Graph('EventGraph')
    g.event('begin','ReceiveBeginPlay');g.get('active','MiningActive');g.call('initial','Self','ApplyMiningVisuals')
    g.edge('active','MiningActive','initial','ShowMining');g.edge('begin','then','initial','execute')
    g.call('enableTick','Actor','SetActorTickEnabled');g.edge('active','MiningActive','enableTick','bEnabled');g.chain('initial','enableTick')
    g.event('tick','ReceiveTick');g.call('update','Self','ApplyMiningVisuals');g.edge('active','MiningActive','update','ShowMining');g.edge('tick','then','update','execute')
    g.event('end','ReceiveEndPlay');g.call('stop','Self','StopMining');g.edge('end','then','stop','execute');reports[g.name]=g.report()
    g=Graph('UserConstructionScript')
    for n in S.get_nodes_in_graph(BP,g.name):
        if n.node_type!='K2Node_FunctionEntry':assert S.delete_node(BP,g.name,n.node_id)
    g.get('preview','PreviewMining');g.call('update','Self','ApplyMiningVisuals');g.edge('preview','PreviewMining','update','ShowMining');g.chain('entry','update')
    reports[g.name]=g.report()
    for name in ['PreviewMining','MiningTargetLocal','MiningActive']:
        assert S.modify_variable(BP,name,new_category='Mining',set_instance_editable=1)
    assert S.modify_variable(BP,'VehicleBodyLengthCm',new_category='Mining|Dimensions',new_tooltip='Original chassis length before beam bounds. Centimeters at scale 1: 591.6596.')
    assert S.modify_variable(BP,'MiningRangeBodyLengths',new_category='Mining|Dimensions',new_tooltip='Maximum beam length in vehicle body lengths. Requested default: 3.')
    compile_bp();save(BP);record('graph-readback.json',reports)
    configure_blueprints([BP])
    return BP

SHOWCASE=BASE+'/Showcase/LVL_ElectromagneticMiner'

def build_showcase():
    sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    # Refuse to replace a user's dirty map. Our previous inspection actors were transient.
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Current map has unsaved edits'
    record('previous-editor-world.json',dict(world=editor.get_editor_world().get_path_name()))
    world=unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    world.get_world_settings().set_editor_property('default_game_mode',unreal.GameModeBase)
    floor_mat=mat('M_MinerShowcase_Floor',(.11,.14,.15),.12,.8)
    def spawn(cls,label,location,rotation=(0,0,0)):
        a=sub.spawn_actor_from_class(cls,unreal.Vector(*location),unreal.Rotator(pitch=rotation[0],yaw=rotation[1],roll=rotation[2]))
        assert a,label
        a.set_actor_label(label);return a
    floor=spawn(unreal.StaticMeshActor,'Miner_ShowcaseFloor',(650,0,-45))
    floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
    floor.static_mesh_component.set_material(0,floor_mat);floor.set_actor_scale3d(unreal.Vector(90,70,.5))
    car=spawn(unreal.load_asset(BP).generated_class(),'Electromagnetic Mining Vehicle',(0,0,0))
    car.set_editor_property('PreviewMining',False);car.set_editor_property('MiningActive',True)
    car.set_editor_property('MiningTargetLocal',unreal.Vector(1740,0,95))
    rock=spawn(unreal.StaticMeshActor,'Mining Target - Blue Ore',(1820,0,-15))
    rock.static_mesh_component.set_static_mesh(unreal.load_asset('/Game/GuLiStrike/Resources/Ores/Meshes/Blue/SM_Ore_Blue_01_Full'))
    rock.set_actor_scale3d(unreal.Vector(.4,.4,.4))
    sun=spawn(unreal.DirectionalLight,'Showcase_Sun',(0,0,1000),(-42,-38,0))
    sun.light_component.set_editor_property('intensity',4)
    sun.light_component.set_editor_property('light_color',unreal.Color(255,236,210,255))
    sky=spawn(unreal.SkyLight,'Showcase_Sky',(0,0,1500))
    sky.light_component.set_editor_property('intensity',1.5)
    atmosphere=spawn(unreal.SkyAtmosphere,'Showcase_Atmosphere',(0,0,0))
    fill=spawn(unreal.RectLight,'Showcase_Fill',(350,-850,650))
    fill.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(fill.get_actor_location(),unreal.Vector(0,0,180)),False)
    fill.light_component.set_editor_property('intensity',1500)
    fill.light_component.set_editor_property('attenuation_radius',3000)
    fill.light_component.set_editor_property('source_width',650)
    fill.light_component.set_editor_property('source_height',650)
    camera=spawn(unreal.CameraActor,'Miner_ShowcaseCamera',(1030,-1050,690))
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(),unreal.Vector(-20,0,210)),False)
    camera.camera_component.set_field_of_view(43)
    unreal.ViewportService.set_camera_location(camera.get_actor_location());unreal.ViewportService.set_camera_rotation(camera.get_actor_rotation())
    unreal.ViewportService.set_fov(43);unreal.ViewportService.set_game_view(True);unreal.ViewportService.set_realtime(True)
    # Invoke the real graph for the editor preview too.
    car.call_method('ApplyMiningVisuals',(False,))
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,SHOWCASE)
    return dict(map=SHOWCASE,car=car.get_name())
