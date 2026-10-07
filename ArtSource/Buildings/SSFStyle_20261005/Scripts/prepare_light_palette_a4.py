"""Lighten the A-v3 color families with hue-preserving HSL adjustments; keep all existing regions."""
import colorsys
import json
from pathlib import Path

ROOT=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
OUT=ROOT/'References_A_v4'
OUT.mkdir(exist_ok=True)
assert not (OUT/'reference_manifest.json').exists(), 'Published A-v4 is frozen'
old=json.loads((ROOT/'References_A_v3/render_manifest.json').read_text(encoding='utf-8'))

def rgb(code): return [int(code[i:i+2],16) for i in (1,3,5)]
def lightness(code):
    values=[x/255 for x in rgb(code)]
    linear=[x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4 for x in values]
    y=sum(x*w for x,w in zip(linear,(.2126,.7152,.0722)))
    return 116*y**(1/3)-16 if y>.008856 else 903.3*y
def lifted(code,floor,saturation_cap=.6):
    if lightness(code)>=floor: return code,0
    h,l,s=colorsys.rgb_to_hls(*[x/255 for x in rgb(code)])
    s=min(s,saturation_cap)
    for step in range(1001):
        amount=step/1000
        out='#'+''.join(f'{round(x*255):02X}' for x in colorsys.hls_to_rgb(h,l+(1-l)*amount,s))
        if lightness(out)>=floor:
            return out,amount
    raise ValueError(code)

themes={};changes=[]
for key,t in old['themes'].items():
    result={'name':t['name'].replace('深青','浅青')}
    for role in ('Primary','Equipment','Accent','Frame'):
        target=62 if role=='Frame' else 65
        color,amount=lifted(t[role],target,.35 if role=='Frame' else (.45 if role=='Equipment' else .6))
        result[role]=color
        changes.append({'asset':key,'role':role,'source_hex':t[role],'output_hex':color,
                        'HSL_lightness_lift_towards_1':amount,'L_star_before':round(lightness(t[role]),2),
                        'L_star_after':round(lightness(color),2),'target_L_star':target})
    themes[key]=result
platform,platform_mix=lifted('#274E61',65)
ink,ink_mix=lifted('#274E61',55,.25)
report={'version':'SSF_Reference_A_v4','source_version':'SSF_Reference_A_v3',
        'user_instruction':'深色的颜色再浅一些，不要有太深的颜色','original_user_swatch_library':old['user_swatch_library'],
        'method':'Keep each original selected swatch hue, raise HSL lightness to role-specific CIE L* floor; cap saturation of revised dark frames/equipment for softer color',
        'themes':themes,'changes':changes,'platform':platform,'platform_HSL_lift':platform_mix,
        'shared_ink':ink,'ink_source_swatch':'#274E61','ink_HSL_lift':ink_mix,
        'tone_thresholds':[.12,.55],'tone_factors':[.78,.90,1.0],
        'internal_line_strength':.35,'platform_line_strength':.35,
        'note':'Derived lighter colors are explicit revisions; not falsely described as unchanged exact original HEX swatches. User A/B approvals remain pending.'}
(OUT/'palette_revision.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'themes':themes,'platform':platform,'ink':ink,'tone_factors':report['tone_factors']},ensure_ascii=False))
