"""Keep unused original animation clips inside the saved candidate, unchanged."""
import bpy,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
import build_palette_only as c
for key in c.ALL_KEYS:
    p=c.O/'Models'/(key+'_PaletteOnly_B_v1.blend')
    r=json.loads((c.O/'Reports'/(key+'_production.json')).read_text(encoding='utf8'))
    names=r['original_animation_signatures']
    if not names:continue
    bpy.ops.wm.open_mainfile(filepath=str(p),load_ui=False)
    missing=[n for n in names if n not in bpy.data.actions]
    if missing:
        with bpy.data.libraries.load(r['source'],link=False) as (src,dst):dst.actions=list(missing)
    for n,digest in names.items():
        assert c.action_sig(bpy.data.actions[n])==digest,(key,n)
        bpy.data.actions[n].use_fake_user=True
    bpy.ops.wm.save_as_mainfile(filepath=str(p))
    print('UNCHANGED_SOURCE_ACTIONS_RETAINED',key,len(names),flush=True)
