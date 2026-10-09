"""Save human/assistant image inspection notes with final board hashes. No image edits."""
import hashlib
import json
from datetime import datetime, timezone, timedelta
from pathlib import Path

OUT = Path(__file__).resolve().parents[1]
manifest = json.loads((OUT / 'review-manifest.json').read_text(encoding='utf8'))
notes = json.loads((OUT / 'Internal/visual_observations.json').read_text(encoding='utf8'))
generation = json.loads((OUT / 'generation-log.json').read_text(encoding='utf8'))
models = []
for model in manifest['models']:
    note = notes[model['id']]
    boards = {}
    for team, board in model['boards'].items():
        path = OUT / board['path']
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual != board['sha256']:
            raise ValueError('Board changed after manifest: ' + board['path'])
        boards[team] = {'path': board['path'], 'sha256': actual, 'views_inspected': ['Hero', 'Front', 'Left', 'Back']}
    for draft in note['revisions']:
        if not (OUT / 'Boards' / draft).is_file():
            raise ValueError('Historical draft missing: ' + draft)
    models.append({'id': model['id'], 'name': model['name'], 'status': 'assistant_reviewed_for_reference_A', 'boards': boards, **note, 'user_A': 'pending', 'Blender_B': 'not_started'})
report = {
    'version': manifest['version'], 'reviewed_at': datetime.now(timezone(timedelta(hours=8))).isoformat(),
    'reviewer': 'assistant', 'method': 'Visual inspection of imagegen tool results and local board/source images; not an automated image-comparison test.',
    'models_reviewed': len(models), 'selected_boards': len(models)*2, 'successful_generation_records': len(generation['records']),
    'draft_boards_retained': sum(len(m['revisions']) for m in models),
    'palette_contract': manifest['palette'],
    'cream_coverage': {'target_visible_model_percent': [20, 30], 'measured': False, 'background_outline_shadow_excluded': True},
    'limits': [
        'Generated orthographic-looking views and painted surfaces are 2D references, with approximate perspective/detail; source mesh and native views remain authoritative.',
        'No pixel-exact geometry, fixed-area raster equality, RGB under shading, or precise surface coverage is claimed.',
        'Assistant review does not grant user A approval or represent real Blender B, UE model changes, gameplay or environment-render verification.',
        'Browser interaction not run because prior tool denied file://; no alternative entry-point workaround.',
    ],
    'models': models, 'A_approval': 'all_pending', 'B_construction': 'not_started', 'UE_resource_update': False,
}
(OUT / 'visual_qa.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
print(json.dumps({key: report[key] for key in ('models_reviewed', 'selected_boards', 'successful_generation_records', 'draft_boards_retained')}, ensure_ascii=False))
