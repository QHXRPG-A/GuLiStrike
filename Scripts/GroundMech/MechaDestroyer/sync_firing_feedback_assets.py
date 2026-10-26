"""Data-import hook for the approved Ground firing assets; no game or visual test."""
import json
from pathlib import Path
import unreal


def sync_firing_feedback_assets():
    root = Path(unreal.Paths.project_dir()).resolve()
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects')))
    entry = next(r for r in rows if r['Name']=='PlayerBullet')
    if '/FiringFeedback_A4/' in entry['ResourcePath']:
        scope={'__name__':'ground_persistent_trail_sync'}
        path=root/'Scripts/GroundMech/MechaDestroyer/author_persistent_trail_assets.py'
        exec(compile(path.read_text(encoding='utf8'),str(path),'exec'),scope)
        result=scope['sync_dimensions']()
        system=unreal.load_asset(entry['ResourcePath'])
        compiled=unreal.NiagaraService.compile_with_results(system.get_path_name())
        assert compiled.success,str(compiled)
        assert unreal.NiagaraService.save_system(system.get_path_name())
        return result
    if '/FiringFeedback_A3/' not in entry['ResourcePath']:
        return {'success':True,'skipped':'A_v3 is not active'}
    scope={'__name__':'ground_fire_sync'}
    path=root/'Scripts/GroundMech/MechaDestroyer/author_firing_feedback_assets.py'
    exec(compile(path.read_text(encoding='utf8'),str(path),'exec'),scope)
    system=unreal.load_asset(entry['ResourcePath'])
    mat=unreal.load_asset('/Game/GuLiStrike/FX/GroundMech/FiringFeedback_A3/M_GroundMech_SingleCurve_A3')
    result=scope['sync_dimensions'](system,mat)
    compiled=unreal.NiagaraService.compile_with_results(system.get_path_name())
    assert compiled.success,str(compiled)
    assert unreal.NiagaraService.save_system(system.get_path_name())
    muzzle=unreal.load_asset(next(r['ResourcePath'] for r in rows if r['Name']=='PlayerMuzzle'))
    scale=next(r['Scale'] for r in json.loads((root/'Data/Json/DT_GuLiStrikeVfx_Effects.json').read_text()) if r['Name']=='PlayerMuzzle')
    assert unreal.NiagaraService.set_parameter(muzzle.get_path_name(),'User.Size',str(scale['X']))
    assert unreal.NiagaraService.save_system(muzzle.get_path_name())
    return result
