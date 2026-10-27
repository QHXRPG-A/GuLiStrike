"""Approved Blink 3C tuning; workbook I/O stays on excelize-cli."""
import json
from migrate_blink_source import MECH, VFX, cell, read, write

BODY = '/Game/GuLiStrike/FX/GroundMech/Blink/DA_GroundMech_BlinkVfx.DA_GroundMech_BlinkVfx'


def append_fields(book, sheet, values, required='Optional'):
    rows = read(book, sheet)
    headers = list(rows[0])
    edits = []
    for key, (kind, _) in values.items():
        if key not in headers:
            headers.append(key)
            edits.extend(cell(len(headers), i, v) for i, v in enumerate((key, kind, required), 1))
    return rows, headers, edits


def main():
    if 'BlinkConcealSeconds' in read(MECH, '\u6280\u80fd\u8868')[0]:
        raise RuntimeError('Blink is now an endpoint teleport; run configure_blink_teleport_source.py. Legacy Loop/material authoring is retired.')
    rows = read(VFX, 'Effects')
    names = [(i, dict(zip(rows[0], row))) for i, row in enumerate(rows[3:], 4) if len(row) > 1 and row[1]]
    found = next(((i, r) for i, r in names if r['name'] == 'GroundMechBlinkBody'), None)
    if found:
        line, row = found
        if row['ResourcePath'] not in (BODY,'/Game/GuLiStrike/FX/GroundMech/Blink/M_GroundMech_BlinkPhase.M_GroundMech_BlinkPhase'):
            raise RuntimeError('Existing Blink body VFX has a different owner/path')
        body_id = int(row['id'])
        write(VFX,'Effects',[cell(rows[0].index('ResourcePath')+1,line,BODY)])
    else:
        body_id = max(int(row['id']) for _, row in names) + 1
        line = max(i for i, _ in names) + 1
        data = dict(id=body_id, name='GroundMechBlinkBody', ResourcePath=BODY,
                    Note='Approved Blink 3C: full-body phase lattice, fracture flicker and recovery',
                    ScaleX=1, ScaleY=1, ScaleZ=1)
        write(VFX, 'Effects', [cell(i+1, line, data.get(key, 0 if rows[1][i] in ('int','float') else ''),
            rows[1][i] if rows[1][i] in ('int','float') else 'string') for i, key in enumerate(rows[0])])
    settings = {
        'BlinkBodyVfxId': ('int', body_id), 'BlinkCrouchDepthCm': ('float', 60),
        'BlinkKneeAngleDegrees': ('float', 110), 'BlinkFlickerHz': ('float', 24),
        'BlinkFragmentOffsetCm': ('float', 8), 'BlinkBodyEmission': ('float', 8),
        'BlinkRibbonLayers': ('int', 3), 'BlinkRibbonSpawnRate': ('float', 670),
        'BlinkArmBackDegrees': ('float', 145), 'BlinkBodyLeanDegrees': ('float', 30),
        'BlinkRecoverySeconds': ('float', .3),
    }
    rows, headers, edits = append_fields(MECH, '\u6280\u80fd\u8868', settings)
    line = next(i for i, row in enumerate(rows[3:], 4) if len(row)>1 and row[1]=='Blink')
    edits.extend(cell(headers.index(k)+1, line, value, kind) for k,(kind,value) in settings.items())
    write(MECH, '\u6280\u80fd\u8868', edits)
    camera = {'BlinkFovBoostDegrees': ('float', 15), 'BlinkFovReturnSeconds': ('float', .25),
              'BlinkHoldSeconds': ('float', 0), 'BlinkCatchupSeconds': ('float', .45)}
    rows, headers, edits = append_fields(MECH, 'Camera', camera, 'Necessary')
    for i, row in enumerate(rows[3:], 4):
        if len(row)>1 and row[1] in ('Free','Aim'):
            edits.extend(cell(headers.index(k)+1, i, value, kind) for k,(kind,value) in camera.items())
    write(MECH, 'Camera', edits)
    print(json.dumps({'body_vfx_id': body_id, 'layers': 3, 'points_per_second': 2010,
                      'arm_back': 145, 'lean': 30, 'max_crouch_cm': 60, 'knee_angle': 110}))


if __name__ == '__main__':
    main()
