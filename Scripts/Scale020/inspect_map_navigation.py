import json
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir())
exec((ROOT/'Scripts/Scale020/capture_editor_baseline.py').read_text(encoding='utf-8').split('def main():')[0])
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
rows=[]
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    cls=actor.get_class().get_name()
    if any(x in cls for x in ['RecastNavMesh','FlightNavigationVolume','DeploymentPoint']):
        rows.append({'path':actor.get_path_name(),'class':cls,'transform':value(actor.get_actor_transform()),'properties':properties(actor)})
out=ROOT/('TestResults/Scale020/nav-'+world.get_name()+'-baseline.json')
if not out.exists():
    out.write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'world':world.get_path_name(),'actors':[(r['path'],r['class']) for r in rows]}))
