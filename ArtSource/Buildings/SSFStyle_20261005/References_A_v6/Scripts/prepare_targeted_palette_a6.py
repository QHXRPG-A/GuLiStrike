"""The attached A-v3 board is the baseline; change dark colors of two buildings only."""
import colorsys
import copy
import json
from pathlib import Path

ROOT=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
OUT=ROOT/'References_A_v6';OUT.mkdir(exist_ok=True)
assert not (OUT/'reference_manifest.json').exists()
old=json.loads((ROOT/'References_A_v3/render_manifest.json').read_text(encoding='utf-8'))
targets=['MilitaryFactory','StrategyCenter']
def rgb(code):return [int(code[i:i+2],16)/255 for i in (1,3,5)]
def lightness(code):
    linear=[x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4 for x in rgb(code)]
    y=sum(x*w for x,w in zip(linear,(.2126,.7152,.0722)))
    return 116*y**(1/3)-16 if y>.008856 else 903.3*y
def lifted(code,floor,cap):
    h,l,s=colorsys.rgb_to_hls(*rgb(code));s=min(s,cap)
    for step in range(1001):
        out='#'+''.join(f'{round(x*255):02X}' for x in colorsys.hls_to_rgb(h,l+(1-l)*step/1000,s))
        if lightness(out)>=floor:return out
    raise ValueError(code)
themes=copy.deepcopy(old['themes']);changes=[]
for key,t in themes.items():
    for role in ('Primary','Equipment','Accent','Frame'):
        code=t[role];changed=key in targets and role!='Accent' and lightness(code)<50
        result=lifted(code,50 if role=='Frame' else 55,.35 if role=='Frame' else .45) if changed else code
        t[role]=result
        changes.append({'asset':key,'role':role,'source_hex':code,'output_hex':result,'changed':changed,
                        'L_star_before':round(lightness(code),2),'L_star_after':round(lightness(result),2)})
assert sum(c['changed'] for c in changes)==4
report={'version':'SSF_Reference_A_v6','source_version':'SSF_Reference_A_v3',
        'user_instruction':'以你刚刚调的这一版为准，军工厂和战略中心的深色需要调浅，其他别动',
        'only_modified_assets':targets,'themes':themes,'changes':changes,
        'original_user_swatch_library':old['user_swatch_library'],
        'method':'Source is the exact attached A-v3 board. Only MilitaryFactory Equipment/Frame and StrategyCenter Primary/Frame are lightened by preserving hue and raising lightness. All other roles, other eight assets, line ink and original three-tone shading stay A-v3.',
        'platform':'#274E61','shared_ink':old['shared_ink'],'tone_thresholds':old['tone_thresholds'],
        'tone_factors':old['tone_factors'],'internal_line_strength':.45,'platform_line_strength':.48,
        'protected_original_swatches':[c for c in old['user_swatch_library'] if lightness(c)>=50],
        'note':'Drone and all other props remain A-v3 by the latest explicit instruction; no automatic propagation of the factory palette. User A for this revision is pending.'}
(OUT/'palette_revision.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'version':report['version'],'changed_roles':[c for c in changes if c['changed']],
                  'preserved_roles':sum(not c['changed'] for c in changes)},ensure_ascii=False))
