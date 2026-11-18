"""Register the complete imagegen r4 output; preserve all other final layers.

This performs canvas assembly and numerical checks only, not raster retouching
or visual review. The generated image is the sole source for the new layer.
"""
from pathlib import Path
import hashlib
import json
import shutil
import sys

sys.path.insert(0, 'C:/Users/a/.codex/tmp/enhanced-explosive-card-runtime')
import cv2
import numpy as np
from PIL import Image
from skimage.metrics import structural_similarity

B = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(name):
    return json.loads((B / name).read_text(encoding='utf-8'))


def write(name, obj):
    (B / name).write_text(json.dumps(obj, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


previous = read('Revisions/r3/manifest.json')
assembly = read('Revisions/r3/final_assembly.json')
g = assembly['geometry']
W, H = g['canvas']
assert sha(B / 'Inputs/L3_projectile_before_r4.png') == previous['layers'][2]['sha256']
unchanged = []
for layer in previous['layers']:
    if layer['index'] != 3:
        assert sha(B / layer['file']) == layer['sha256']
        unchanged.append(dict(index=layer['index'], file=layer['file'], sha256=layer['sha256']))

source = 'Generated/L3_projectile_clean_straight_r4.png'
im = Image.open(B / source)
assert im.mode == 'RGBA'
raw = np.array(im)
assert tuple(im.getchannel('A').getextrema()) == (0, 255)
rw, rh = im.size
# Match preserved mechanical features across the two generated versions.
# One uniform scale and translation correct model-induced framing drift.
# No shear, rotation, recoloring, segmentation, old-pixel paste or local warp.
registration = read('Checks/projectile_r4_registration.json')
assert registration['inliers'] >= 30 and registration['median_error_px'] < 8
matrix = np.array(registration['matrix'], np.float64)
if (rw, rh) == (W, H) and np.allclose(matrix, [[1, 0, 0], [0, 1, 0]]):
    shutil.copy2(B / source, B / 'Layers/L3_projectile.png')
    final = raw
else:
    premult = raw.astype(np.float32) / 255
    premult[:, :, :3] *= premult[:, :, 3:4]
    resampled = cv2.warpAffine(premult, matrix, (W, H), flags=cv2.INTER_LANCZOS4, borderMode=cv2.BORDER_CONSTANT)
    alpha = np.clip(resampled[:, :, 3:4], 0, 1)
    rgb = np.divide(resampled[:, :, :3], resampled[:, :, 3:4], out=np.zeros_like(resampled[:, :, :3]), where=resampled[:, :, 3:4] > 1e-7)
    final = np.clip(np.concatenate([rgb, alpha], 2) * 255, 0, 255).round().astype(np.uint8)
    Image.fromarray(final).save(B / 'Layers/L3_projectile.png')

request = read('projectile_r4_request.json')
inputs = []
for item in request['input_roles']:
    preserved = item.get('preserved_copy', item['file'])
    inputs.append(dict(item, preserved_sha256=sha(B / preserved)))
generation = dict(revision='20261117-r4', tool='image_gen.imagegen', mode='builtin_edit', file=source,
    original_output='C:/Users/a/.codex/generated_images/01a1ea0d-0066-7a93-a197-db399ccf3883/exec-9f7bb89e-b481-4f49-ac5c-45dc7e0aa67b.png',
    sha256=sha(B / source), size=[rw, rh], mode_rgba=im.mode, alpha_range=[int(raw[:, :, 3].min()), int(raw[:, :, 3].max())],
    prompt_file=request['prompt_file'], prompt_sha256=sha(B / request['prompt_file']), inputs=inputs,
    matrix_to_common=matrix.tolist(), registration=registration,
    prior_generated_pixels_copied=False, original_reference_pixels_copied=False, assistant_visual_test=False, user_visual_acceptance='pending')
write('projectile_r4_source.json', generation)
layer = assembly['layers'][2]
layer.update(sha256=sha(B / layer['file']), canvas=[W, H], alpha_range=[int(final[:, :, 3].min()), int(final[:, :, 3].max())],
    transparent_fraction=float(np.mean(final[:, :, 3] == 0)), source_revision='r4',
    source=dict(core=source, outpaint=None, reason='Complete corrected imagegen output mapped as a whole to the existing common canvas', protected_original_master_pixels=0),
    static_refinement_matrix=matrix.tolist(), replacement_source=generation)
assembly['static_refinement'].pop('3', None)
assembly.update(version='r4', revision='20261117-r4', projectile_replacement=generation,
    generation_records_extra=['projectile_r4_request.json', 'projectile_r4_source.json'],
    production_records_extra=['Checks/projectile_r4_validation.json', 'Checks/projectile_r4_registration.json'],
    requested_revision='User requested smooth continuous armor color blocks and a straight coaxial body; the frozen original remains comparison reference.',
    previous_revision_record='Revisions/r3/final_assembly.json')

composite = Image.new('RGBA', (W, H), (0, 0, 0, 0))
by_index = {l['index']: l for l in assembly['layers']}
for idx in g['draw_order']:
    component = Image.open(B / by_index[idx]['file']).convert('RGBA')
    assert component.size == (W, H)
    composite = Image.alpha_composite(composite, component)
composite.save(B / 'Checks/Composite_final.png')
reference = np.array(Image.open(B / 'Inputs/FinalReference.png').convert('RGB'), np.float32) / 255
core = np.array(composite.crop(tuple(g['source_core'])).convert('RGB'), np.float32) / 255
score = float(structural_similarity(reference, core, **assembly['metric_parameters']))
assembly.update(core_rgb_ssim=score, passed=score >= assembly['threshold'])
write('final_assembly.json', assembly)
write('Checks/projectile_r4_validation.json', dict(revision='20261117-r4', source=source, source_size=[rw, rh],
    final_file=layer['file'], final_sha256=layer['sha256'], final_size=[W, H], matrix_to_common=matrix.tolist(),
    final_alpha_range=layer['alpha_range'], transparent_fraction=layer['transparent_fraction'],
    unchanged_layer_hashes=unchanged, full_original_window_rgb_ssim=score, threshold=assembly['threshold'], ssim_passed=assembly['passed'],
    geometry_and_motion_parameters_unchanged=True, reference_sha256=sha(B / 'Inputs/FinalReference.png'),
    assistant_visual_test=False, user_visual_acceptance='pending', correction_goals=['smooth continuous color blocks', 'straight coaxial missile body']))
print(json.dumps(dict(size=[W, H], source_size=[rw, rh], matrix=matrix.tolist(), sha256=layer['sha256'], ssim=score, passed=assembly['passed'], unchanged_layers=len(unchanged))), flush=True)
