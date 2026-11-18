"""Publish one authoritative manifest and its local preview data."""
from pathlib import Path
import json,hashlib
B=Path(__file__).resolve().parent
assembly=json.loads((B/'final_assembly.json').read_text(encoding='utf-8'))
g=assembly['geometry']
decisions=json.loads((B/'user_decisions.json').read_text(encoding='utf-8'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
manifest=dict(schema='guli-six-layer-card/v1',revision='20261117-r3',title=decisions['title'],card='Ground/12.01',date='2026-11-17',phase='html_preview_user_review',reference=dict(file='Inputs/FinalReference.png',sha256=sha(B/'Inputs/FinalReference.png'),size=[1312,1184],identity_check='Checks/reference_identity.json'),user_decisions='user_decisions.json',geometry=g,layers=assembly['layers'],generation_records=['generation_requests.json','generation_results_r1.json','outpaint_requests_r2.json','outpaint_results_r2.json','land_r3_request.json','land_r3_source.json'],production_records=['registration_r1.json','assembly_r2.json','final_assembly.json'],similarity=dict(rgb_ssim=assembly['core_rgb_ssim'],threshold=.8,passed=assembly['passed'],measurement_scope='Entire original 1312x1184 window, all six final PNGs, all opacities 100%, no motion',reference_hash=assembly['reference_sha256'],parameters=assembly['metric_parameters']),preview=dict(file='index.html',url='http://127.0.0.1:8197/',default_mode='stack',solo_mode='full_common_canvas',renderer='Canvas2D',download_package='EnhancedExplosive_SixLayers.zip'),assistant_visual_test=False,user_visual_acceptance='pending',ue_import='not_requested_in_this_stage',source_pixels_copied=False)
manifest['layers'][4]['replacement_source']=assembly['land_replacement']
manifest['revision']=assembly.get('revision','20261117-r3')
manifest['generation_records'].extend(assembly.get('generation_records_extra',[]))
manifest['production_records'].extend(assembly.get('production_records_extra',[]))
if 'requested_revision' in assembly:
    manifest['requested_revision']=assembly['requested_revision']
    manifest['previous_revision_record']=assembly['previous_revision_record']
if (B/'manifest.json').exists():
    previous=json.loads((B/'manifest.json').read_text(encoding='utf-8'))
    if previous.get('revision')==manifest['revision'] and [l['sha256'] for l in previous.get('layers',[])]==[l['sha256'] for l in manifest['layers']]:
        for key in ('technical_validation','ue_import','user_authorized_formal_import'):
            if key in previous:manifest[key]=previous[key]
(B/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
data=dict(revision=manifest['revision'],source=manifest['reference']['file'],sourceCore=g['source_core'],canvas=g['canvas'],window=g['visible_window'],drawOrder=g['draw_order'],travel=g['max_travel_source_pixels'],maxTilt=g['max_card_tilt_degrees'],apertureRatio=g['aperture_ratio'],cropEachSide=g['crop_each_side_source_pixels'],motionOwners=g['motion_owner_depths'],ssim=assembly['core_rgb_ssim'],layers=[{k:l[k] for k in ['index','name','file','sha256','depth','motion_owner']} for l in assembly['layers']])
(B/'card-data.js').write_text('window.cardData = '+json.dumps(data,ensure_ascii=False,indent=2)+';\n',encoding='utf-8')
print(json.dumps(dict(preview=manifest['preview']['url'],layers=len(data['layers']),ssim=data['ssim'],crop=data['cropEachSide'])))

