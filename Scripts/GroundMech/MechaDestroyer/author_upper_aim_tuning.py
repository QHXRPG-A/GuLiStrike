"""Author turret response through the existing excelize source-table pipeline."""
import json
from pathlib import Path
from author_3c_tuning import update

VALUES = dict(UpperBodyAimResponseSeconds=.10, UpperBodyAimSnapToleranceDegrees=.05)

if __name__ == '__main__':
    update('Mobility', {'Default': VALUES}, tuple(VALUES))
    out = Path(__file__).resolve().parents[3] / 'Artifacts/GroundUpperAim_20261104'
    out.mkdir(parents=True, exist_ok=True)
    report = dict(success=True, source='Data/Excel/GuLiStrikeMech.xlsx', sheet='Mobility',
                  values=VALUES, ue_imported=False)
    (out / 'source-tuning.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report))
