"""Reuse the proven native FBX reader against the new 36 exports, without changing v1."""
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
original=(R/'Scripts/readback_ue_delivery_v1.py').read_text(encoding='utf8')
assert "D=R/'UE_Delivery_v1'" in original
adapted=original.replace("D=R/'UE_Delivery_v1'","D=R/'UE_Delivery_Team_v2'").replace('SSF_ALL_30_FBX_READBACK_PASSED','SSF_ALL_36_TEAM_FBX_READBACK_PASSED')
exec(compile(adapted,str(R/'Scripts/readback_team_fbx_v2.py'),'exec'),globals())
