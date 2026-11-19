"""Build, save and structurally certify the playable Ground barrage range without starting PIE."""
import json
import math
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/"Artifacts/GroundNoSurvivors"
MAP="/Game/Maps/LVL_CommanderMassPrototype"
PREFIX="NoSurvivors_"
FOLDER="Review/Ground/NoSurvivors"
MATERIALS="/Game/GuLiStrike/GroundMech/NoSurvivors/Range"
START=(2750.,63150.)
PLATFORM=(START[0]+14500.,START[1]+2300.)
TARGETS=[
    ("Near",7350,61200,"50 m"),("Center",12750,63150,"100 m"),("Far",17550,60750,"150 m"),
    ("Inside",12750,61950,"IN"),("Outside",12750,60450,"OUT"),("Friendly",12750,64350,"ALLY"),
    ("LowAir",PLATFORM[0],PLATFORM[1]+1950.,"LOW AIR")]
LIB=unreal.EditorAssetLibrary


def color(hexcolor):
    r,g,b=(int(hexcolor[i:i+2],16) for i in (0,2,4))
    return unreal.Color(r=r,g=g,b=b,a=255)


def material(name,hexcolor):
    path=MATERIALS+"/MI_NoSurvivors_"+name
    obj=unreal.load_asset(path) if LIB.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        path.rsplit("/",1)[1],MATERIALS,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    assert obj
    parent=unreal.load_asset("/Engine/BasicShapes/BasicShapeMaterial")
    ml=unreal.MaterialEditingLibrary
    ml.set_material_instance_parent(obj,parent)
    rgb=color(hexcolor)
    channels=[v/255. for v in [rgb.r,rgb.g,rgb.b]]
    channels=[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in channels]
    ml.set_material_instance_vector_parameter_value(obj,"Color",unreal.LinearColor(*channels,1.))
    actual=ml.get_material_instance_vector_parameter_value(obj,"Color")
    assert all(abs(a-b)<.0001 for a,b in zip((actual.r,actual.g,actual.b),channels)),name
    assert LIB.save_loaded_asset(obj,False)
    return obj


def build():
    asset_report=json.loads((OUT/"asset-readback.json").read_text(encoding="utf8"))
    assert asset_report["success"] and len(asset_report["muzzle_sockets"])==2
    radius=asset_report["radius_cm"]
    horizontal_range=asset_report["horizontal_dive_range_cm"]
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assert not level.is_in_play_in_editor()
    world=editor.get_editor_world()
    assert world.get_path_name().split(".")[0]==MAP
    subsystem=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    all_actors=list(subsystem.get_all_level_actors())
    own={a.get_actor_label():a for a in all_actors if a.get_actor_label().startswith(PREFIX)}
    assert len(own)==sum(a.get_actor_label().startswith(PREFIX) for a in all_actors)
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() or own,"Unrelated unsaved map"
    for suffix in ("Mark","Label"):
        old=own.pop(PREFIX+"Airburst"+suffix,None)
        if old:
            new=PREFIX+"Dive"+suffix
            assert new not in own
            old.modify()
            old.set_actor_label(new)
            own[new]=old
    navy=material("Navy","274E61")
    amber=material("Amber","EE9D58")
    teal=material("Teal","0D9099")
    cube=unreal.load_asset("/Engine/BasicShapes/Cube")
    ring=unreal.load_asset("/Game/GuLiStrike/FX/CombatExplosions/SM_ShockwaveRing")
    ring_extent=ring.get_bounds().box_extent.x
    bp=unreal.EditorAssetLibrary.load_blueprint_class("/Game/GuLiStrike/GroundMech/BP_GroundMech_Light")
    mech=unreal.get_default_object(bp)
    mode=unreal.get_default_object(world.get_world_settings().get_editor_property("default_game_mode"))
    assert mode.get_editor_property("default_ground_pawn_class")==bp and mech.model_id==4001
    nav=next(a for a in all_actors if isinstance(a,unreal.RecastNavMesh) and "CommanderSoldier" in a.get_name())
    added=[]

    def floor(x,y):
        hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,18000),unreal.Vector(x,y,-10000),
            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,list(own.values()),unreal.DrawDebugTrace.NONE)
        assert hit and hit.to_tuple()[0] and hit.to_tuple()[6].z>.65,(x,y)
        return hit.to_tuple()[5].z

    def place(name,cls,xyz,yaw=0):
        label=PREFIX+name
        obj=own.get(label)
        pos=unreal.Vector(*xyz)
        if obj is None:
            obj=subsystem.spawn_actor_from_class(cls,pos)
            assert obj
            obj.set_actor_label(label)
            own[label]=obj
        assert isinstance(obj,cls),label
        obj.modify()
        obj.set_folder_path(FOLDER)
        obj.set_actor_location(pos,False,True)
        obj.set_actor_rotation(unreal.Rotator(yaw=yaw),True)
        return obj

    def shape(name,mesh,pos,scale,mat,block=False):
        obj=place(name,unreal.StaticMeshActor,pos)
        comp=obj.static_mesh_component
        comp.modify()
        comp.set_static_mesh(mesh)
        comp.set_material(0,mat)
        comp.set_mobility(unreal.ComponentMobility.STATIC)
        comp.set_editor_property("can_ever_affect_navigation",False)
        comp.set_collision_profile_name("BlockAll" if block else "NoCollision")
        obj.set_actor_enable_collision(block)
        obj.set_actor_scale3d(unreal.Vector(*scale))
        return obj

    def label(name,value,pos,size):
        obj=place(name,unreal.TextRenderActor,pos,180)
        c=obj.get_component_by_class(unreal.TextRenderComponent)
        c.set_editor_property("can_ever_affect_navigation",False)
        c.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
        c.set_world_size(size)
        c.set_text_render_color(color("FEE4D9"))
        c.set_text(unreal.TextLibrary.conv_string_to_text(value))
        return obj

    x,y=START
    ground=floor(x,y)
    height=mech.capsule_component.get_scaled_capsule_half_height()
    start=place("Start",unreal.TargetPoint,(x,y,ground+height+3))
    start.set_editor_property("is_editor_only_actor",False)
    start.set_editor_property("tags",[unreal.Name("GuLi.NoSurvivors.Start")])
    shape("Takeoff",ring,(x,y,ground+4),(4.5,4.5,1),teal)
    shape("HeightMast",cube,(x+170,y-560,ground+1525),(.32,.32,30.5),navy)
    for meters in (10,20,30):
        shape("Mark"+str(meters),cube,(x+170,y-560,ground+meters*100),(.5,1.8,.2),amber)
    label("Height","30 m",(x+155,y-560,ground+3060),85)
    preview=place("Ranger",unreal.SkeletalMeshActor,(x,y,ground+height+3))
    preview.set_editor_property("is_editor_only_actor",True)
    preview.set_actor_hidden_in_game(True)
    preview.set_actor_enable_collision(False)
    mesh=preview.skeletal_mesh_component
    mesh.set_skeletal_mesh_asset(mech.mesh.skeletal_mesh_asset)
    mesh.set_editor_property("can_ever_affect_navigation",False)
    preview.set_actor_location(start.get_actor_location()+mech.mesh.relative_location,False,True)
    preview.set_actor_rotation(mech.mesh.relative_rotation,True)
    preview.set_actor_scale3d(mech.mesh.relative_scale3d)
    for name,tx,ty,caption in TARGETS:
        z=floor(tx,ty)
        projected=unreal.NavigationSystemV1.project_point_to_navigation(world,unreal.Vector(tx,ty,z),nav,None,unreal.Vector(150,150,1200))
        assert projected and math.hypot(projected.x-tx,projected.y-ty)<150,name
        actor=place(name,unreal.GuLiCommanderDeploymentPoint,(tx,ty,projected.z),180)
        for k,v in dict(team=unreal.GuLiTeam.RED if name=="Friendly" else unreal.GuLiTeam.BLUE,unit_type_id=2 if name=="LowAir" else 1,
                        rows=1,columns=1,spacing_centimeters=600.,start_idle=True,allow_automatic_fire=False,
                        use_in_population_manifest=True,initial_max_health_override=1200.,initial_shield=0.).items():
            actor.set_editor_property(k,v)
        added.append(actor)
        label(name+"Label",caption,(tx+130,ty,z+(3650 if name=="LowAir" else 620)),95 if name in ("Near","Far","Center") else 66)
        if name in ("Near","Center","Far"):
            shape(name+"Mark",ring,(tx,ty,z+6),(2.6,2.6,1),amber)
    cx,cy=x+10000,y
    z=floor(cx,cy)
    shape("BlastLimit",ring,(cx,cy,z+8),(radius/ring_extent,radius/ring_extent,1),teal)
    label("BlastLabel",f"R {radius/100:.2f} m",(cx,cy+radius,z+120),62)
    limit_x=x+horizontal_range
    limit_z=floor(limit_x,y)
    shape("DiveMark",ring,(limit_x,y,limit_z+8),(3.5,3.5,1),teal)
    label("DiveLabel",f"{horizontal_range/100:g} m",(limit_x,y,limit_z+680),115)
    wx,wy=x+6900,y+3750
    wz=floor(wx,wy)
    shape("Wall",cube,(wx,wy,wz+760),(.9,12.,15.2),navy,True)
    shape("WallStripe",cube,(wx-46,wy,wz+1150),(.04,12.,.7),amber)
    label("WallLabel","WALL",(wx-50,wy,wz+1230),95)
    px,py=PLATFORM
    deck_z=floor(px,py)+3200.
    shape("Deck",cube,(px,py,deck_z-50),(18,16,1),teal,True)
    for side in (-1,1):
        pillar_y=py+side*735.
        base_z=floor(px,pillar_y)
        top_z=deck_z-100.
        shape("DeckPillar"+("L" if side<0 else "R"),cube,(px,pillar_y,(base_z+top_z)/2),
              (1,1,(top_z-base_z)/100),navy,True)
    label("DeckLabel","DECK",(px-910,py,deck_z+110),90)
    note=place("Instructions",unreal.Note,(x-200,y+500,ground+200))
    note.set_editor_property("text",f"游骑兵无人生还靶场。按住空格起飞，离地30米按Q悬停，移动鼠标扫射。每发水平飞到{horizontal_range/100:g}米时垂直加速下砸，落地爆炸。可向侧墙、平台下方或台面开火。LOW AIR 是平台旁的敌方重防号。百米靶旁的地面环标示爆炸半径。")
    # Refresh only the new deployment signatures, preserving the certified ore layout.
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,MAP)
    result=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    if not result.success:
        # The preceding range already certified its six ground targets. A new range has none of them.
        air=own[PREFIX+"LowAir"]
        delta=[air] if unreal.GuLiResourceAuthoringLibrary.revalidate_added_deployments([air],False).success else added
        result=unreal.GuLiResourceAuthoringLibrary.revalidate_added_deployments(delta,True)
    assert result.success,str(result.message)
    navigation=unreal.GuLiNavigationBakeLibrary.validate_world_navigation(world)
    if not navigation.success: navigation=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(world,True)
    assert navigation.success,str(navigation.message)
    if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages():
        assert unreal.EditorLoadingAndSavingUtils.save_map(world,MAP)
    assert level.load_level(MAP)
    world=editor.get_editor_world()
    saved={a.get_actor_label():a for a in subsystem.get_all_level_actors() if a.get_actor_label().startswith(PREFIX)}
    assert set(saved)==set(own)
    saved_start=saved[PREFIX+"Start"]
    assert saved_start.actor_has_tag("GuLi.NoSurvivors.Start") and not saved_start.get_editor_property("is_editor_only_actor")
    assert saved[PREFIX+"Ranger"].skeletal_mesh_component.skeletal_mesh_asset.get_path_name()==asset_report["mesh"]
    assert saved[PREFIX+"Wall"].get_actor_enable_collision()
    assert abs(saved[PREFIX+"BlastLimit"].get_actor_scale3d().x*ring_extent-radius)<.01
    marker=saved[PREFIX+"DiveMark"]
    pos,origin=marker.get_actor_location(),saved_start.get_actor_location()
    marker_distance=math.hypot(pos.x-origin.x,pos.y-origin.y)
    assert abs(marker_distance-horizontal_range)<.01 and not marker.get_actor_enable_collision()
    deck=saved[PREFIX+"Deck"]
    assert deck.get_actor_enable_collision()
    def support_z(top):
        hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(px,py,top),unreal.Vector(px,py,-10000),
            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,False,[],unreal.DrawDebugTrace.NONE).to_tuple()
        assert hit[0] and hit[6].z>.65
        return hit[5].z
    deck_top=support_z(deck_z+400)
    under_floor=support_z(deck_z-200)
    assert abs(deck_z-deck_top)<2 and deck_top-under_floor>3000
    units=[]
    for name,tx,ty,caption in TARGETS:
        obj=saved[PREFIX+name]
        assert obj.get_editor_property("start_idle") and not obj.get_editor_property("allow_automatic_fire") and obj.get_editor_property("initial_max_health_override")==1200
        units.append(dict(label=obj.get_actor_label(),unit_type_id=obj.get_editor_property("unit_type_id"),position=obj.get_actor_location().to_tuple(),team=str(obj.get_editor_property("team")),
                          team_id=int(obj.get_editor_property("team").value)))
    manifests={}
    placed={}
    for size in (200,600):
        dest=ROOT/f"Config/Hybrid/RuntimeManifest-{size}.json"
        # The main constructor is shared with the normal PIE room picker.
        result=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(size,str(dest)))
        assert result["success"],result
        check=json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(dest)))
        assert check["success"],check
        saved_manifest=json.loads(dest.read_text(encoding="utf-8-sig"))
        roster=[o for o in saved_manifest["objects"] if o["kind"]==1]
        assert len(roster)==size and int(saved_manifest["numbers"]["adapter/family/GroundPlayer/state_version"])==17
        for target in units:
            tx,ty,_=target["position"]
            matches=[o for o in roster if o["team"]==target["team_id"] and o["definition"]=="unit/"+str(target["unit_type_id"]) and float(o["health"])==1200
                     and math.hypot(float(o["x"])-tx,float(o["y"])-ty)<=151]
            assert len(matches)==1,target["label"]
            placed[target["label"]]=matches[0]
            key="deployment/"+matches[0]["id"].upper()+"/"
            assert int(saved_manifest["numbers"][key+"start_idle"])==1 and int(saved_manifest["numbers"][key+"automatic_fire"])==0
        manifests[size]=check
    path=ROOT/"Config/Hybrid/RuntimeManifest-NoSurvivors.json"
    result=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(600,str(path),True))
    assert result["success"],result
    check=json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(path)))
    assert check["success"],check
    trial=json.loads(path.read_text(encoding="utf-8-sig"))
    assert saved_manifest["objects"]==trial["objects"]
    expected_numbers=dict(saved_manifest["numbers"])
    trial_key="ground_rogue/barrage/trial_level"
    assert int(expected_numbers[trial_key])==0 and int(trial["numbers"][trial_key])==1
    expected_numbers[trial_key]=trial["numbers"][trial_key]
    assert expected_numbers==trial["numbers"]
    assert saved_manifest["map_hash"]==trial["map_hash"]
    production_rules={r["id"]:r for r in saved_manifest["rules"]}
    range_rules={r["id"]:r for r in trial["rules"]}
    assert all(range_rules.get(k)==v for k,v in production_rules.items())
    preview_rules=[r for k,r in range_rules.items() if k not in production_rules]
    assert len(preview_rules)==3 and all("GroundNoSurvivors_Trial" in r["id"] for r in preview_rules)
    weapon_rows=json.loads((ROOT/"Data/Json/DT_GuLiStrikeRogueCards_GroundWeaponEffects.json").read_text(encoding="utf8"))
    profile=next(row for row in weapon_rows if row["Name"]=="GroundNoSurvivors")
    barrage_rules=[r for r in production_rules.values() if "GroundNoSurvivors" in r["id"]]
    assert len(barrage_rules)==3*len(profile["BarrageShotCounts"])
    for level,bonus in enumerate(profile["BarrageDamageBonuses"],1):
        skill="GroundNoSurvivors" if level==1 else f"GroundNoSurvivors_Lv{level}"
        rules=[r for r in barrage_rules if f"{len(skill)}:{skill}" in r["id"].split("/")]
        assert {r["id"].split("/",1)[0] for r in rules}=={"admit","settle","retire"}
        damage=next(r for r in rules if r["id"].startswith("settle/"))
        assert math.isclose(float(damage["damage"]),asset_report["damage"]*(1+float(bonus)),abs_tol=1e-4) and damage["max_targets"]>1
    unit_rows=json.loads((ROOT/"Data/Json/DT_GuLiStrikeCommander_Soldiers.json").read_text(encoding="utf8"))
    model_rows=json.loads((ROOT/"Data/Json/DT_GuLiStrikeModels_Models.json").read_text(encoding="utf8"))
    def blast_sample(name,burst):
        actor=placed[PREFIX+name]
        unit=next(r for r in unit_rows if actor["definition"]=="unit/"+str(r["Id"]))
        model=next(r for r in model_rows if r["Id"]==unit["ModelId"])
        if model["VATDefinition"]:
            bounds=unreal.load_asset(model["VATDefinition"]).get_editor_property("gameplay_bounds")
            center,extent=(bounds.min+bounds.max)*.5,(bounds.max-bounds.min)*.5
        else:
            bounds=unreal.load_asset(model["ResourcePath"]).get_bounds()
            center,extent=bounds.origin,bounds.box_extent
        center*=unit["PresentationScale"]
        yaw=math.radians(float(actor["facing_yaw"]))
        hit=(float(actor["x"])+center.x*math.cos(yaw)-center.y*math.sin(yaw),
             float(actor["y"])+center.x*math.sin(yaw)+center.y*math.cos(yaw),float(actor["z"])+center.z)
        target_radius=max(extent.x,extent.y)*unit["PresentationScale"]
        distance=math.dist(burst,hit)
        assert distance<=radius+target_radius,(name,distance,radius+target_radius)
        return dict(unit=unit["DisplayName"],movement_layer=unit["MovementLayer"],burst_cm=burst,hit_center_cm=hit,
                    center_distance_cm=distance,body_radius_cm=target_radius,field_radius_cm=radius)
    samples={"ground":blast_sample("Center",(cx,cy,z)),"low_air":blast_sample("LowAir",(px,py+740,deck_top))}
    assert samples["ground"]["movement_layer"]==0 and samples["low_air"]["movement_layer"]==1
    manifests["range"]=dict(objects=check["objects"],rules=check["rules"],barrage_rules=[r["id"] for r in barrage_rules])
    result=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    assert result.success and unreal.GuLiNavigationBakeLibrary.validate_world_navigation(world).success
    focus=saved_start.get_actor_location()
    camera=focus+unreal.Vector(-1150,750,550)
    editor.set_level_viewport_camera_info(camera,unreal.MathLibrary.find_look_at_rotation(camera,focus+unreal.Vector(0,0,55)))
    return dict(success=True,map=MAP,entry=saved_start.get_actor_location().to_tuple(),entities=len(saved),targets=units,
                blast_radius_cm=radius,horizontal_marker_cm=marker_distance,marker_collision=marker.get_actor_enable_collision(),
                deck_top_z_cm=deck_top,floor_below_deck_z_cm=under_floor,deck_position_xy_cm=PLATFORM,blast_samples=samples,
                manifests=manifests,resource=str(result.message),pie=False)


try: report=build()
except Exception: report=dict(success=False,error=traceback.format_exc())
(OUT/"scene-readback.json").write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf8")
unreal.MCPythonHelper.submit_result(json.dumps(report,ensure_ascii=False))
