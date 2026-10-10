"""Check final source/artifact provenance without compiling or launching UE."""
from pathlib import Path
import datetime
import hashlib
import json
import re
import subprocess

BUILD = Path(__file__).resolve().parent
ROOT = BUILD.parents[2]
manifest = json.loads((BUILD / 'verification-build4.json').read_text(encoding='utf-8'))
source_hashes = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in manifest['source_hashes']}
source_mismatches = [p for p, digest in source_hashes.items() if digest != manifest['source_hashes'][p]]
build_ids = {p: json.loads(Path(p).read_text(encoding='utf-8-sig'))['BuildId'] for p in manifest['build_ids']}
sources = {
    'gs.StateTree.GroupIndex': 'Source/GuLiStrike/Gameplay/Stronghold/GuLiArmyAdvanceSubsystem.cpp',
    'gs.StateTree.ConditionSnapshot': 'Source/GuLiStrike/Commander/Orders/GuLiUnitTaskReadSnapshot.cpp',
    'gs.SceneUI.ParallelGeometry': 'Source/GuLiStrike/Commander/UI/GuLiSceneUIWidget.cpp',
    'gs.Avoidance.ParallelCandidates': 'Source/GuLiStrike/Commander/Mass/GuLiCommanderPredictiveAvoidanceProcessor.cpp',
}
defaults = {}
for cvar, path in sources.items():
    source = (ROOT / path).read_text(encoding='utf-8-sig')
    match = re.search(r'TEXT\("' + re.escape(cvar) + r'"\),\s*([01])\s*,', source)
    assert match, cvar
    defaults[cvar] = int(match[1])
scripts = ['run_muzzle_batch_review.py', 'run_snapshot_parallel_review.py',
           'analyze_snapshot_parallel_review.py', 'render_snapshot_parallel_report.py',
           'author_snapshot_review_entry.py']
for name in scripts:
    path = ROOT / 'Scripts/Performance' / name
    compile(path.read_text(encoding='utf-8'), str(path), 'exec')
diff_check = subprocess.run(['git', 'diff', '--check', '--', 'Source', 'Scripts'], cwd=ROOT, capture_output=True, text=True)
config_changes = subprocess.check_output(['git', 'diff', '--name-only', '--', 'Config'], cwd=ROOT, text=True).splitlines()
result = {'checked_at': datetime.datetime.now().astimezone().isoformat(), 'build_manifest': 'verification-build4.json',
          'source_files_verified': len(source_hashes), 'source_mismatches': source_mismatches,
          'source_matches_built_revision': not source_mismatches, 'source_hashes': source_hashes,
          'module_build_ids': build_ids, 'module_build_ids_match': len(set(build_ids.values())) == 1 and build_ids == manifest['build_ids'],
          'candidate_source_defaults': defaults, 'python_scripts_compiled': scripts,
          'diff_check_exit_code': diff_check.returncode, 'diff_check_stdout': diff_check.stdout,
          'production_config_changes': config_changes}
(BUILD / 'final-static-verification.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
assert not source_mismatches and result['module_build_ids_match'] and diff_check.returncode == 0 and not config_changes
print(json.dumps({k: v for k, v in result.items() if k not in ['source_hashes', 'module_build_ids', 'diff_check_stdout']}, ensure_ascii=False))
