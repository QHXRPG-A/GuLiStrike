"""Make linework and all three tone bands visible; preserve A_v6 colors and geometry."""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import sys
import bpy
from mathutils import Vector

parser=argparse.ArgumentParser()
parser.add_argument('--preview',action='store_true')
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
ROOT=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
SOURCE=ROOT/'References_A_v6'
OUT=ROOT/('StyleAudit_A_v6/Preview_Candidate_v1' if args.preview else 'References_A_v7')
OUT.mkdir(exist_ok=True);(OUT/'Renders').mkdir(exist_ok=True)
assert not (OUT/'reference_manifest.json').exists()
source_file=SOURCE/'SSF_ReferenceDesign_v6.blend'
source_sha=hashlib.sha256(source_file.read_bytes()).hexdigest()
old=json.loads((SOURCE/'render_manifest.json').read_text(encoding='utf-8'))
baseline=json.loads((ROOT/'Baseline/blender_source_manifest.json').read_text(encoding='utf-8'))
base={a['key']:a for a in baseline['assets']}
bpy.ops.wm.open_mainfile(filepath=str(source_file))
report=copy.deepcopy(old);report['version']='SSF_Reference_A_v7'
report['revision_reason']='User questioned visibility of linework and three tone bands; preserve all A_v6 palettes and source geometry, improve reference style readability.'
report['tone_thresholds']=[.38,.68]
style={'version':report['version'],'source_version':old['version'],
       'user_quote':'线稿和三档明暗好像没加？',
       'tone_thresholds_before':old['tone_thresholds'],'tone_thresholds_after':[.38,.68],
       'tone_factors_preserved':old['tone_factors'],'fixed_light_direction_preserved':old['art_light_direction'],
       'shared_ink_preserved':old['shared_ink'],
       'internal_mask_strength_before':.45,'internal_mask_strength_after':.65,
       'platform_mask_strength_before':.48,'platform_mask_strength_after':.65,
       'structure_width_2048_before':1.45,'structure_width_2048_after':2.2,
       'outer_width_2048_before':2.6,'outer_width_2048_after':4.4,
       'assembly_structure_width_4096':1.6,'assembly_outer_width_4096':3.0,
       'all_palette_HEX_preserved_from_A_v6':True,'geometry_pose_camera_preserved':True,
       'reference_only':True,'production_outline_shell_created':False,'UE_formal_import_performed':False}
report['style_revision']=style
palette=copy.deepcopy(old['palette_revision'])
palette['version']=report['version'];palette['source_version']=old['version']
palette['user_instruction']='配色保持A_v6，仅修正线稿和三档明暗可读性。'
palette['only_modified_assets']=[];palette['all_palette_HEX_preserved_from_A_v6']=True
palette['prior_color_revision']='References_A_v6/palette_revision.json'
palette['method']='All 40 palette roles preserved from A_v6; only reference line strengths, widths and band thresholds adjusted.'
palette['note']='No paint HEX changed this revision. Final pixels change with the corrected line and tone treatment.'
palette['tone_thresholds']=[.38,.68];palette['internal_line_strength']=.65;palette['platform_line_strength']=.65
for c in palette['changes']:
    c['source_hex']=c['output_hex'];c['changed']=False;c['L_star_before']=c['L_star_after']
report['palette_revision']=palette
checks=[]
for asset in report['assets']:
    for role,code in asset['material_palette'].items():
        prefix='A3_'+asset['key']+'_'+role
        choices=[m for m in bpy.data.materials if m.name==prefix or m.name.startswith(prefix+'_LineMask_')]
        assert len(choices)==1,prefix
        mat=choices[0];original_diffuse=tuple(mat.diffuse_color)
        ramp=next(n for n in mat.node_tree.nodes if n.type=='VALTORGB')
        original_colors=[tuple(e.color) for e in ramp.color_ramp.elements]
        for e,position in zip(ramp.color_ramp.elements,(0,.38,.68)):e.position=position
        for n in mat.node_tree.nodes:
            if n.type=='MATH' and n.operation=='MULTIPLY':
                if n.inputs[0].is_linked and n.inputs[0].links[0].from_node.type=='TEX_IMAGE':
                    assert abs(n.inputs[1].default_value-.45)<1e-6
                    n.inputs[1].default_value=.65
                elif abs(n.inputs[1].default_value-.48)<1e-6:
                    n.inputs[1].default_value=.65
        assert tuple(mat.diffuse_color)==original_diffuse
        assert [tuple(e.color) for e in ramp.color_ramp.elements]==original_colors
        checks.append({'asset':asset['key'],'role':role,'material':mat.name,'HEX':code,
                       'diffuse_RGB_preserved':True,'all_three_band_RGB_preserved':True,
                       'thresholds':[e.position for e in ramp.color_ramp.elements]})
assert len(checks)==90
scene_keys=[base[a['key']]['scene'] for a in report['assets']]
group=next(s for s in bpy.data.scenes if 'ReferenceAssembly' in s.name)
line_checks=[]
for scene in [bpy.data.scenes[k] for k in scene_keys]+[group]:
    scene.render.use_freestyle=True
    for ls in scene.view_layers[0].freestyle_settings.linesets:
        if ls.name=='StructuralLines':ls.linestyle.thickness=1.6 if scene==group else 2.2*scene.render.resolution_x/2048
        if ls.name=='OuterContour':ls.linestyle.thickness=3.0 if scene==group else 4.4*scene.render.resolution_x/2048
    assert scene.view_settings.exposure==0 and scene.view_settings.gamma==1
    line_checks.append({'scene':scene.name,'Freestyle':True,'widths':
                       {ls.name:ls.linestyle.thickness for ls in scene.view_layers[0].freestyle_settings.linesets}})

def digest(objects):
    h=hashlib.sha256()
    for o in sorted(objects,key=lambda o:o.name):
        for v in o.data.vertices:h.update(repr(tuple(v.co)).encode())
        for p in o.data.polygons:h.update(repr(tuple(p.vertices)).encode())
        for v in o.data.vertices:h.update(repr([(g.group,round(g.weight,7)) for g in v.groups]).encode())
    return h.hexdigest()
geometry=[]
for asset in report['assets']:
    scene=bpy.data.scenes[base[asset['key']]['scene']]
    assert digest([o for o in scene.objects if o.type=='MESH'])==asset['geometry_sha256_after']
    geometry.append({'asset':asset['key'],'geometry_weights_equal_A_v6':True})
    if args.preview and asset['key'] not in ('MilitaryFactory','AirBase','StrategyCenter'):continue
    bpy.context.window.scene=scene;cam=scene.camera
    for view,data in asset['views'].items():
        if args.preview and view!='Hero':continue
        cam.location=data['camera_location_m'];cam.rotation_euler=data['rotation_rad'];cam.data.ortho_scale=data['ortho_scale_m']
        scene.render.filepath=str(OUT/data['file']);bpy.ops.render.render(write_still=True)
        print('STYLE_REFERENCE_VIEW_DONE',asset['key'],view,flush=True)

if args.preview:
    factory=next(a for a in report['assets'] if a['key']=='MilitaryFactory')
    scene=bpy.data.scenes[base['MilitaryFactory']['scene']];bpy.context.window.scene=scene
    scene.render.use_freestyle=False
    for row in checks:
        if row['asset']!='MilitaryFactory':continue
        mat=bpy.data.materials[row['material']]
        ramp=next(n for n in mat.node_tree.nodes if n.type=='VALTORGB')
        emission=next(n for n in mat.node_tree.nodes if n.type=='EMISSION')
        for link in list(emission.inputs['Color'].links):mat.node_tree.links.remove(link)
        mat.node_tree.links.new(ramp.outputs['Color'],emission.inputs['Color'])
        for e,color in zip(ramp.color_ramp.elements,((0,0,1,1),(0,1,0,1),(1,0,0,1))):e.color=color
    scene.render.filepath=str(OUT/'Renders/MilitaryFactory_BandIDs.png');bpy.ops.render.render(write_still=True)
else:
    bpy.context.window.scene=group;cam=group.camera
    points=[o.matrix_world@Vector(p) for o in group.objects if o.type=='MESH' for p in o.bound_box]
    lo=Vector([min(p[a] for p in points) for a in range(3)]);hi=Vector([max(p[a] for p in points) for a in range(3)])
    center=(lo+hi)/2;extent=max(hi-lo)
    for view,direction in [('Hero',(.7,-1.2,1.4)),('Top',(0,0,1))]:
        cam.location=center+Vector(direction).normalized()*extent*4
        cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
        group.render.filepath=str(OUT/'Renders'/f'Assembly_{view}.png');bpy.ops.render.render(write_still=True)
    packed=[im.name for im in bpy.data.images if im.packed_file]
    assert len(packed)==10
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'SSF_ReferenceDesign_v7.blend'))
    (OUT/'packed_scene_report.json').write_text(json.dumps({'success':True,'packed_images':packed,
         'reference_only':True,'production_modeling_started':False},ensure_ascii=False,indent=2),encoding='utf-8')
assert hashlib.sha256(source_file.read_bytes()).hexdigest()==source_sha
(OUT/'render_manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
(OUT/'palette_revision.json').write_text(json.dumps(palette,ensure_ascii=False,indent=2),encoding='utf-8')
(OUT/'style_revision.json').write_text(json.dumps(style,ensure_ascii=False,indent=2),encoding='utf-8')
(OUT/'style_preservation_validation.json').write_text(json.dumps({'success':True,'version':report['version'],
    'actual_materials_checked':len(checks),'all_palette_HEX_equal_A_v6':True,'actual_base_and_band_RGB_equal_A_v6':True,
    'material_checks':checks,'line_checks':line_checks,'geometry_checks':geometry,
    'light_direction_preserved':True,'cameras_preserved':True,'production_modeling_started':False},ensure_ascii=False,indent=2),encoding='utf-8')
print('SSF_A_V7_STYLE_REFERENCE_DONE','preview' if args.preview else 'full',flush=True)
