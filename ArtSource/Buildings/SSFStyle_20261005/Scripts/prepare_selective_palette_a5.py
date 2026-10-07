"""Only lighten original dark swatches; preserve original light swatches and shading."""
import json
from pathlib import Path

ROOT=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
OUT=ROOT/'References_A_v5'
OUT.mkdir(exist_ok=True)
assert not (OUT/'reference_manifest.json').exists(), 'Published A-v5 is frozen'
original=json.loads((ROOT/'References_A_v3/render_manifest.json').read_text(encoding='utf-8'))
lightened=json.loads((ROOT/'References_A_v4/palette_revision.json').read_text(encoding='utf-8'))

def lightness(code):
    rgb=[int(code[i:i+2],16)/255 for i in (1,3,5)]
    linear=[x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4 for x in rgb]
    y=sum(x*w for x,w in zip(linear,(.2126,.7152,.0722)))
    return 116*y**(1/3)-16 if y>.008856 else 903.3*y

# A transparent reference-stage classification, not a project-wide color standard.
# All six original light/middle swatches are locked to their exact A-v3 values.
cutoff=50
protected=[c for c in original['user_swatch_library'] if lightness(c)>=cutoff]
themes={};changes=[]
for asset,t in original['themes'].items():
    themes[asset]={'name':t['name'].replace('深青','浅青')}
    for role in ('Primary','Equipment','Accent','Frame'):
        color=t[role];is_dark=lightness(color)<cutoff
        result=lightened['themes'][asset][role] if is_dark else color
        assert is_dark or result==color
        themes[asset][role]=result
        changes.append({'asset':asset,'role':role,'source_hex':color,'output_hex':result,
                        'classification':'dark_to_lighten' if is_dark else 'light_or_middle_preserved',
                        'changed':result!=color,'L_star_before':round(lightness(color),2),
                        'L_star_after':round(lightness(result),2)})
report={'version':'SSF_Reference_A_v5','source_version':'SSF_Reference_A_v3',
        'previous_submission':'SSF_Reference_A_v4','user_instruction':'深色调浅，浅色别动',
        'original_user_swatch_library':original['user_swatch_library'],
        'method':'Only original swatches with CIE L* below 50 use the prior lighter derived swatch; other original HEX and original three-tone factors are preserved exactly. No global exposure, grade or saturation adjustment.',
        'dark_classification_L_star_below':cutoff,'protected_original_swatches':protected,
        'themes':themes,'changes':changes,'platform':lightened['platform'],
        'platform_source_swatch':'#274E61','shared_ink':lightened['shared_ink'],
        'ink_source_swatch':original['shared_ink'],
        'tone_thresholds':original['tone_thresholds'],'tone_factors':original['tone_factors'],
        'internal_line_strength':.45,'platform_line_strength':.48,
        'note':'All original light/middle HEX and three-tone shading factors remain A-v3. Dark line ink changes can affect edge pixels; this is not a claim of pixel-identical entire images. A/B remain pending.'}
assert all(c['output_hex']==c['source_hex'] for c in changes if not c['changed'])
assert report['tone_factors']==[.4,.72,1.0]
(OUT/'palette_revision.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'version':report['version'],'protected_original_swatches':protected,
                  'changed_roles':sum(c['changed'] for c in changes),'preserved_roles':sum(not c['changed'] for c in changes),
                  'themes':themes,'tone_factors':report['tone_factors']},ensure_ascii=False))
