"""Install and read back the Ground barrage's two tables, flight definition and Q mapping."""
import json
import re
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/"Artifacts/GroundNoSurvivors"
BASE="/Game/GuLiStrike/GroundMech"
FX="/Game/GuLiStrike/FX/GroundMech/NoSurvivors"
PAWN=BASE+"/BP_GroundMech_Light"
CONTEXT=BASE+"/Input/IMC_GroundMech"
ACTION=BASE+"/Input/IA_Ground_NoSurvivors"
PROJECTILE=FX+"/DA_GroundNoSurvivorsMissile"
FIELD=FX+"/DA_GroundNoSurvivorsExplosion"
TABLES=["DT_GuLiStrikeVfx_Effects","DT_GuLiStrikeSpellFields_Fields"]
SKILLS="DT_GuLiStrikeMech_Skills"
LIB=unreal.EditorAssetLibrary


def asset(path,cls,factory=None):
    obj=unreal.load_asset(path) if LIB.does_asset_exist(path) else None
    if obj is None:
        if factory is None:
            factory=unreal.DataAssetFactory()
            factory.set_editor_property("data_asset_class",cls)
        folder,name=path.rsplit("/",1)
        obj=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,folder,cls,factory)
    assert isinstance(obj,cls),path
    obj.modify()
    return obj


def rows(table):
    return {v["Name"]:v for v in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))}


def save_and_reload(assets):
    for obj in assets:
        assert LIB.save_loaded_asset(obj,False),obj.get_path_name()
    ok,error=unreal.EditorLoadingAndSavingUtils.reload_packages(
        [obj.get_outermost() if hasattr(obj,"get_outermost") else obj.get_outer() for obj in assets],
        unreal.ReloadPackagesInteractionMode.ASSUME_NEGATIVE)
    assert ok and not str(error),str(error)


def configure_projectile(field):
    projectile=asset(PROJECTILE,unreal.GuLiProjectileEffectDefinition)
    table=unreal.load_asset("/Game/GuLiStrike/Data/"+SKILLS)
    skill=rows(table)["GroundNoSurvivors"]
    profile=unreal.DataTableRowHandle(data_table=table,row_name="GroundNoSurvivors")
    motion=unreal.GuLiProjectileMotionSettings()
    for k,v in dict(lift_seconds=.26,minimum_lift_height=420.,maximum_lift_height=900.,
                    lateral_offset=2200.,vertical_curve=1550.,longitudinal_curve=700.,convergence_distance=3900.,
                    turn_rate=600.).items(): motion.set_editor_property(k,v)
    for k,v in dict(motion=motion,motion_profile_row=profile,impact_field=field,flight_vfx_id=skill["BulletVfxId"],
                    intercept_enemy_bodies=False,trail_fade_seconds=1.25).items():
        projectile.set_editor_property(k,v)
    return projectile


def install(visuals_only=False, projectile_only=False):
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    calibration=json.loads((OUT/"calibration-optimized.json").read_text(encoding="utf8"))
    assert calibration["success"] and calibration["system"]==FX+"/NS_NoSurvivorsImpact"
    radius=calibration["radius_cm"]
    assets=[]
    for key in ([TABLES[0],SKILLS] if projectile_only else TABLES[:1] if visuals_only else [*TABLES,SKILLS]):
        table=unreal.load_asset("/Game/GuLiStrike/Data/"+key)
        before=rows(table)
        exported=json.loads((ROOT/"data/Json"/(key+".json")).read_text(encoding="utf-8-sig"))
        names=["GroundNoSurvivorsFlight","GroundNoSurvivorsImpact"] if "Vfx" in key else ["GroundNoSurvivors"]
        new={row["Name"]:row for row in exported if row["Name"] in names}
        assert set(new)==set(names)
        merged=dict(before)
        merged.update(new)
        assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table,json.dumps(list(merged.values()),ensure_ascii=False))
        after=rows(table)
        assert all(v==after[k] for k,v in before.items() if k not in new),key
        assets.append(table)
    if projectile_only:
        save_and_reload(assets)
        save_and_reload([configure_projectile(unreal.load_asset(FIELD))])
        return readback(radius)
    if visuals_only:
        save_and_reload(assets)
        return readback(radius)
    field=asset(FIELD,unreal.GuLiSpellFieldDefinition)
    variant=unreal.GuLiEffectVisualVariant()
    for key,value in dict(vfx_id=68,scale_parameter_name="User.Area_Scale",random_yaw=True,maximum_lifetime=5.25).items():
        variant.set_editor_property(key,value)
    for key,value in dict(config_id="GroundNoSurvivors",radius=radius,visual_reference_radius=radius,use_target_hit_centers=True,
                          activation_variants=[variant],dissipation_seconds=5.25).items():
        field.set_editor_property(key,value)
    projectile=configure_projectile(field)
    action=asset(ACTION,unreal.InputAction,unreal.InputAction_Factory())
    action.set_editor_property("value_type",unreal.InputActionValueType.BOOLEAN)
    action.set_editor_property("consume_input",True)
    context=unreal.load_asset(CONTEXT)
    context.modify()
    data=context.get_editor_property("default_key_mappings")
    mappings=[m for m in data.mappings if m.action!=action]
    assert not any(str(m.key.get_editor_property("key_name"))=="Q" for m in mappings),"Another Ground action owns Q"
    key=unreal.Key()
    key.set_editor_property("key_name","Q")
    mapping=unreal.EnhancedActionKeyMapping()
    mapping.set_editor_property("key",key)
    mapping.set_editor_property("action",action)
    data.set_editor_property("mappings",[*mappings,mapping])
    context.set_editor_property("default_key_mappings",data)
    bp=unreal.load_asset(PAWN)
    bp.modify()
    defaults=unreal.get_default_object(bp.generated_class())
    defaults.modify()
    defaults.set_editor_property("barrage_action",action)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    assets.extend([field,projectile,action,context,bp])
    save_and_reload(assets)
    return readback(radius)


def readback(radius):
    p=unreal.load_asset(PROJECTILE)
    f=unreal.load_asset(FIELD)
    defaults=unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(PAWN))
    tablefx=rows(unreal.load_asset("/Game/GuLiStrike/Data/"+TABLES[0]))
    fieldrow=rows(unreal.load_asset("/Game/GuLiStrike/Data/"+TABLES[1]))["GroundNoSurvivors"]
    assert fieldrow["Damage"]==30 and fieldrow["RadiusCentimeters"]==radius
    assert f.use_target_hit_centers and f.visual_reference_radius==radius and f.activation_variants[0].vfx_id==68 and str(f.activation_variants[0].scale_parameter_name)=="User.Area_Scale"
    assert p.flight_vfx_id==67 and not p.intercept_enemy_bodies and p.impact_field==f
    skill_table=unreal.load_asset("/Game/GuLiStrike/Data/"+SKILLS)
    skill=rows(skill_table)["GroundNoSurvivors"]
    source_skill=next(r for r in json.loads((ROOT/"data/Json"/(SKILLS+".json")).read_text(encoding="utf8")) if r["Name"]=="GroundNoSurvivors")
    assert p.motion_profile_row.data_table==skill_table and str(p.motion_profile_row.row_name)==skill["Name"]
    motion=p.resolve_motion_settings()
    strike=p.get_ground_strike_flight()
    assert strike.horizontal_range==skill["HorizontalRangeCentimeters"]==source_skill["HorizontalRangeCentimeters"]
    assert strike.dive_acceleration==skill["DiveAccelerationCmPerSecondSquared"]==source_skill["DiveAccelerationCmPerSecondSquared"]
    assert motion.maximum_lifetime==skill["ProjectileLifetime"] and motion.speed==skill["ProjectileSpeed"] and motion.sweep_radius==skill["SweepRadius"]
    assert defaults.model_id==4001 and defaults.get_editor_property("barrage_action")==unreal.load_asset(ACTION)
    assert sum(m.action==unreal.load_asset(ACTION) and str(m.key.get_editor_property("key_name"))=="Q"
               for m in unreal.load_asset(CONTEXT).get_editor_property("default_key_mappings").get_editor_property("mappings"))==1
    effects={}
    for name,identifier,path in (
            ("GroundNoSurvivorsFlight",67,FX+"/NS_NoSurvivorsFlight"),
            ("GroundNoSurvivorsImpact",68,FX+"/NS_NoSurvivorsImpact")):
        row=tablefx[name]
        resource=unreal.load_asset(path)
        assert isinstance(resource,unreal.NiagaraSystem) and resource.get_path_name() in str(row["ResourcePath"])
        assert row["Id"]==identifier and not row["PersistentTrailVfxId"]
        scale=row["Scale"]
        axes=list(scale.values()) if isinstance(scale,dict) else re.findall(r"[XYZ]\s*=\s*([-+0-9.eE]+)",scale)
        export_fx=json.loads((ROOT/"data/Json"/(TABLES[0]+".json")).read_text(encoding="utf8"))
        expected=next(r for r in export_fx if r["Name"]==name)
        assert len(axes)==3 and list(map(float,axes))==[expected["Scale"][axis] for axis in "XYZ"],scale
        effects[name]=dict(id=identifier,system=resource.get_path_name(),scale=scale,extra_trail=row["PersistentTrailVfxId"])
        if name=="GroundNoSurvivorsFlight":
            for column in ("TrailLengthCm","TrailFadeSeconds"):
                assert abs(row[column]-expected[column])<.0001
                effects[name][column]=row[column]
    assert tablefx["GroundNoSurvivorsImpact"]["RadiusAtUnitScaleCm"]==radius
    return dict(success=True,projectile=PROJECTILE,field=FIELD,radius_cm=radius,damage=30,
                effects=effects,impact_scale_parameter="User.Area_Scale",use_hit_centers=f.use_target_hit_centers,
                skill_table=SKILLS,skill_row=skill["Name"],horizontal_dive_range_cm=strike.horizontal_range,
                dive_acceleration_cm_s2=strike.dive_acceleration,intercept_enemy_bodies=p.intercept_enemy_bodies,
                maximum_cruise_seconds=motion.maximum_lifetime,motion=motion.export_text(),q=ACTION,model_id=4001,mesh=defaults.mesh.skeletal_mesh_asset.get_path_name(),
                muzzle_sockets=[s for s in ["Jet_Outlet_L","Jet_Outlet_R"] if defaults.mesh.does_socket_exist(s)])


def run(visuals_only=False, projectile_only=False):
    try: report=install(visuals_only, projectile_only)
    except Exception: report=dict(success=False,error=traceback.format_exc())
    if report["success"]:
        (OUT/"asset-readback.json").write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf8")
    unreal.MCPythonHelper.submit_result(json.dumps(report))


if __name__ == "__main__":
    run()
