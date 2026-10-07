"""Persist final documentation checks and current review handoff state."""
import json,subprocess,sys,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];ART=ROOT/'ArtSource/CommanderLOD_20261005';OUT=ART/'Reports'
check=subprocess.run([sys.executable,'-X','utf8',str(ROOT/'.agents/skills/gulistrike-progress/scripts/progress_docs.py'),'check','--json'],cwd=ROOT,capture_output=True,text=True,encoding='utf8',check=True)
report=json.loads(check.stdout);assert not report['errors']
(OUT/'progress_check.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
manifest=json.loads((ART/'review_manifest.json').read_text(encoding='utf8'))
approval=json.loads((ART/'approval_B.json').read_text(encoding='utf8')) if (ART/'approval_B.json').exists() else None
delivery=json.loads((ART/'formal_delivery.json').read_text(encoding='utf8')) if (ART/'formal_delivery.json').exists() else None
approved=bool(approval and approval['approval_B']=='approved')
switched=bool(delivery and delivery['formal_switched'])
if approved:assert approval['content_sha256']==manifest['content_sha256']
for unit in manifest['units']:
 for file in unit['artifacts']:
  path=ART/file['path'];assert path.suffix.lower() not in {'.uasset','.umap','.uexp','.ubulk','.pak'}
  assert hashlib.sha256(path.read_bytes()).hexdigest()==file['sha256']
scene=json.loads((OUT/'review_scene.json').read_text(encoding='utf8'));assert scene['saved']
handoff=dict(success=True,version=manifest['version'],content_sha256=manifest['content_sha256'],
 approval_B='approved' if approved else 'pending',formal_switched=switched,review_url='http://127.0.0.1:8709/Review/index.html',
 map=scene['map'],review_groups=len(scene['groups']),blender_open='ArtSource/CommanderLOD_20261005/BiZhiMao/BiZhiMao_3Tier.blend',
 screenshot='Review/review_screenshot.jpg',documentation_errors=0,documentation_warnings=len(report['warnings']),
 native_compile='passed' if switched else 'not_run',pie='not_run',network='not_run',fps='not_run',
 remaining=['Actual-version B decision','WM01 selected source far silhouette/contour deviation',
 'Vehicle far budget +10 triangles each; BiZhiMao retained near/far excess',
 'Post-approval atomic group switch and separately permitted native configuration build'])
if switched:
 handoff.update(formal_delivery='formal_delivery.json',formal_groups=6,total_assets=delivery['total_assets'],editor_reopened=True,
  remaining=['Manual construction/movement/network gameplay review','FPS measurement','Recorded source silhouette and budget differences remain documented; appearance B already approved.'])
(OUT/'handoff.json').write_text(json.dumps(handoff,ensure_ascii=False,indent=2),encoding='utf8')
print('HANDOFF_READY',manifest['version'],manifest['content_sha256'][:12],'doc errors 0; warnings',len(report['warnings']))
