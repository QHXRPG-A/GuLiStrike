"""Reopen the saved A_v7 scene and compare actual base/band RGB with frozen A_v6."""
from pathlib import Path
import hashlib
import json
import bpy
ROOT=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
OUT=ROOT/'References_A_v7'
assert not (OUT/'reference_manifest.json').exists()
old=json.loads((ROOT/'References_A_v6/render_manifest.json').read_text(encoding='utf-8'))
source_file=ROOT/'References_A_v6/SSF_ReferenceDesign_v6.blend'
before=hashlib.sha256(source_file.read_bytes()).hexdigest()
original={}
bpy.ops.wm.open_mainfile(filepath=str(source_file))
for asset in old['assets']:
    for role,code in asset['material_palette'].items():
        prefix='A3_'+asset['key']+'_'+role
        mat=next(m for m in bpy.data.materials if m.name==prefix or m.name.startswith(prefix+'_LineMask_'))
        ramp=next(n for n in mat.node_tree.nodes if n.type=='VALTORGB')
        dot=ramp.inputs['Fac'].links[0].from_node
        original[mat.name]={'asset':asset['key'],'role':role,'HEX':code,'diffuse':tuple(mat.diffuse_color),
                            'bands':[tuple(e.color) for e in ramp.color_ramp.elements],
                            'light_direction':tuple(dot.inputs[1].default_value)}
bpy.ops.wm.open_mainfile(filepath=str(OUT/'SSF_ReferenceDesign_v7.blend'))
checks=[]
for name,previous in original.items():
    mat=bpy.data.materials[name];ramp=next(n for n in mat.node_tree.nodes if n.type=='VALTORGB')
    dot=ramp.inputs['Fac'].links[0].from_node
    base_error=max(abs(x-y) for x,y in zip(mat.diffuse_color,previous['diffuse']))
    band_error=max(abs(x-y) for e,p in zip(ramp.color_ramp.elements,previous['bands']) for x,y in zip(e.color,p))
    threshold_error=max(abs(e.position-p) for e,p in zip(ramp.color_ramp.elements,(0,.38,.68)))
    assert base_error==0 and band_error==0 and threshold_error<1e-7
    assert tuple(dot.inputs[1].default_value)==previous['light_direction']
    assert ramp.color_ramp.interpolation=='CONSTANT'
    checks.append({'material':name,'asset':previous['asset'],'role':previous['role'],'palette_hex':previous['HEX'],
                   'base_RGB_error':base_error,'band_RGB_error':band_error,'threshold_error':threshold_error,
                   'constant_three_band_ramp':True,'light_direction_unchanged':True})
assert len(checks)==90
packed=[im.name for im in bpy.data.images if im.packed_file]
assert len(packed)==10
assert all(s.view_settings.exposure==0 and s.view_settings.gamma==1 for s in bpy.data.scenes)
assert hashlib.sha256(source_file.read_bytes()).hexdigest()==before
report={'success':True,'version':'SSF_Reference_A_v7','source_version':old['version'],
        'saved_scene_reopened':True,'actual_materials_checked':90,'all_base_and_band_RGB_equal_A_v6':True,
        'new_thresholds':[.38,.68],'original_tone_factors_preserved':old['tone_factors'],
        'original_light_direction_preserved':True,'global_exposure':0,'global_gamma':1,
        'packed_images':packed,'source_blend_unchanged':True,'checks':checks,
        'reference_only':True,'production_modeling_started':False,'formal_UE_import_performed':False}
(OUT/'palette_preservation_validation.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('SAVED_A_V7_BASE_BAND_RGB_AND_DISCRETE_RAMPS_VERIFIED',90,flush=True)
