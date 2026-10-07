"""Read frozen A_v6 and render isolated line/tone evidence without saving or editing it."""
from pathlib import Path
import hashlib
import json
import shutil
import bpy

ROOT=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
SOURCE=ROOT/'References_A_v6'
OUT=ROOT/'StyleAudit_A_v6'
OUT.mkdir(exist_ok=True)
(OUT/'Renders').mkdir(exist_ok=True)
scene_file=SOURCE/'SSF_ReferenceDesign_v6.blend'
before_sha=hashlib.sha256(scene_file.read_bytes()).hexdigest()
bpy.ops.wm.open_mainfile(filepath=str(scene_file))
manifest=json.loads((SOURCE/'render_manifest.json').read_text(encoding='utf-8'))
baseline=json.loads((ROOT/'Baseline/blender_source_manifest.json').read_text(encoding='utf-8'))

checks=[]
for asset in manifest['assets']:
    for role,code in asset['material_palette'].items():
        prefix='A3_'+asset['key']+'_'+role
        choices=[m for m in bpy.data.materials if m.name==prefix or m.name.startswith(prefix+'_LineMask_')]
        assert len(choices)==1,prefix
        mat=choices[0]
        ramps=[n for n in mat.node_tree.nodes if n.type=='VALTORGB']
        assert len(ramps)==1,mat.name
        ramp=ramps[0]
        fac_links=list(ramp.inputs['Fac'].links)
        assert len(fac_links)==1 and fac_links[0].from_node.type=='VECT_MATH'
        dot=fac_links[0].from_node
        assert dot.operation=='DOT_PRODUCT'
        assert dot.inputs[0].links[0].from_node.type=='NEW_GEOMETRY'
        assert dot.inputs[0].links[0].from_socket.name=='Normal'
        assert len(ramp.color_ramp.elements)==3 and ramp.color_ramp.interpolation=='CONSTANT'
        errors=[]
        for element,factor in zip(ramp.color_ramp.elements,manifest['tone_factors']):
            errors.append(max(abs(element.color[i]-mat.diffuse_color[i]*factor) for i in range(3)))
        assert max(errors)<1e-7
        checks.append({'asset':asset['key'],'role':role,'material':mat.name,'palette_hex':code,
                       'normal_dot_ramp_connected':True,'ramp_interpolation':ramp.color_ramp.interpolation,
                       'thresholds':[e.position for e in ramp.color_ramp.elements],
                       'factors':manifest['tone_factors'],'factors_max_error':max(errors),
                       'independent_mask_nodes':[n.name for n in mat.node_tree.nodes if n.type=='TEX_IMAGE']})
scenes=[]
for asset in baseline['assets']:
    scene=bpy.data.scenes[asset['scene']]
    fs=scene.view_layers[0].freestyle_settings
    scenes.append({'asset':asset['key'],'render_engine':scene.render.engine,
                   'freestyle_enabled':scene.render.use_freestyle,
                   'line_sets':[{'name':ls.name,'enabled':ls.show_render,'thickness_px':ls.linestyle.thickness,
                                 'silhouette':ls.select_silhouette,'external_contour':ls.select_external_contour,
                                 'crease':ls.select_crease,'material_boundary':ls.select_material_boundary}
                                for ls in fs.linesets]})

key='MilitaryFactory'
asset=next(a for a in manifest['assets'] if a['key']==key)
scene=bpy.data.scenes[next(a['scene'] for a in baseline['assets'] if a['key']==key)]
bpy.context.window.scene=scene
view=asset['views']['Hero'];cam=scene.camera
cam.location=view['camera_location_m'];cam.rotation_euler=view['rotation_rad']
cam.data.ortho_scale=view['ortho_scale_m']
scene.render.resolution_x=scene.render.resolution_y=2048
scene.render.resolution_percentage=100
mats=[]
for role in asset['material_palette']:
    prefix='A3_'+key+'_'+role
    mat=next(m for m in bpy.data.materials if m.name==prefix or m.name.startswith(prefix+'_LineMask_'))
    ramp=next(n for n in mat.node_tree.nodes if n.type=='VALTORGB')
    emission=next(n for n in mat.node_tree.nodes if n.type=='EMISSION')
    original_socket=emission.inputs['Color'].links[0].from_socket
    mats.append((mat,ramp,emission,original_socket,[tuple(e.color) for e in ramp.color_ramp.elements]))

shutil.copyfile(SOURCE/view['file'],OUT/'Renders/MilitaryFactory_A_v6_Current.png')
for mode in ('ToneOnly','LineOnly','BandIDs','FlatColor'):
    scene.render.use_freestyle=(mode=='LineOnly')
    for mat,ramp,emission,original_socket,original_colors in mats:
        nt=mat.node_tree
        for link in list(emission.inputs['Color'].links):nt.links.remove(link)
        nt.links.new(original_socket if mode=='LineOnly' else ramp.outputs['Color'],emission.inputs['Color'])
        for i,element in enumerate(ramp.color_ramp.elements):
            if mode=='ToneOnly':element.color=original_colors[i]
            elif mode=='LineOnly':element.color=(1,1,1,1)
            elif mode=='FlatColor':element.color=mat.diffuse_color
            else:element.color=((0,0,1,1),(0,1,0,1),(1,0,0,1))[i]
    scene.render.filepath=str(OUT/'Renders'/f'MilitaryFactory_{mode}.png')
    bpy.ops.render.render(write_still=True)
    print('STYLE_DIAGNOSTIC_RENDER_DONE',mode,flush=True)

for mat,ramp,emission,original_socket,original_colors in mats:
    for link in list(emission.inputs['Color'].links):mat.node_tree.links.remove(link)
    mat.node_tree.links.new(original_socket,emission.inputs['Color'])
    for element,color in zip(ramp.color_ramp.elements,original_colors):element.color=color
assert hashlib.sha256(scene_file.read_bytes()).hexdigest()==before_sha
report={'version':'SSF_StyleAudit_A_v6','source_version':'SSF_Reference_A_v6',
        'source_blend_sha256':before_sha,'source_file_unchanged':True,
        'actual_materials_checked':len(checks),'three_discrete_bands_connected':True,
        'tone_factors':manifest['tone_factors'],'tone_thresholds':manifest['tone_thresholds'],
        'shared_ink':manifest['shared_ink'],'material_checks':checks,'scene_line_checks':scenes,
        'demonstration_asset':key,'camera':view,'render_modes':['Current','ToneOnly','LineOnly','BandIDs','FlatColor'],
        'reference_only':True,'production_outline_shell_created':False,'formal_UE_material_created':False,
        'source_geometry_or_palette_modified':False,'A':'pending','B':'not_started'}
(OUT/'native_node_audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('SSF_A_V6_NATIVE_LINE_TONE_AUDIT_DONE',len(checks),flush=True)
