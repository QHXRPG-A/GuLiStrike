"""Read existing captures only; keep full CSV and 30 s trace scopes distinct."""
import csv
import hashlib
import json
import math
import sys
from collections import defaultdict
from pathlib import Path
from statistics import median

OUT = Path(__file__).resolve().parent
PROJECT = OUT.parents[1]
BASELINE = OUT.parent / 'CommanderClientCpu-Retest-20260916'
sys.path.insert(0, str(PROJECT / 'Scripts'))
from analyze_commander_move_stress import read_engine, stats

COUNTERS = (
    'RHI/DrawCalls', 'RHI/PrimitivesDrawn', 'GPUSceneInstanceCount',
    'GPUMem/LocalUsedMB', 'GPUMem/LocalBudgetMB', 'GPUMem/SystemUsedMB',
    'LightCount/All', 'RenderTargetPoolUsed', 'SceneCulling/NumStaticInstances',
    'SceneCulling/NumDynamicInstances', 'PSO/PSOMisses', 'PSO/PSOComputeMisses',
)


def resources(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        rows = list(csv.reader(stream))
    header = next(r for r in reversed(rows) if r and r[0] == 'EVENTS')
    frame_index = header.index('FrameTime')
    samples = []
    for row in rows:
        try:
            frame_ms = float(row[frame_index])
        except (ValueError, IndexError):
            continue
        if math.isfinite(frame_ms):
            samples.append(row)
    return {key: stats([float(row[header.index(key)]) for row in samples])
            for key in COUNTERS if key in header}


items = []
groups = defaultdict(list)
comparison = json.loads((BASELINE / 'comparison.json').read_text(encoding='utf-8-sig'))
for previous in comparison:
    path = BASELINE / previous['run'] / previous['role'] / 'engine.csv'
    engine = read_engine(path)
    metrics = {k: v for k, v in engine.items()
               if k.startswith(('GPU/', 'Exclusive/RenderThread/', 'RenderThreadIdle/'))
               or k in ('FrameTime', 'GameThreadTime', 'RenderThreadTime', 'RHIThreadTime', 'GPUTime')}
    for key in ('GameThreadTime', 'RenderThreadTime', 'GPUTime'):
        assert metrics[key] == previous['engine_full_capture'][key], (path, key)
    metrics.update(resources(path))
    item = {'run': previous['run'], 'clients': previous['clients'], 'role': previous['role'],
            'source': str(path), 'scope': 'full approximately 40 second engine CSV capture',
            'metrics': metrics}
    items.append(item)
    groups[f"{previous['clients']}c-{previous['role']}"] .append(item)

summary = {}
for group, runs in groups.items():
    keys = sorted(set().union(*(r['metrics'].keys() for r in runs)))
    summary[group] = {key: {
        'rounds_present': sum(key in r['metrics'] for r in runs),
        'median_of_means': median(r['metrics'][key]['mean'] for r in runs if key in r['metrics']),
        'median_of_p95': median(r['metrics'][key]['p95'] for r in runs if key in r['metrics']),
        'min_mean': min(r['metrics'][key]['mean'] for r in runs if key in r['metrics']),
        'max_mean': max(r['metrics'][key]['mean'] for r in runs if key in r['metrics']),
    } for key in keys}

checks = []
for entry in json.loads((BASELINE / 'source-manifest.json').read_text(encoding='utf-8-sig')):
    current = hashlib.sha256(Path(entry['path']).read_bytes()).hexdigest()
    checks.append({**entry, 'current_sha256': current, 'unchanged': current == entry['sha256']})

result = {'scope': 'nine existing full engine CSV captures; three per group',
          'aggregation': 'median of each run mean, not pooled frame median',
          'caveats': ['GPU scopes are not an additive critical path budget.',
                      'Async compute and unattributed GPU work need trace inspection.',
                      'Drawn primitives count includes passes; it is not unique mesh triangles.',
                      'GPUSceneInstanceCount includes the scene and allocated instance pools.'],
          'summary': summary, 'runs': items, 'source_checks': checks}
(OUT / 'gpu-baseline.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
rows = []
for key in ('GPUTime', 'GPU/TemporalSuperResolution', 'GPU/ShadowDepths', 'GPU/BasePass',
            'GPU/Prepass', 'GPU/LumenReflections', 'GPU/VolumetricFog', 'GPU/Unaccounted',
            'Exclusive/RenderThread/EventWait/Visibility', 'RHI/DrawCalls', 'RHI/PrimitivesDrawn'):
    rows.append({'metric': key, **{g: round(data[key]['median_of_means'], 4) for g, data in summary.items()}})
print(json.dumps({'metrics': rows, 'source_files_unchanged': all(c['unchanged'] for c in checks)}, indent=2))
