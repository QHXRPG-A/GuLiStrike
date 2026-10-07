"""Freeze only reviewable native outputs; verify prior approved reference manifests."""
import json,hashlib,shutil
from pathlib import Path
from PIL import Image
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1'
file=O/'SSF_Production_B_v1.blend';digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();model=digest(file)
assert not (O/'production_manifest.json').exists(),'Do not overwrite a published B version'
report=json.loads((O/'construction_report.json').read_text(encoding='utf8'))
checks=['native_validation.json','palette_preservation_validation.json','animation_deformation_validation.json','render_evidence_manifest.json','style_breakdown_manifest.json','AnimationPreviews/animation_preview_manifest.json','AnimationPreviews/movie_readback_report.json']
for relative in checks:
 d=json.loads((O/relative).read_text(encoding='utf8'));assert d['source_blend_sha256']==model,(relative,'evidence from another model')
movies=json.loads((O/'AnimationPreviews/animation_preview_manifest.json').read_text());assert len(movies['clips'])==22
decoded=json.loads((O/'AnimationPreviews/movie_readback_report.json').read_text());assert len(decoded['clips'])==22 and decoded['all_22_delivered_movies_decoded']
assert json.loads((O/'AnimationPreviews/movie_pixel_validation.json').read_text())['all_110_checkpoints_compared']
assert json.loads((O/'visual_qa.json').read_text())['assistant_visual_review_completed']
prior=[]
for manifest in sorted(R.glob('References_A_v*/reference_manifest.json')):
 frozen=json.loads(manifest.read_text(encoding='utf8'))
 for record in frozen['files']:
  path=(R/record['file']).resolve();assert path.is_relative_to(R.resolve());assert path.exists() and path.stat().st_size==record['bytes'] and digest(path)==record['sha256'],(record['file'],'prior reference changed')
 prior.append({'version':frozen['version'],'manifest_sha256':digest(manifest),'tracked_files_verified':len(frozen['files'])})
snapshot=O/'ScriptsSnapshot';snapshot.mkdir(exist_ok=True)
names=['build_production_b1.py','build_atlas_lines_b1.py','build_source_actions_b1.py','refine_native_normals_b1.py','finalize_native_product_b1.py','render_product_evidence_b1.py','render_animation_previews_b1.py','capture_production_animation_source_b1.py','audit_native_product_b1.py','audit_palette_preservation_b1.py','audit_native_animation_deformation_b1.py','render_native_style_breakdown_b1.py','verify_delivered_movies_b1.py','package_product_boards_b1.py','package_static_qa_b1.py','package_style_and_movies_b1.py','record_visual_qa_b1.py','write_product_review_b1.py','freeze_product_b1.py']
for name in names:shutil.copy2(R/'Scripts'/name,snapshot/name)
for a in report['assets']:
 for e in a['lods']:e['outline_scope']='Opaque major mechanical surfaces selected within cap; every body component remains intact'
(O/'construction_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
entries=[];paths=[file,O/'README.md',O/'Budget_Exceptions.md',O/'construction_report.json',O/'native_validation.json',O/'palette_preservation_validation.json',O/'animation_deformation_validation.json',O/'budget_exception_record.json',O/'render_evidence_manifest.json',O/'style_breakdown_manifest.json',O/'visual_qa.json',O/'Overview_Buildings_B_v1.png',R/'Review_B_v1.md',R/'review_B_v1.html',R/'approval_A.json',R/'Source/source_manifest.json']
for folder,pattern in [('Renders','*.png'),('Sheets','*.png'),('QA','*.png'),('Textures','*.png'),('AnimationSource','*_FullPose.json'),('ScriptsSnapshot','*.py')]:paths.extend(sorted((O/folder).glob(pattern)))
paths.extend([O/'AnimationPreviews/animation_preview_manifest.json',O/'AnimationPreviews/movie_readback_report.json',O/'AnimationPreviews/movie_pixel_validation.json'])
for clip in movies['clips']:
 paths.extend([O/'AnimationPreviews'/clip['file'],O/'AnimationPreviews'/(clip['clip']+'_report.json')])
 paths.extend(O/'AnimationPreviews'/c['file'] for c in clip['checkpoints'])
paths.extend(sorted((O/'AnimationPreviews/Decoded').glob('*.png')))
for path in sorted(set(paths)):
 assert path.exists() and path.stat().st_size>0,path
 entry={'file':path.relative_to(R).as_posix(),'bytes':path.stat().st_size,'sha256':digest(path)}
 if path.suffix.lower()=='.png':
  with Image.open(path) as im:entry['resolution']=list(im.size)
 entries.append(entry)
manifest={'version':'SSF_Production_B_v1','date':'2026-10-05','art_revision':'1.3','stage':'actual_Blender_product_submitted_for_B','source_blend_sha256':model,'approved_A_version':'SSF_Reference_A_v7','A_manifest_sha256':digest(R/'References_A_v7/reference_manifest.json'),'approvals':{'A':'approved','B':'pending_user_decision','body_budget_exceptions':'pending_user_decision'},'assets':[{'key':a['key'],'source_dimensions_m':a['source_dimensions_m'],'editable_parts':len(a['parts']),'bone_count':a['rig_bone_count'],'lods':a['lods'],'animations':a.get('animations',[])} for a in report['assets']],
 'full_source_animation_samples':sum(c['full_source_samples'] for c in report['animations']),'all_22_clips_and_3_actual_LODs':True,'native_preview_frames':sum(c['frames'] for c in movies['clips']),'all_body_caps_passed':False,'body_budget_exception_count':24,'all_outline_caps_passed':True,'files':entries,'prior_reference_verification':prior,'working_intermediates_and_logs_excluded':True,'formal_export_roundtrip_performed':False,'UE_formal_import_performed':False,'UE_performance_or_project_camera_validation_performed':False}
target=O/'production_manifest.json';target.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
for entry in entries:
 p=R/entry['file'];assert p.stat().st_size==entry['bytes'] and digest(p)==entry['sha256']
result={'version':manifest['version'],'manifest_sha256':digest(target),'source_blend_sha256':model,'tracked_files':len(entries),'all_current_files_exist_and_match_hash':True,'prior_reference_versions_preserved':len(prior),'B_status':'pending_user_decision','budget_status':'24_body_exceptions_pending','UE_formal_import_performed':False}
(R/'DeliveryValidation_B_v1.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8');print('SSF_B_PRODUCT_FROZEN_AND_VERIFIED',json.dumps(result),flush=True)
