"""Freeze A-v2 while verifying that published A-v1 and shared baseline are intact."""
import copy
import hashlib
import json
from datetime import datetime,timezone
from pathlib import Path
from PIL import Image

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT = ROOT/'References_A_v2'
if (OUT/'reference_manifest.json').exists():
    raise SystemExit('A-v2 already frozen; use a new sibling version.')
old = json.loads((ROOT/'References_A_v1/reference_manifest.json').read_text(encoding='utf-8'))
setup = json.loads((OUT/'reference_setup.json').read_text(encoding='utf-8'))
assert setup['geometry_digest_before']==setup['geometry_digest_after']
assert len(setup['palette_revision'])==8
assert setup['cameras']==json.loads((ROOT/'References_A_v1/reference_setup.json').read_text())['cameras']
for a in old['artifacts']:
    assert hashlib.sha256((ROOT/a['path']).read_bytes()).hexdigest()==a['sha256'], a['path']


def artifact(relative, role):
    path = ROOT/relative
    record = {'path':relative,'role':role,'bytes':path.stat().st_size,
              'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
    if path.suffix=='.png':
        with Image.open(path) as im:
            record['resolution_px'] = list(im.size)
    return record


artifacts=[]
image_check=[]
for view in ('Hero','Front','Left','Back'):
    relative = f'References_A_v2/RSG_A_v2_{view}_SourceStyle.png'
    a=artifact(relative,'three_quarter_effect_reference' if view=='Hero' else 'orthographic_'+view.lower())
    assert a['resolution_px']==[2048,2048]
    artifacts.append(a)
    with Image.open(ROOT/relative) as image:
        rgb=image.convert('RGB')
        bg=rgb.getpixel((0,0))
        blue=body=0
        for r,g,b in list(rgb.getdata()):
            if abs(r-bg[0])+abs(g-bg[1])+abs(b-bg[2])>20:
                body+=1
                if b>r+25 and g>r+15 and b>=g:
                    blue+=1
        image_check.append({'view':view,'sky_blue_pixels':blue,'body_pixels_estimated':body,
                            'blue_fraction_of_body_estimated':round(blue/max(body,1),4)})
artifacts += [artifact('References_A_v2/RSG_A_v2_ReferenceMaterialStudy.blend','reference_material_study_not_production_model'),
              artifact('References_A_v2/reference_setup.json','palette_revision_same_cameras_and_source_geometry'),
              artifact('Scripts/render_style_reference_a_v2.py','reproducible_palette_reference_render'),
              artifact('README_A_v2.md','current_review_notes'),
              artifact('References_A_v2/imagegen_color_prompt.txt','builtin_imagegen_color_draft_prompt'),
              artifact('References_A_v2/RSG_A_v2_ImagegenColorDraft.png','draft_not_selected_for_A'),
              artifact('References_A_v1/reference_manifest.json','previous_frozen_version'),
              artifact('Source/source_manifest.json','authoritative_original_source')]
manifest=copy.deepcopy(old)
manifest.update({'version':'A-v2','published_utc':datetime.now(timezone.utc).isoformat(),
                 'previous_version':'A-v1','user_revision_request':'加一点天蓝色',
                 'artifacts':artifacts,'palette_srgb':setup['palette_srgb'],
                 'palette_revision':setup['palette_revision'],
                 'source_geometry_digest_before':setup['geometry_digest_before'],
                 'source_geometry_digest_after':setup['geometry_digest_after'],
                 'all_A_v1_artifact_hashes_intact':True,
                 'same_cameras_and_pose_as_A_v1':True,
                 'sky_blue_visibility_estimates':image_check,
                 'image_generation':{'mode':'builtin_imagegen','selected_for_review':False,
                    'draft':'References_A_v2/RSG_A_v2_ImagegenColorDraft.png',
                    'draft_resolution_px':[1254,1254],
                    'reason':'Below 2K and some panel differences; archived color study only.',
                    'prompt':'References_A_v2/imagegen_color_prompt.txt',
                    'review_images_method':'source-based Blender reference material renders'},
                 'approvals':{'A':{'status':'pending','user_decision':None,'version':'A-v2'},
                              'B':{'status':'not_started','user_decision':None,'version':None}}})
(OUT/'reference_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'version':'A-v2','all_images_2048':True,'A_v1_intact':True,
                  'geometry_unchanged':True,'new_blue_parts':8,
                  'sky_blue_visibility':image_check,'A':'pending'},ensure_ascii=False))
