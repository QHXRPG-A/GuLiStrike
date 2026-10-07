"""Inspect the actual stored UE LODs, normals, colors and weights against the B_v2 exports."""
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
original=(R/'Scripts/readback_from_ue_v1.py').read_text(encoding='utf8')
assert "D=R/'UE_Delivery_v1'" in original and "source[key]['bones']" in original
adapted=original.replace("D=R/'UE_Delivery_v1'","D=R/'UE_Delivery_Team_v2'").replace("source[key]['bones']","source[asset['building']]['bones']")
exec(compile(adapted,str(R/'Scripts/readback_team_from_ue_v2.py'),'exec'),globals())
