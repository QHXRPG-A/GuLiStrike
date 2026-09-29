"""Read-only bitmap analysis and provenance. Never generates or edits image pixels."""
from pathlib import Path
import hashlib
import json
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/UI/WM01MissileCards/Production_v4'
requests = json.loads((OUT/'generation-requests.json').read_text(encoding='utf-8'))
requests += json.loads((OUT/'remaining-requests.json').read_text(encoding='utf-8'))
sources = json.loads((OUT/'output-sources.json').read_text(encoding='utf-8'))
def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()
items = []
for request in requests:
    card, layer = request['card'], request['layer']
    source, = [s for s in sources if s['card'] == card and s['layer'] == layer]
    path = OUT/f'{card}_{layer}.png'
    # These text files contain the exact payload string; their hashes are separate
    # from inherited review templates and are not claimed to be earlier prompts.
    prompt = OUT/'Prompts'/f'{card}_{layer}.txt'
    prompt.write_bytes(request['prompt'].encode('utf-8'))
    with Image.open(path) as im:
        assert im.size == (1024, 1536)
        alpha = np.asarray(im.getchannel('A')) if 'A' in im.getbands() else None
        info = {'size': list(im.size), 'mode': im.mode, 'alpha_range': None if alpha is None else [int(alpha.min()), int(alpha.max())],
                'transparent_fraction': 0 if alpha is None else float(np.mean(alpha == 0))}
    items.append({'card': card, 'layer': layer, 'output': str(path), 'raw_source': source['source'],
                  'output_sha256': sha(path), 'prompt': str(prompt), 'prompt_sha256': sha(prompt),
                  'inputs': [{'path': p, 'sha256': sha(p)} for p in request['referenced_image_paths']],
                  'transparent_background': request['transparent_background'], **info})

# Small source-space landmark windows, selected from visible opaque structures.
# The search measures translation only; it never writes a transformed image.
landmarks = [
    ('MissilePod', 2, 'scanner joint', (310, 85, 390, 162)),
    ('MissilePod', 2, 'welding arm', (32, 910, 102, 974)),
    ('MissilePod', 2, 'gripped panel', (784, 586, 856, 674)),
    ('MissilePod', 3, 'tube front', (238, 447, 312, 508)),
    ('MissilePod', 3, 'assembly fixture', (411, 1095, 508, 1145)),
    ('RainSalvo', 2, 'blue missile', (876, 694, 945, 754)),
    ('RainSalvo', 2, 'green +1', (666, 772, 736, 821)),
    ('RainSalvo', 3, 'left pod tube', (209, 845, 284, 904)),
    ('RainSalvo', 3, 'right pod rim', (783, 1095, 840, 1136)),
    ('RainSalvo', 1, 'top free missile', (410, 36, 448, 64)),
]
checks = []
for card, layer, name, (x0,y0,x1,y1) in landmarks:
    ref = OUT.parent/('Review_v2' if card == 'MissilePod' else 'Review_v3')/(card+'.png')
    a = np.asarray(Image.open(ref).convert('RGB'), dtype=np.float32)
    b = np.asarray(Image.open(OUT/f'{card}_{layer}.png').convert('RGB'), dtype=np.float32)
    target = a[y0:y1,x0:x1].reshape(-1)
    target -= target.mean()
    denom = np.linalg.norm(target)
    best = (-2., 0, 0)
    for dy in range(-12, 13):
        for dx in range(-12, 13):
            sample = b[y0+dy:y1+dy,x0+dx:x1+dx].reshape(-1).copy()
            sample -= sample.mean()
            score = float(np.dot(target, sample) / max(denom*np.linalg.norm(sample), 1e-8))
            if score > best[0]: best = (score, dx, dy)
    checks.append({'card':card, 'layer':layer, 'landmark':name, 'reference_window':[x0,y0,x1,y1],
                   'best_translation_pixels':[best[1],best[2]],'rgb_correlation':round(best[0],5)})
report = {'generator':'built-in image_gen', 'canvas':[1024,1536], 'outputs':items,
          'landmark_analysis':checks, 'analysis_scope':'Translation search in selected windows, not pixel-perfect reconstruction or full visual approval.',
          'user_accepted_details':'2026-09-29: current scan intensity and blue missile long flame retained; ask before future aesthetic changes.',
          'six_layer_user_approval':'not requested/received in this iteration'}
protected = {'MissilePod_2.png':'88f970e60700017ed0e3bf3a087102dbecd3439dc931ddcd63884c70d5689dde',
             'RainSalvo_2.png':'5655fcba866eae934e7737b9cb8872087dbb9499c306e9ce3ba24f9f2933c3a5'}
for name, digest in protected.items():
    assert sha(OUT/name) == digest, 'Accepted layer changed: '+name
report['protected_layer_hashes'] = protected
border_requests = json.loads((OUT/'border-requests.json').read_text(encoding='utf-8'))
border_raw = {'MissilePod':'exec-b3ec430d-5402-46cf-ab88-6a6a19f805ac.png',
              'RainSalvo':'exec-23699116-71b4-4713-910d-c32248afc616.png'}
report['background_margins'] = []
for request in border_requests:
    card = request['card']
    path = OUT/f'{card}_6_Margin.png'
    prompt = OUT/'Prompts'/f'{card}_6_Margin.txt'
    prompt.write_bytes(request['prompt'].encode('utf-8'))
    with Image.open(path) as im:
        size, mode = list(im.size), im.mode
    report['background_margins'].append({'card':card,'output':str(path),'output_sha256':sha(path),
        'prompt':str(prompt),'prompt_sha256':sha(prompt),'raw_source':str(Path(sources[0]['source']).parent/border_raw[card]),
        'inputs':[{'path':p,'sha256':sha(p)} for p in request['referenced_image_paths']],
        'requested_size':[1536,2304],'actual_size':size,'mode':mode,
        'material_mapping':'Extended UV=(original UV+0.25)/1.5. Original source wins throughout [0,1]; extension used outside only.',
        'approval':'User: 允许，只补背景外沿'} )
(OUT/'generation-manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'files':len(items),'landmarks':checks},ensure_ascii=False))
