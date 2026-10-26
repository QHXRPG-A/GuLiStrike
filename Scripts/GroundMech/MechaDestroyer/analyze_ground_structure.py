"""Numerical comparison for the specifically authorized walking/turning repro."""
import json
import math
import sys
from pathlib import Path
import numpy as np
from bake_heavy_gait_data import matrix

OUT = Path(__file__).resolve().parents[3] / 'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export'
rest = {}
for bone in json.loads((OUT / 'ue_mesh_import.json').read_text())['bones']:
    rest[bone['name']] = rest.get(bone['parent'], np.eye(4)) @ matrix(bone['local'])
settings = json.loads((OUT.parents[4] / 'Data/Json/DT_GuLiStrikeMech_GroundIK.json').read_text())[0]


def analyze(name):
    source = json.loads((OUT / name).read_text())
    assert not source['error'], source['error']
    rows = []
    for r in source['rows']:
        if not r.get('ik', {}).get('serial', 0):
            continue
        if not rows or r['ik']['serial'] != rows[-1]['ik']['serial']:
            rows.append(r)
    result = {'capture': name, 'samples': len(rows), 'phases': {}}
    for phase in sorted({r['phase'] for r in rows}):
        selected = [r for r in rows if r['phase'] == phase and r['ik']['alpha'] > .99]
        output_steps, input_steps, knee_steps, pelvis_rates, angles, twists, length_errors, gaps, sole_errors, hinge_errors = [], [], [], [], [], [], [], [], [], []
        for r in selected:
            f = r['ik']
            feet = []
            for side, suffix in enumerate(('L', 'R')):
                s = f['sides'][side]
                h, k, a = [np.array(b['position']) for b in s['output'][:3]]
                rh, rk, ra = [rest[n + '_' + suffix][:3, 3] for n in ('UpperLeg', 'Leg', 'Foot')]
                l1, l2 = np.linalg.norm(k-h), np.linalg.norm(a-k)
                length_errors.extend([abs(l1-np.linalg.norm(rk-rh)), abs(l2-np.linalg.norm(ra-rk))])
                angles.append(math.degrees(math.acos(np.clip((h-k)@(a-k)/l1/l2, -1, 1))))
                axis = (a-h)/np.linalg.norm(a-h)
                pole = k-h-axis*((k-h)@axis)
                rest_axis = (ra-rh)/np.linalg.norm(ra-rh)
                ref = rk-rh-rest_axis*((rk-rh)@rest_axis)
                hinge = ref-axis*(ref@axis)
                twists.append(math.degrees(math.acos(np.clip(pole@hinge/np.linalg.norm(pole)/np.linalg.norm(hinge), -1, 1))))
                rn = np.cross(rk-rh, ra-rk); rn /= np.linalg.norm(rn)
                normal = np.cross(k-h, a-k); normal /= np.linalg.norm(normal)
                for index, bone_name in enumerate(('UpperLeg_', 'Leg_')):
                    local_axis = rest[bone_name+suffix][:3, :3].T @ rn
                    bone = s['output'][index]
                    axis = matrix({'q': bone['rotation'], 't': bone['position']})[:3, :3] @ local_axis
                    hinge_errors.append(math.degrees(math.acos(np.clip(axis@normal, -1, 1))))
                feet.append(a)
                if s['contact'] > .999 and s['locked_after']:
                    for bone_index, key, plane in ((2, 'Heel', 'heel_plane'), (3, 'Toe', 'toe_plane')):
                        bone = s['output'][bone_index]
                        local = settings[key + ('Right' if side else 'Left')]
                        p = matrix({'q': bone['rotation'], 't': bone['position']}) @ np.array([local[v] for v in 'XYZ'] + [1])
                        # Controlled flat scenario: component yaw only, world Z unchanged.
                        sole_errors.append(abs(p[2] * r.get('mesh_scale', [.996678]*3)[2] + f['world_position'][2] - s[plane][2]))
            gaps.append(feet[0][0]-feet[1][0])
        for a, b in zip(selected, selected[1:]):
            if b['ik']['serial'] - a['ik']['serial'] != 1:
                continue
            for side in (0, 1):
                x, y = a['ik']['sides'][side], b['ik']['sides'][side]
                input_steps.append(math.dist(x['input'][2]['position'], y['input'][2]['position']))
                output_steps.append(math.dist(x['output'][2]['position'], y['output'][2]['position']))
                knee_steps.append(math.dist(x['output'][1]['position'], y['output'][1]['position']))
            pelvis_rates.append(abs(b['ik']['pelvis'] - a['ik']['pelvis']) / b['ik']['dt'])
        result['phases'][phase] = {
            'max_input_ankle_step_cm': max(input_steps, default=0),
            'max_output_ankle_step_cm': max(output_steps, default=0),
            'max_knee_step_cm': max(knee_steps, default=0),
            'max_pelvis_correction_speed_cm_s': max(pelvis_rates, default=0),
            'inside_knee_angle_range_deg': [min(angles), max(angles)],
            'max_bend_plane_swivel_deg': max(twists),
            'max_leg_length_error_cm': max(length_errors),
            'max_hinge_axis_error_deg': max(hinge_errors),
            'min_foot_separation_cm': min(gaps),
            'max_locked_sole_height_error_cm': max(sole_errors, default=0),
            'locked_sole_samples': len(sole_errors),
        }
    return result


if __name__ == '__main__':
    names = sys.argv[1:] or ['structure_ik_before.json', 'structure_ik_after.json']
    report = {'visual_verification': 'User pending; no screenshots or viewport inspection', 'captures': [analyze(n) for n in names]}
    (OUT / 'structure_comparison.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
