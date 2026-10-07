import json, hashlib, datetime
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
mh=sha(R/'Production_B_v4/production_manifest.json')
bh=sha(R/'Production_B_v4/ControlRigMech_B_v4_Production.blend')
assert mh=='802ade7bbd94e336ec8d6b2fb7fa8dbe36efc7a27a20cb7ba37df34fc144da16'
assert bh=='bf566b78fb3fd03c516016ed0e32bc7ee8412929c303283985340d5d5b5d853b'
record={'version':'B-v4','decision':'approved_for_UE_import','user_message_evidence':'导入至ue','decision_date':'2026-10-04','recorded_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'manifest_path':'Production_B_v4/production_manifest.json','manifest_sha256':mh,'blender_sha256':bh,'target':'/Game/GuLiStrike/Mechs/ControlRigMech','budget_result':'over_budget_delta_retained; import authorization is not a budget or FPS pass','frozen_B_manifest_is_submission_snapshot':True}
p=R/'Approvals/Approval_B_v4_Import_20261004.json'
if p.exists():
    old=json.loads(p.read_text(encoding='utf-8'));assert old['manifest_sha256']==mh and old['blender_sha256']==bh
else:p.write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
dec=json.loads((R/'review_decisions.json').read_text(encoding='utf-8'))
dec['B'].update(status='approved_for_UE_import',user_decision='导入放行',user_message_evidence='导入至ue',decision_date='2026-10-04',approval_record=str(p.relative_to(R)))
dec['formal_UE'].update(status='export_readback_in_progress',delivery_version='UE-v1',source_B_version='B-v4')
(R/'review_decisions.json').write_text(json.dumps(dec,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(record,ensure_ascii=False))
