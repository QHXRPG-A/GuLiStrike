import unreal,json
from pathlib import Path
system=unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/NS_MachineGunImpact_Batch')
rows=json.loads(unreal.GuLiCombatEffectAuthoringLibrary.get_impact_compiled_source(system))
p=Path(unreal.Paths.project_dir())/'outputs/performance/20261009-client-three-optimizations/impact-compiled-vm.json'
p.write_text(json.dumps(rows,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'emitters':list(rows)}))
