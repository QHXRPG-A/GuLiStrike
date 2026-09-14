"""Deploy the approved shared tables/miner tuning after native classes are rebuilt."""
import json
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir()).resolve()
E=unreal.EditorAssetLibrary
old="/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_SpellFields"
new="/Game/GuLiStrike/Data/DT_GuLiStrikeSpellFields_Fields"
if E.does_asset_exist(old) and not E.does_asset_exist(new):
    assert E.rename_asset(old,new), "Global SpellFields migration failed"
exec(compile((ROOT/"Scripts/import_data_to_engine.py").read_text(encoding="utf-8"),"import_data_to_engine.py","exec"))
config=unreal.load_asset("/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy")
config.set_editor_property("mining_vehicle_unit_type_id",3)
config.set_editor_property("factory_maneuver_speed_centimeters_per_second",1500.0)
assert E.save_loaded_asset(config)
field=unreal.load_asset("/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundExplosion")
field.set_editor_property("config_id","WingmanGroundMissile")
assert E.save_loaded_asset(field)
fx="/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Green"
assert unreal.NiagaraService.set_parameter(fx,"Constants.Beam.BeamWidth.Beam Width","25.0")
compiled=unreal.NiagaraService.compile_with_results(fx)
assert compiled.success, str(compiled)
assert unreal.NiagaraService.save_system(fx)
settings=unreal.NiagaraService.get_all_editable_settings(fx)
width=next(float(p.current_value) for p in settings.rapid_iteration_parameters if p.setting_path=="Constants.Beam.BeamWidth.Beam Width")
report={"success":width==25.0,"laser_width_before_cm":5.0,"laser_width_after_cm":width,"laser_width_multiplier":5,
        "factory_speed_cm_s":config.get_editor_property("factory_maneuver_speed_centimeters_per_second"),
        "miner_unit_type_id":config.get_editor_property("mining_vehicle_unit_type_id"),"wingman_field":str(field.get_editor_property("config_id"))}
(ROOT/"outputs/unit-data-mining/asset-deployment.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
unreal.MCPythonHelper.submit_result(json.dumps(report))
