"""Read back this reference delivery, sources and local links. Does not edit images or UE assets."""
import ast
import hashlib
import json
import re
import struct
import subprocess
from datetime import datetime, timezone, timedelta
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit

OUT = Path(__file__).resolve().parents[1]
ROOT = OUT.parents[1]
OLD = OUT.parent / 'LocalTeamColorReference_A_v2_20261008'
errors = []

def read(path):
    return json.loads(path.read_text(encoding='utf8'))

def sha(path):
    if path.suffix.lower() in {'.uasset', '.umap', '.uexp', '.ubulk', '.pak'}:
        raise ValueError('UE binary asset reading is prohibited')
    return hashlib.sha256(path.read_bytes()).hexdigest()

def require(condition, message):
    if not condition:
        errors.append(message)

def png_info(path):
    raw = path.read_bytes()
    require(raw[:8] == b'\x89PNG\r\n\x1a\n', f'Invalid PNG: {path.name}')
    return {'width': struct.unpack('>I', raw[16:20])[0], 'height': struct.unpack('>I', raw[20:24])[0], 'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}

manifest = read(OUT / 'review-manifest.json')
models = manifest['models']
require(len(models) == 14, 'Model count must be 14')
require(manifest['board_count'] == 28, 'Board count must be 28')
require(manifest['approved_models'] == [] and not manifest['runtime_enabled'], 'Reference approvals must remain pending')
base_keys = ['fixed_armor', 'fixed_secondary', 'mechanisms', 'blue', 'blue_secondary', 'red', 'red_secondary']
allowed = {c for card in manifest['palette_cards'] for c in card}
require(all(manifest['palette'][k]['hex'] in allowed for k in base_keys), 'Base color outside user palette')
boards, sources, dynamic_links = [], [], []
for m in models:
    config_path = OUT / 'Configs' / f"{m['id']}.json"
    require(read(config_path) == m, f"Configuration mismatch: {m['id']}")
    require(m['approval_A']['status'] == 'pending' and m['construction_B']['status'] == 'not_started', f"Approval incorrectly granted: {m['id']}")
    dynamic_links.append(config_path)
    for team in ('blue', 'red'):
        p = OUT / m['boards'][team]['path']
        info = png_info(p)
        require((info['width'], info['height']) == (1536, 1024), f'Board dimensions: {p.name}')
        require(info['sha256'] == m['boards'][team]['sha256'], f'Board hash mismatch: {p.name}')
        boards.append({'model': m['id'], 'team': team, 'path': p.relative_to(OUT).as_posix(), **info})
        dynamic_links.extend([p, ROOT / m['previous_boards'][team]])
    for src in m['source_references']:
        p = ROOT / src['path']
        require(sha(p) == src['sha256'], f"Source hash mismatch: {src['path']}")
        sources.append(src)
        dynamic_links.append(p)
shield = next(m for m in models if m['id'] == 'ShieldGenerator')
require(shield.get('function_hex') is None, 'Legacy shield fixed cyan field remains')
require(shield['paint_contract']['team_light'] == {'count': 3, 'blue': '#6AA4BE', 'red': '#A34053', 'fixed_cyan': False}, 'Shield team lights contract mismatch')

palette_records = []
for rec in read(OUT / 'palette-source-map.json')['records']:
    p = OUT / rec['relative_destination']
    source = Path(rec['original_path'])
    old_copy = OLD / rec['relative_destination']
    same_old = sha(p) == sha(old_copy) == rec['sha256']
    same_original = sha(p) == sha(source) if source.exists() else None
    require(same_old and same_original is not False, f'Palette changed: {p.name}')
    palette_records.append({'file': rec['relative_destination'], 'sha256': sha(p), 'matches_A_v2': same_old, 'matches_original': same_original})
    dynamic_links.append(p)
feedback = manifest['user_feedback_image']
require(sha(OUT / feedback['path']) == feedback['sha256'], 'Feedback image changed')

generation = []
records = read(OUT / 'generation-log.json')['records']
require(len(records) >= 28, 'Missing successful generation records')
for rec in records:
    destination = OUT / rec['selected_destination']
    generated = Path(rec['source_output'])
    prompt = OUT / rec['prompt']
    require(prompt.exists() and prompt.stat().st_size > 0, f"Prompt missing: {rec['request']}")
    require(generated.exists() and sha(destination) == sha(generated), f"Generated output changed: {rec['request']}")
    require(1 <= len(rec['reference_inputs']) <= 5, f"Reference count: {rec['request']}")
    inputs = []
    for name in rec['reference_inputs']:
        p = Path(name)
        require(p.exists(), f'Input missing: {name}')
        inputs.append({'path': name, 'sha256': sha(p)})
    generation.append({'request': rec['request'], 'output_sha256': sha(destination), 'prompt_sha256': sha(prompt), 'reference_inputs': inputs})
    dynamic_links.extend([destination, prompt])

class PageParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
        self.links = []
    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if 'id' in attrs:
            self.ids.add(attrs['id'])
        for key in ('src', 'href'):
            if attrs.get(key):
                self.links.append(attrs[key])

html_results = []
js = (OUT / 'gallery.js').read_text(encoding='utf8')
required_ids = set(re.findall(r'getElementById\("([^"]+)"\)', js))
for name in ('index.html', 'overview.html'):
    p = OUT / name
    page = PageParser()
    page.feed(p.read_text(encoding='utf8'))
    missing_ids = sorted(required_ids - page.ids)
    require(not missing_ids, f'Missing HTML ids: {name}: {missing_ids}')
    for link in page.links:
        parts = urlsplit(link)
        if not parts.scheme and parts.path:
            dynamic_links.append((p.parent / unquote(parts.path)).resolve())
    html_results.append({'page': name, 'script_ids_match': not missing_ids, 'static_links': len(page.links)})
for path in dynamic_links:
    require(path.exists(), f'Missing local link: {path}')

syntax = []
for path in sorted((OUT / 'Internal').glob('*.py')):
    ast.parse(path.read_text(encoding='utf8'), filename=str(path))
    syntax.append({'file': path.relative_to(OUT).as_posix(), 'syntax': 'passed'})
for name in ('gallery.js', 'gallery-data.js'):
    result = subprocess.run(['node', '--check', str(OUT / name)], capture_output=True, text=True, encoding='utf8')
    require(result.returncode == 0, f'JavaScript syntax: {name}: {result.stderr}')
    syntax.append({'file': name, 'syntax': 'passed' if result.returncode == 0 else 'failed'})

old_frozen = read(OLD / 'frozen_delivery_manifest.json')
old_mismatches = []
for entry in old_frozen['files']:
    path = (OLD / entry['path']).resolve()
    require(path.is_relative_to(OLD.resolve()), 'Historical manifest path escapes its root')
    if not path.exists() or sha(path) != entry['sha256']:
        old_mismatches.append(entry['path'])
require(not old_mismatches, f'A_v2 modified: {old_mismatches}')
native_copies = []
for path in sorted((OUT / 'Sources').glob('*.png')):
    equal = sha(path) == sha(OLD / 'Sources' / path.name)
    require(equal, f'Native source copy changed: {path.name}')
    native_copies.append({'file': 'Sources/' + path.name, 'sha256': sha(path), 'matches_A_v2': equal})
require(len(native_copies) == 22, 'Expected 22 reused UE native views')
captures = []
for name in ('source-capture.json', 'source-capture-added-buildings.json', 'source-capture-turret-front.json'):
    unchanged = sha(OUT / name) == sha(OLD / name)
    require(unchanged, f'Historical capture report changed: {name}')
    captures.append({'file': name, 'matches_A_v2': unchanged, 'reused_history': True})

report = {
    'version': manifest['version'], 'checked_at': datetime.now(timezone(timedelta(hours=8))).isoformat(),
    'status': 'passed' if not errors else 'failed', 'errors': errors,
    'models': len(models), 'canonical_boards': len(boards), 'configs': len(models),
    'board_readback': boards, 'source_hash_records': sources, 'palette_readback': palette_records,
    'generation_records': generation, 'native_source_copies': native_copies, 'historical_capture_reports': captures,
    'static_local_links_checked': len(dynamic_links), 'html_structure': html_results, 'syntax': syntax,
    'A_v2_preservation': {'files_checked': len(old_frozen['files']), 'mismatches': old_mismatches},
    'visual_qa': 'visual_qa.json; assistant inspection, not user approval',
    'cream_coverage': {'design_target_percent': [20, 30], 'measured': False, 'basis': 'visible model surfaces; excludes background/shadows'},
    'browser_interaction': {'status': 'not_run', 'reason': 'Prior browser tool rejected file://. Restriction retained; no alternative protocol or entry-point workaround attempted.'},
    'approval_A': 'all_pending', 'Blender_B': 'not_started', 'formal_ue_resource_update': False,
    'UE_editor_used_this_revision': False, 'native_compile': False, 'gameplay_started': False,
    'automated_tests_added_or_run': False, 'image_editing_by_script': False,
}
(OUT / 'delivery-check.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
print(json.dumps({k: report[k] for k in ('status', 'errors', 'models', 'canonical_boards', 'configs', 'static_local_links_checked', 'A_v2_preservation')}, ensure_ascii=False))
raise SystemExit(1 if errors else 0)
