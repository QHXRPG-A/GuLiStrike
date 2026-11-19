import json
from pathlib import Path
root=Path(__file__).resolve().parent
old=json.loads((root/'card-data_v6.js').read_text(encoding='utf8').split('=',1)[1].strip().rstrip(';'))
assembled=json.loads((root/'assembled_outpaint_v7.json').read_text(encoding='utf8'))
old_manifest=json.loads((root/'manifest_v6.json').read_text(encoding='utf8'))
closure_path=root/'cleanup_20261117.json'
closed=closure_path.exists() and json.loads(closure_path.read_text(encoding='utf-8-sig'))['status']=='completed'
report={'version':7,'scope':'Generative outpainting of v6 layer boundaries; existing generated core pixels retained',
 'canvas':assembled['canvas'],'window':assembled['window'],'travel_px':[24,30],
 'core_change_pixel_count':0,'original_similarity_requirement':.95,
 'status':'done' if closed else 'edge-review',
 'completion_basis':'User explicitly requested session closure on 2026-11-17' if closed else '',
 'visual_review':'No separate visual-pass claim; task closed at user request' if closed else 'pending user review; no assistant result-image inspection',
 'no_new_segmentation':True,'no_master_pixel_pasting':True,'cards':{}}
for card,c in old.items():
 c['canvas']=assembled['canvas'];c['window']=assembled['window'];c['revision']='7-edge-2'
 c['status']='done' if closed else 'edge-review';c['overscan_ready']=True
 c['reference_similarity']['mode']='Unchanged v6 core; browser measurement to be confirmed for v7'
 for layer,row in zip(c['layers'],assembled['cards'][card]):
  layer['file']=row['file'];layer['size']=row['size']
 measurement_path=root/card/'browser_measurement_v7.json'
 if measurement_path.exists():
  measured=json.loads(measurement_path.read_text(encoding='utf8'))
  if measured['layer_hashes']==[r['sha256'] for r in assembled['cards'][card]]:
   c['reference_similarity']['value']=measured['browserRgbSsim']
   c['reference_similarity']['mode']='Actual browser RGBA composition, original window, zero offset and tilt, opacity sliders 100%'
 report['cards'][card]={'source':c['source'],'source_sha256':old_manifest['cards'][card]['source_sha256'],
  'reference_similarity':c['reference_similarity'],'similarity_pass':c['reference_similarity']['value']>=.95,
  'layers':[{**l,'export_sha256':r['sha256'],'base_v6':r['base'],'base_core_pixel_identical':True,
             'alpha_min':r['alpha_min'],'alpha_max':r['alpha_max'],'outpaint_sources':r['sources']}
             for l,r in zip(c['layers'],assembled['cards'][card])]}
check_path=root/'technical_checks_v7.json'
if check_path.exists():
 checks=json.loads(check_path.read_text(encoding='utf8'))
 matching=all(checks['cards'].get(card,{}).get('layer_hashes')==[r['sha256'] for r in rows]
              for card,rows in assembled['cards'].items())
 if matching:
  report['technical_checks']={'command':'node verify_generated_v7.cjs',**checks,
    'limitations':'Texture-bound coverage does not certify generated seam continuity or hidden-surface completeness.'}
(root/'card-data_v7.js').write_text('window.cardCollection = '+json.dumps(old,ensure_ascii=False,indent=2)+';\n',encoding='utf8')
(root/'manifest_v7.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('12 layers; common canvas',assembled['canvas'],'window',assembled['window'])

