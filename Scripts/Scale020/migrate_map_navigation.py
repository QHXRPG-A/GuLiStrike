"""Explicit per-map native configuration migration and bake; no SaveAll."""
import json,time
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir())
exec((ROOT/'Scripts/Scale020/capture_editor_baseline.py').read_text(encoding='utf-8').split('def main():')[0])
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
allowed={'LVL_CommanderMassPrototype','LVL_Main','LVL_ShipTest','LVL_ShipWingmanAirCombatPrototype'}
assert world.get_name() in allowed,world.get_path_name()
out=ROOT/('TestResults/Scale020/nav-'+world.get_name()+'-migration.json')
configured=unreal.GuLiNavigationBakeLibrary.migrate_object_scale020(world)
report={'world':world.get_path_name(),'configuration':value(configured),'started':time.time()}
out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
assert configured.success,configured.message
result=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(world,True)
report.update(result=value(result),finished=time.time())
out.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report,ensure_ascii=False))
