"""Bake contact curves from raw animation tracks and actual skinned sole geometry (no pose tick)."""
import json
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_dir()).resolve()
out=root/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export'
folder='/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Animations'
mesh=unreal.load_asset('/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Meshes/SK_GroundMech_A4B2')
ik=json.loads((root/'Data/Json/DT_GuLiStrikeMech_GroundIK.json').read_text())[0]
result=json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.bake_ground_contacts(mesh,folder,ik['ContactHeightTolerance'],ik['ContactBlendHeight']))
assert result['success'],result
for clip in result['clips']:
    assert unreal.EditorAssetLibrary.save_loaded_asset(unreal.load_asset(clip['asset']),False)
(out/'ground_contact_calibration.json').write_text(json.dumps(result,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(result))
