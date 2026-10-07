"""Describe the complete post-approval group switch without changing formal references."""
import json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART
groups=json.loads((ART/'Reports/resource_groups.json').read_text(encoding='utf8'))['groups']
rows=[]
for group in groups:
 old=dict(ModelAsset=group['original_model'],PresentationClass=group['original_presentation'],VATDefinition=group['original_vat'])
 proposed=dict(old)
 if group['original_model']:proposed['ModelAsset']=group['candidate_models'][0]
 if group['candidate_presentation']:proposed['PresentationClass']=group['candidate_presentation']
 if group['candidate_vat']:proposed['VATDefinition']=group['candidate_vat']
 rows.append(dict(id=group['id'],name=group['name'],original_references=old,review_references=proposed,
  resource_group=group,rollback='Restore this group reference set in Excel/JSON/DataTable; discard its failed versioned copies; leave other groups intact.'))
plan=dict(version='CommanderLOD_3Tier_v1',state='prepared_not_applied',approval_B='pending',formal_switched=False,
 prerequisites=['Explicit B decision for the manifest content hash','Resolve or explicitly accept listed visual/budget deviations',
  'BiZhiMao native DataAsset configuration requires separately permitted native build/reload'],
 steps=['Preserve latest source rows and native reference readback per group',
  'Copy the complete approved group to versioned formal folders; remap its internal candidate dependencies',
  'Read back all three tiers, materials, animation definitions and component transforms before switching',
  'Change only the group ModelAsset/PresentationClass/VATDefinition references; BiZhiMao also changes construction ID8 Mesh',
  'Export via the existing Excel pipeline, import/reload native tables, verify unchanged IDs and gameplay values',
  'If any step fails, restore the complete group and native tables from that group snapshot'],groups=rows)
if (ART/'formal_delivery.json').exists():
 delivery=json.loads((ART/'formal_delivery.json').read_text(encoding='utf8'))
 assert delivery['success'] and delivery['formal_switched']
 plan.update(state='applied_verified',approval_B='approved',formal_switched=True,prerequisites=[],current_formal_groups=delivery['groups'],
  approval_receipt='approval_B.json',native_build='Reports/native_build.json')
(ART/'Reports/switch_plan.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2),encoding='utf8')
print('GROUP_SWITCH_PLAN_PREPARED',len(rows))
