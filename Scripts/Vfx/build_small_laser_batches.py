"""Bind the owned laser pool copy to the native capacity set before activation."""
import json, traceback
from pathlib import Path
import unreal

OUT=Path(unreal.Paths.project_dir())/'outputs/performance/20261009-all-optimizations'
source='/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool'
target=source+'_SmallBatches'
report={'success':False,'source':source,'target':target,'runtime_capacity':256,'rollback_capacity':1024}
try:
    assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
    if not unreal.EditorAssetLibrary.does_asset_exist(target):
        assert unreal.EditorAssetLibrary.duplicate_asset(source,target)
    asset=unreal.load_asset(target)
    error=unreal.GuLiCombatEffectAuthoringLibrary.bind_laser_pool_capacity(asset)
    assert error is not None,error
    compiled=unreal.NiagaraService.compile_with_results(target)
    assert compiled.success and compiled.error_count==0,str(compiled)
    assert unreal.NiagaraService.save_system(target)
    report.update(success=True,compile=str(compiled),parameter=str(unreal.NiagaraService.get_parameter(target,'User.LaserSlotCount')),
        summary=str(unreal.NiagaraService.summarize(target)),
        contract='Global slot indices are divided by the World-frozen native block size; particle ExecIndex and arrays use the same capacity. Default 1024 remains a local rollback.')
except Exception:
    report['error']=traceback.format_exc()
(OUT/'small-laser-batch-build.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report))
