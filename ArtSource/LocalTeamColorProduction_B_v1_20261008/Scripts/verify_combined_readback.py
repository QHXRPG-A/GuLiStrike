"""Read the saved combined B file; compare its actual scene data to sources."""
import bpy, json, sys, hashlib
import numpy as np
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import build_palette_only as c
p=c.O/'PaletteOnly_14Models_BlueRed_B_v1.blend'
bpy.ops.wm.open_mainfile(filepath=str(p),load_ui=False)
assert len(bpy.data.scenes)==29
overview=bpy.data.scenes['00_十四模型蓝红成品_B待审核']
instances=[o for o in overview.objects if o.get('review_display_instance')]
assert len(instances)==28 and all(o.instance_type=='COLLECTION' for o in instances)
action_hashes={c.action_sig(a) for a in bpy.data.actions}
rows=[]
for key in c.ALL_KEYS:
    r=json.loads((c.O/'Reports'/(key+'_production.json')).read_text(encoding='utf8'))
    for team in r['teams']:
        s=bpy.data.scenes[team['scene']]
        bpy.context.window.scene=s;bpy.context.view_layer.update()
        for rec in team['objects']:
            o=s.objects.get(rec['name']);assert o and c.signature(o)==rec['signature'],(key,rec['name'])
            region=o.data.attributes.get('Bv1_ColorRegion')
            if region:
                pal=o.data.color_attributes.get('SSF_PaletteLinear') or o.data.color_attributes.get('GuLi_PaletteLinear') or o.data.color_attributes['Bv1_PaintLinear']
                actual=np.empty(len(o.data.loops)*4,np.float32)
                pal.data.foreach_get('color',actual);actual=actual.reshape(-1,4)
                for face in o.data.polygons:
                    role=c.ROLES[region.data[face.index].value]
                    assert np.max(abs(actual[list(face.loop_indices)]-c.color(role,team['team'])))<2e-6,(key,'palette')
        for rig in team.get('rigs',[]):assert c.bonesig(s.objects[rig['name']])==rig['signature']
    assert all(h in action_hashes for h in r['original_animation_signatures'].values()),(key,'missing original clip data')
    rows.append({'key':key,'Blue_Red_source_geometry_and_palette_preserved':True,'source_rigs_preserved':True,'all_original_clip_data_present':True})
out={'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'native_scene_count':29,'overview_instances':28,'model_count':14,'checks':rows,'all_passed':True,'B_user_approval':'pending','UE_updated':False}
(c.O/'Reports/combined-save-readback.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf8')
print('COMBINED_SAVE_READBACK_29_SCENES_28_INSTANCES_14_MODELS_PASSED',flush=True)
