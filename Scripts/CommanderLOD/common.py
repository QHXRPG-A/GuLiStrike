"""Current production contract for Commander unit mesh LODs."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / 'ArtSource/CommanderLOD_20261005'
LOD_COUNT = 3
LOD_NAMES = ('Near', 'Middle', 'Far')
DEFAULT_SCREEN_SIZES = (1.0, 0.056, 0.028)
REVIEW_PACKAGE = '/Game/GuLiStrike/Commander/LODReview_20261005'


def units():
    rows = json.loads((ROOT / 'Data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf8'))
    result = sorted(rows, key=lambda row: row['Id'])
    assert [row['Id'] for row in result] == [1, 2, 3, 4, 5, 6]
    return result


def selected_indices(count):
    assert count > 0
    return [0, 1, count - 1] if count > LOD_COUNT else list(range(count))


def unit_dir(name):
    path = ART / name
    for directory in ('Sources', 'FBX', 'Reports', 'Review'):
        (path / directory).mkdir(parents=True, exist_ok=True)
    return path


def require_unapproved_candidate():
    """A saved B-approved version must not be rewritten by candidate producers."""
    receipt = ART / 'approval_B.json'
    if receipt.exists() and json.loads(receipt.read_text(encoding='utf8'))['approval_B'] == 'approved':
        raise RuntimeError('CommanderLOD_3Tier_v1 is approved and frozen. Read formal_delivery.json; create a new review version before changing candidate artifacts or frozen evidence.')
