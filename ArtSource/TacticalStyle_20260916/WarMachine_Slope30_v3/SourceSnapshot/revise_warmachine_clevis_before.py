"""Build and review the v2 clevis connection on the retained v1 hub seats."""
import json
import sys
from pathlib import Path

import bpy
import bmesh
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).parent))
import revise_warmachine_hub_seat as review

review.SOURCE = review.ROOT / 'ArtSource/TacticalStyle_20260916/WarMachine_HubSeat_v1'
review.SOURCE_EDITABLE = review.SOURCE / 'WarMachine_HubSeat_Editable.blend'
review.OUT = review.ROOT / 'ArtSource/TacticalStyle_20260916/WarMachine_LegClevis_v2'
review.PREVIEWS = review.OUT / 'Previews'
review.EDITABLE = review.OUT / 'WarMachine_LegClevis_Editable.blend'
review.REVIEW = review.OUT / 'WarMachine_LegClevis_Review.blend'
review.REPORT = json.loads((review.SOURCE / 'WarMachine_handbuilt_report.json').read_text(encoding='utf8'))
review.SCALE = review.REPORT['source_scale']
review.OFFSET = Vector(review.REPORT['source_offset'])
review.CHANGED_PREFIXES = ('WM suspension diagonal_', 'WM suspension toe ', 'WM toe amber optic_',
                           'WM suspension hub seat_', 'WM suspension pivot eye_',
                           'WM suspension clevis lug_', 'WM suspension cross pin_',
                           'WM suspension pin collar_', 'WM suspension pin cap_')
review.EXPECTED_REPLACEMENTS = 32
review.REVISION_DESCRIPTION = 'v2: tapered beam eye, drilled clevis lugs and transverse pin on retained hub seats'


def validate_joints():
    bpy.ops.wm.open_mainfile(filepath=str(review.EDITABLE))
    measurements = []
    for corner in ('F_L', 'F_R', 'R_L', 'R_R'):
        x, inner = (2.75, .63) if corner.startswith('F') else (-2.82, -1.60)
        sign = 1 if corner.endswith('L') else -1
        forward = Vector((x-inner, 3.02-1.02, 0)).normalized()
        axis = Vector((-forward.y, forward.x * sign, 0))
        pin = bpy.data.objects['WM suspension cross pin_' + corner]
        center = sum((v.co for v in pin.data.vertices), Vector()) / len(pin.data.vertices)
        pin_radius = max(((v.co-center)-axis*(v.co-center).dot(axis)).length for v in pin.data.vertices)
        hole_results = []
        for name in ['WM suspension pivot eye_' + corner,
                     'WM suspension clevis lug_' + corner + ' inner',
                     'WM suspension clevis lug_' + corner + ' outer']:
            obj = bpy.data.objects[name]
            inner_ring = [v.co for v in obj.data.vertices][40:60]
            hole_center = sum(inner_ring, Vector()) / len(inner_ring)
            delta = hole_center-center
            alignment = (delta-axis*delta.dot(axis)).length
            radius = min(((v-center)-axis*(v-center).dot(axis)).length for v in inner_ring)
            hole_results.append({'object': name, 'axis_error_m': alignment,
                                 'minimum_radial_clearance_m': radius-pin_radius})
        measurements.append({'corner': corner, 'pin_radius_m': pin_radius,
                             'eye_to_lug_clearance_each_side_m': (.32-.075-.21)*review.SCALE,
                             'holes': hole_results})
    invalid = []
    new_names = json.loads((review.OUT/'geometry-check.json').read_text(encoding='utf8'))['added_parts']
    for name in new_names:
        obj = bpy.data.objects[name]
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        if any(not e.is_manifold for e in bm.edges) or bm.calc_volume(signed=True) <= 0:
            invalid.append(name)
        bm.free()
    report = {'pin_hole_alignment': measurements, 'non_manifold_or_inward_new_parts': invalid,
              'display_note': 'Static mechanical assembly only; no skeleton or animated constraints added'}
    review.write_json(review.OUT/'joint-check.json', report)
    assert not invalid, invalid
    assert all(h['axis_error_m'] < 1e-4 and h['minimum_radial_clearance_m'] > 0
               for m in measurements for h in m['holes'])
    review.log('Four pins and twelve actual axis holes checked')


if __name__ == '__main__':
    review.main()
    stage = sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'all'
    if stage in ('all', 'prepare', 'validate'):
        validate_joints()
