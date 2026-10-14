"""Export the current ownership contract and refresh the saved review entry."""
import json
import runpy
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir()).resolve()
prefix=globals().get('EVIDENCE_PREFIX','ownership-current')
report=root/f'TestResults/HybridFullAcceptance/Implementation/{prefix}-manifest-readback.json'
runpy.run_path(str(root/'Scripts/Network/refresh_integrated_manifests.py'),init_globals={'REPORT':report})
before=json.loads(unreal.GuLiHybridAuthoringLibrary.read_hybrid_review_entry())
actors=[a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(a,unreal.GuLiHybridNetworkReviewActor)]
if len(actors)!=1: raise RuntimeError('Expected exactly one saved review entry')
entry=actors[0]
result=json.loads(unreal.GuLiHybridAuthoringLibrary.save_hybrid_review_entry(str(root/'Config/Hybrid/RuntimeManifest-600.json'),
    before['room'],before['identity'],600))
if not result.get('success'): raise RuntimeError(json.dumps(result))
after=json.loads(unreal.GuLiHybridAuthoringLibrary.read_hybrid_review_entry())
(root/f'TestResults/HybridFullAcceptance/Implementation/{prefix}-map-readback.json').write_text(json.dumps({'before':before,'save':result,'after':after},ensure_ascii=False,indent=2),encoding='utf-8')
runpy.run_path(str(root/'Scripts/Network/readback_integrated_assets.py'))
