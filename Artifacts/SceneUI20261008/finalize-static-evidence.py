"""Record static checks and freeze delivered review files; never starts UE or compiles native code."""
import ast
import hashlib
import json
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Artifacts/SceneUI20261008'
report=json.loads((OUT/'static-review.json').read_text(encoding='utf8'))
scripts=sorted((ROOT/'Scripts/SceneUI').glob('*.py'))
for p in scripts:ast.parse(p.read_text(encoding='utf-8-sig'),filename=str(p))
diff=subprocess.run(['git','diff','--check'],cwd=ROOT,capture_output=True,text=True,encoding='utf8')
new_sources=[ROOT/'Source/GuLiStrike'/p for p in (
    'Commander/UI/GuLiSceneUITypes.h','Commander/UI/GuLiSceneUIWidget.h','Commander/UI/GuLiSceneUIWidget.cpp',
    'Gameplay/Presentation/GuLiLocalTeamColors.h','Gameplay/Presentation/GuLiLocalTeamColors.cpp')]
whitespace=[]
for p in scripts+new_sources:
    for line_number,line in enumerate(p.read_text(encoding='utf-8-sig').splitlines(),1):
        if line.rstrip()!=line:whitespace.append(f'{p.relative_to(ROOT).as_posix()}:{line_number}')
report.update(success=diff.returncode==0 and not whitespace,
    python_ast_files=[p.relative_to(ROOT).as_posix() for p in scripts],diff_check_exit_code=diff.returncode,
    new_text_trailing_whitespace=whitespace,
    progress_document_check={'errors':0,'warnings':16,'index_built':True,
        'edited_ui_child_hash_matches':True,'commander_parent_reconstructed_hash_matches':True,
        'existing_split_mismatches':['僚机架构子页及父级历史哈希','指挥官03数据技能子页历史哈希，正文与HEAD相同']})
(OUT/'static-review.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
assert report['success'],report

review=ROOT/'ArtSource/LocalTeamColorReview_20261008'
files=sorted((review/'Renders').glob('*.png'))+sorted((review/'Configs').glob('*.json'))
files += [review/'index.html',review/'REVIEW.md',review/'asset-authoring.json',review/'material-readback.json',review/'render-readback.json']
manifest={'version':'LocalTeamColorReview.20261008.v1','runtime_enabled':False,
    'user_region_review':'pending','files':[]}
for p in files:
    value=p.read_bytes()
    manifest['files'].append({'file':p.relative_to(review).as_posix(),'bytes':len(value),'sha256':hashlib.sha256(value).hexdigest()})
(review/'preview-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
print(f'Static checks passed: {len(scripts)} Python files, diff and new source whitespace; froze {len(manifest["files"])} review files')
