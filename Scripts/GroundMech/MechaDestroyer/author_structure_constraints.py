"""Add bounded rigid-leg/plant transitions through the existing Excel pipeline."""
import json
from author_mobility_tables import cli, BOOK, write, column

fields = {
    'MinKneeAngle': (80., 'Minimum inside knee angle in degrees; prevents overfolding'),
    'MaxKneeAngle': (155., 'Maximum inside knee angle; avoids straight-leg singularity'),
    'MaxLegSwingDegrees': (45., 'Hip-to-ankle cone about the authored rest leg'),
    'MaxKneeTwistDegrees': (25., 'Bend-plane swivel about the authored hinge plane'),
    'MinFootSeparation': (40., 'Minimum left/right foot separation in mesh space, cm'),
    'LockMaxYawDegrees': (25., 'Release planted foot before body/platform yaw exceeds this angle'),
    'LockReleaseSeconds': (.18, 'Continuous plant-lock fade duration, seconds'),
    'MaxPelvisSpeed': (120., 'Pelvis correction speed cap, cm/s'),
    'MaxCorrectionSpeed': (360., 'Additional IK/stride correction speed cap, cm/s'),
    'MaxFootAngularSpeed': (180., 'Ankle correction angular speed cap, degrees/s'),
}
data = json.loads(cli('read', BOOK, '--sheet', 'GroundIK', '--format', 'json'))['rows']
headers = data[0]
cells = []
for name, (value, note) in fields.items():
    if name not in headers:
        headers.append(name)
    col = column(headers.index(name) + 1)
    for row, val in ((1, name), (2, 'float'), (3, 'Necessary'), (4, value)):
        cells.append({'cell': f'{col}{row}', 'value': val})
cells.append({'cell': f'{column(headers.index("LockReleaseDistance") + 1)}4', 'value': 90.})
write('GroundIK', cells)
print(json.dumps({'sheet': 'GroundIK', 'fields': fields, 'LockReleaseDistance': 90.}, indent=2))
