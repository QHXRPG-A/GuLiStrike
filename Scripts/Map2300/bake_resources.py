"""Use the existing canonical resource adapter after migrating the original stable keys."""
import json
from pathlib import Path
import unreal


def main():
    out=Path(unreal.Paths.project_dir()).resolve()/'Artifacts/Map2300/20260923'
    assert (out/'marker-migration-identities.json').is_file()
    result=unreal.GuLiResourceAuthoringLibrary.prepare_canonical_authoring_and_bake(False,False)
    payload={name:getattr(result,name) for name in ('success','message','source_hash','layout_hash','territory_count','cluster_count','node_count','initial_soldier_count','validated_initial_soldier_count')}
    payload['issues']=list(result.issues)
    (out/'resource-bake.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8')
    return payload


unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))
