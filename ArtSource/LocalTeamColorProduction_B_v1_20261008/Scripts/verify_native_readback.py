"""Delivery save/readback inspection; does not modify native models or run a game."""
import bpy,json,sys,hashlib
import numpy as np
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
import build_palette_only as c
results=[]
for key in c.ALL_KEYS:
    p=c.O/'Models'/(key+'_PaletteOnly_B_v1.blend')
    r=json.loads((c.O/'Reports'/(key+'_production.json')).read_text(encoding='utf8'))
    assert hashlib.sha256(Path(r['source']).read_bytes()).hexdigest()==r['source_sha256'],(key,'source changed')
    bpy.ops.wm.open_mainfile(filepath=str(p),load_ui=False)
    checks=[];regions={};tones=set();lamp_groups=[]
    for team in r['teams']:
        s=bpy.data.scenes[team['scene']];bpy.context.window.scene=s;bpy.context.view_layer.update()
        assert s.render.use_freestyle and s.view_settings.view_transform=='Standard'
        for rec in team['objects']:
            o=bpy.data.objects[rec['name']];assert c.signature(o)==rec['signature'],(key,o.name,'geometry/normals/UV/weights/transform')
            me=o.data;at=me.attributes.get('Bv1_ColorRegion')
            if at:
                roles=np.array([v.value for v in at.data]);pal=me.color_attributes.get('SSF_PaletteLinear') or me.color_attributes.get('GuLi_PaletteLinear') or me.color_attributes['Bv1_PaintLinear']
                actual=np.empty(len(me.loops)*4,np.float32);pal.data.foreach_get('color',actual);actual=actual.reshape(-1,4)
                expected=np.zeros_like(actual)
                for face in me.polygons:expected[list(face.loop_indices)]=c.color(c.ROLES[roles[face.index]],team['team'])
                err=float(abs(actual-expected).max());assert err<2e-6,(key,o.name,'palette',err)
                if team['team']=='Blue':regions[rec['source']]=(roles.copy(),actual.copy())
                else:
                    other,bcol=regions[rec['source']];assert np.array_equal(roles,other),(key,'team region mismatch')
                    fixedloops=[l for f in me.polygons if c.ROLES[roles[f.index]] not in ('TeamLight','TeamDark','TeamLightLamp') for l in f.loop_indices]
                    assert np.array_equal(actual[fixedloops],bcol[fixedloops]),(key,'fixed color changed')
                checks.append({'object':o.name,'LOD':rec['LOD'],'mesh_and_deformation_signature_equal':True,'palette_max_error':err,'role_face_counts':{role:int((roles==n).sum()) for n,role in enumerate(c.ROLES)}})
                if key=='ShieldGenerator':
                    top=roles==c.ROLES.index('TeamLightLamp');assert int(top.sum())==150
                    assert not (roles==c.ROLES.index('Cyan')).any()
                    groups=[x for x in team['region_mapping']['SM_ShieldGenerator'] if 'Crown shield emitter top' in str(x['source_component'])]
                    assert len(groups)==3 and all(x['role']=='TeamLightLamp' for x in groups)
                    lamp_groups.append({'team':team['team'],'original_top_components':3,'faces':int(top.sum()),'color':c.PALETTE[team['team']+'Light'],'fixed_blue_or_cyan_faces':0})
            else:checks.append({'object':o.name,'LOD':rec['LOD'],'mesh_and_deformation_signature_equal':True,'original_outline_geometry':True})
            for mat in me.materials:
                if 'outline' in mat.name.lower() or 'contour' in mat.name.lower():continue
                ramps=[n for n in mat.node_tree.nodes if n.type=='VALTORGB' and len(n.color_ramp.elements)==3]
                assert ramps,(key,mat.name,'missing three-tone')
                assert any(n.color_ramp.interpolation=='CONSTANT' and np.allclose([e.color[0] for e in n.color_ramp.elements],[.4,.72,1.]) for n in ramps),(key,'tone mismatch')
                tones.add(mat.name)
        for rig in team.get('rigs',[]):assert c.bonesig(bpy.data.objects[rig['name']])==rig['signature'],(key,'rig changed')
    for name,digest in r['original_animation_signatures'].items():assert c.action_sig(bpy.data.actions[name])==digest,(key,name,'clip changed')
    images=[{'name':i.name,'packed':bool(i.packed_file),'path':i.filepath} for i in bpy.data.images if i.type=='IMAGE' and i.source=='FILE' and i.users]
    assert all(i['packed'] or Path(bpy.path.abspath(i['path'])).exists() for i in images),(key,'missing texture')
    results.append({'key':key,'native_file_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'source_sha256_unchanged':True,'Blue_Red_fixed_regions_identical':True,'all_source_mesh_signatures_preserved':True,'rigs_preserved':True,'original_actions_preserved':len(r['original_animation_signatures']),'three_tone_materials_checked':len(tones),'original_LODs_preserved':True,'images':images,'shield_top_checks':lamp_groups,'checks':checks})
    print('NATIVE_SAVED_READBACK_OK',key,flush=True)
out={'models':14,'variants':28,'model_checks':results,'all_native_save_readback_checks_passed':True,'UE_updated':False,'B_approval':'pending'}
(c.O/'Reports/native-save-readback.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf8')
print('NATIVE_SAVE_READBACK_14_28_COMPLETE',flush=True)
