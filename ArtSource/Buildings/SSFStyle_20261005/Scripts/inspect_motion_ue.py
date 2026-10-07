"""Read local bone motion at five times per source clip, without posing actors."""
from pathlib import Path
import json
import math
import unreal

ROOT = Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
source = json.loads((ROOT / 'Source/source_manifest.json').read_text(encoding='utf-8'))
by_skeleton = {m['skeleton']: m for m in source['meshes'] if m['class'] == 'SkeletalMesh'}
records = []
def xyz(v): return [float(v.x), float(v.y), float(v.z)]
def pose(t):
    q = t.rotation
    return {'translation_cm': xyz(t.translation), 'rotation_xyzw': [q.x, q.y, q.z, q.w],
            'scale': xyz(t.scale3d)}
for clip in source['animations']:
    asset = unreal.load_asset(clip['path'])
    mesh = by_skeleton[clip['skeleton']]
    times = [clip['duration_s'] * f for f in (0, .25, .5, .75, 1)]
    changed = []
    root_samples = None
    for b in mesh['bones']:
        samples = [pose(unreal.AnimationLibrary.get_bone_pose_for_time(asset, b['name'], t, False)) for t in times]
        first = samples[0]
        max_cm = max(math.dist(first['translation_cm'], s['translation_cm']) for s in samples)
        max_scale = max(math.dist(first['scale'], s['scale']) for s in samples)
        max_deg = max(math.degrees(2 * math.acos(min(1.0, abs(sum(a*c for a,c in zip(
            first['rotation_xyzw'], s['rotation_xyzw'])))))) for s in samples)
        if max_cm > .01 or max_deg > .1 or max_scale > .0001:
            changed.append({'bone': b['name'], 'max_translation_from_start_cm': round(max_cm, 4),
                            'max_rotation_from_start_deg': round(max_deg, 3),
                            'max_scale_delta': round(max_scale, 6)})
        if b['parent_index'] == -1:
            root_samples = samples
    records.append({'asset': mesh['key'], 'clip': clip['name'], 'duration_s': clip['duration_s'],
                    'times_s': times, 'moving_bones': changed, 'root_samples': root_samples})
out = {'stage': 'A_source_motion_inventory', 'method': 'Five local-pose samples per clip; not full animation acceptance',
       'clips': records, 'ue_assets_saved': False}
(ROOT / 'Source/motion_inventory.json').write_text(json.dumps(out, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'clips': len(records),
    'moving_bone_counts': {r['clip']: len(r['moving_bones']) for r in records}, 'ue_assets_saved': False}))
