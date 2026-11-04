"""Apply the agreed 2026-11-03 tuning as absolute Excel values through excelize.

Camera changes require the frozen, numerically read Blueprint preview view. This
script never guesses its transform or reads UE binary assets.
"""
import argparse
import json
import math
from pathlib import Path

from author_mobility_tables import BOOK, cli, column, write

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'Artifacts/Ground3C_20261103'
BLUEPRINT = '/Game/GuLiStrike/GroundMech/BP_GroundMech_Light.BP_GroundMech_Light'


def update(sheet, values, new_fields=()):
    rows = json.loads(cli('read', BOOK, '--sheet', sheet, '--format', 'json', '--raw'))['rows']
    headers = rows[0]
    cells = []
    for field in new_fields:
        if field not in headers:
            headers.append(field)
            col = column(len(headers))
            for line, value in enumerate((field, 'float', 'Necessary'), 1):
                cells.append(dict(cell=f'{col}{line}', value=value, type='string'))
    for row_number, row in enumerate(rows[3:], 4):
        for field, value in values.get(row[headers.index('name')], {}).items():
            assert field in headers, (sheet, field)
            index = headers.index(field)
            if index < len(row) and row[index] != '' and math.isclose(float(row[index]), value, rel_tol=1.e-10, abs_tol=1.e-10):
                continue
            cells.append(dict(cell=f'{column(index+1)}{row_number}', value=value, type='float'))
    assert set(values).issubset({row[headers.index('name')] for row in rows[3:]})
    if cells:
        write(sheet, cells)


def camera_values(path):
    captured = json.loads(Path(path).read_text(encoding='utf8'))
    assert captured['success'] and captured['blueprint'] == BLUEPRINT
    assert all(math.isclose(v, 1., abs_tol=1.e-6) for v in captured['actor_transform']['scale'])
    assert abs(captured['local_view']['roll']) < .01, 'Camera table requires an unrolled preview view'
    boom = captured['spring_arm']
    arm = boom['arm_length']
    x, y, z = boom['socket_offset']
    pitch, yaw, fov = boom['pitch'], boom['yaw'], captured['horizontal_fov_degrees']
    assert all(math.isfinite(v) for v in (arm, x, y, z, pitch, yaw, fov))
    assert 0 < arm <= 5000 and -89 <= pitch <= 89 and 10 <= fov <= 120
    return dict(ArmLength=arm, SocketOffsetX=x, SocketOffsetY=y, SocketOffsetZ=z,
                PitchDegrees=pitch, YawDegrees=yaw, FieldOfView=fov,
                MinDistance=arm*.65, MaxDistance=arm*2.,
                MinPitchDegrees=min(-75., pitch), MaxPitchDegrees=max(70., pitch))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--camera-capture', type=Path)
    args = parser.parse_args()
    mobility = dict(StrafeSpeedMultiplier=.8, MovingTurnRate=2160., TurnStartDegrees=1., TurnStopDegrees=.5)
    update('Mobility', {'Default': mobility}, ('StrafeSpeedMultiplier',))
    gait = {}
    for name in ('WalkForward', 'WalkBackward', 'StrafeLeft', 'StrafeRight', 'RunForward'):
        gait[name] = dict(HorizontalMotionScale=1.25 if name == 'WalkBackward' else 1.,
                          SwingLiftScale=.8125, PelvisMotionScale=.6875)
    gait['RunBackward'] = dict(HorizontalMotionScale=1.08, SwingLiftScale=.702, PelvisMotionScale=.594)
    for name in ('TurnLeft', 'TurnRight'):
        gait[name] = dict(BasePlayRate=4.)
    update('Gait', gait)
    camera = camera_values(args.camera_capture) if args.camera_capture else None
    if camera is not None:
        update('Camera', {'Free': camera})
    OUT.mkdir(parents=True, exist_ok=True)
    report = dict(mobility=mobility, gait=gait, free_camera=camera,
                  camera_capture=str(args.camera_capture.resolve()) if args.camera_capture else None,
                  camera_pending=camera is None, ue_imported=False)
    (OUT / 'source-tuning.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
