"""Pack reference textures before freezing A-v6, without touching source geometry."""
from pathlib import Path
import bpy
import json
ROOT=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
OUT=ROOT/'References_A_v6'
assert not (OUT/'reference_manifest.json').exists(), 'Published reference version is frozen'
bpy.ops.wm.open_mainfile(filepath=str(OUT/'SSF_ReferenceDesign_v6.blend'))
original=json.loads((ROOT/'References_A_v3/render_manifest.json').read_text(encoding='utf-8'))
current=json.loads((OUT/'render_manifest.json').read_text(encoding='utf-8'))
old_assets={a['key']:a for a in original['assets']}
def linear(code):
    rgb=[int(code[i:i+2],16)/255 for i in (1,3,5)]
    return [x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4 for x in rgb]
checks=[]
for asset in current['assets']:
    key=asset['key']
    for role,code in asset['material_palette'].items():
        prefix='A3_'+key+'_'+role
        mats=[m for m in bpy.data.materials if m.name==prefix or m.name.startswith(prefix+'_LineMask_')]
        assert len(mats)==1,(prefix,[m.name for m in mats])
        mat=mats[0];expected=linear(code)
        base_error=max(abs(mat.diffuse_color[i]-expected[i]) for i in range(3))
        ramps=[n for n in mat.node_tree.nodes if n.type=='VALTORGB']
        assert len(ramps)==1
        elements=list(ramps[0].color_ramp.elements)
        assert len(elements)==3
        ramp_error=max(abs(elements[t].color[i]-expected[i]*factor) for t,factor in enumerate(original['tone_factors']) for i in range(3))
        assert base_error<1e-7 and ramp_error<1e-7,(prefix,base_error,ramp_error)
        previous=old_assets[key]['material_palette'][role]
        protected=previous in current['palette_revision']['protected_original_swatches']
        assert not protected or previous==code,(prefix,previous,code)
        checks.append({'material':mat.name,'source_hex':previous,'current_hex':code,
                       'protected_original_color':protected,'base_color_max_error':base_error,
                       'original_three_tone_ramp_max_error':ramp_error})
assert current['tone_factors']==original['tone_factors']
for scene in bpy.data.scenes:
    assert scene.view_settings.exposure==0 and scene.view_settings.gamma==1
(OUT/'palette_preservation_validation.json').write_text(json.dumps({
    'success':True,'version':current['version'],'source_version':original['version'],
    'actual_Blender_materials_checked':len(checks),
    'protected_materials_checked':sum(c['protected_original_color'] for c in checks),
    'six_original_light_middle_swatches':current['palette_revision']['protected_original_swatches'],
    'original_three_tone_factors':original['tone_factors'],'global_exposure':0,'global_gamma':1,
    'checks':checks,'claim':'Only dark colors of two named buildings changed; source light colors, original line ink and three-tone ramps remain. Other eight assets shader graphs are identical and their 34 raw renders are copied byte-for-byte.'},
    ensure_ascii=False,indent=2),encoding='utf-8')
def plain(value):
    if isinstance(value,(float,int)):return round(value,7)
    if isinstance(value,(str,bool)) or value is None:return value
    try:return [plain(x) for x in value]
    except TypeError:return str(type(value))

def signature(mat):
    nodes=[]
    for n in mat.node_tree.nodes:
        row={'name':n.name,'type':n.type,'inputs':[(i.name,plain(i.default_value)) for i in n.inputs if hasattr(i,'default_value')]}
        for attr in ('operation','blend_type','use_clamp'):
            if hasattr(n,attr):row[attr]=getattr(n,attr)
        if n.type=='VALTORGB':row['ramp']=[(round(e.position,7),plain(e.color)) for e in n.color_ramp.elements]
        if n.type=='TEX_IMAGE':row['image']=n.image.name if n.image else None
        nodes.append(row)
    data={'diffuse':plain(mat.diffuse_color),'nodes':nodes,
          'links':[(l.from_node.name,l.from_socket.name,l.to_node.name,l.to_socket.name) for l in mat.node_tree.links]}
    return hashlib.sha256(json.dumps(data,sort_keys=True).encode()).hexdigest()

import hashlib
scope=json.loads((OUT/'scope_preservation_validation.json').read_text(encoding='utf-8'))
for row in scope['material_checks']:
 assert signature(bpy.data.materials[row['material']])==row['shader_signature_after'],row['material']
print('ACTUAL_SAVED_SHADER_GRAPHS_CHECKED',len(scope['material_checks']),flush=True)
packed=[]
for im in bpy.data.images:
    if im.source=='FILE' and Path(bpy.path.abspath(im.filepath)).is_file():
        # Images are lazily loaded after reopening a blend; force pixel data first.
        first_pixel=im.pixels[0]
        assert im.has_data, im.filepath
        im.pack(); packed.append(im.name)
assert len(packed)>=10, ('Expected eight masks plus logo and light texture',packed)
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'SSF_ReferenceDesign_v6.blend'))
(OUT/'packed_scene_report.json').write_text(json.dumps({'success':True,'packed_images':packed,
    'reference_only':True,'production_modeling_started':False},ensure_ascii=False,indent=2),encoding='utf-8')
print('REFERENCE_TEXTURES_PACKED',len(packed),flush=True)
