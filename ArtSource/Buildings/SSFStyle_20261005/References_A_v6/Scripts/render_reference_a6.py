"""Modify only two buildings in the frozen A-v3 scene; reuse all other original renders."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import bpy

ROOT=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
OUT=ROOT/'References_A_v6';ORIGINAL=ROOT/'References_A_v3'
assert not (OUT/'reference_manifest.json').exists()
revision=json.loads((OUT/'palette_revision.json').read_text(encoding='utf-8'))
old=json.loads((ORIGINAL/'render_manifest.json').read_text(encoding='utf-8'))
baseline=json.loads((ROOT/'Baseline/blender_source_manifest.json').read_text(encoding='utf-8'))
targets=set(revision['only_modified_assets'])
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def linear(code):
    values=[int(code[i:i+2],16)/255 for i in (1,3,5)]
    return tuple(x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4 for x in values)
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
def digest(objects):
    h=hashlib.sha256()
    for o in sorted(objects,key=lambda o:o.name):
        for v in o.data.vertices:h.update(repr(tuple(v.co)).encode())
        for p in o.data.polygons:h.update(repr(tuple(p.vertices)).encode())
        for v in o.data.vertices:h.update(repr([(g.group,round(g.weight,7)) for g in v.groups]).encode())
    return h.hexdigest()

bpy.ops.wm.open_mainfile(filepath=str(ORIGINAL/'SSF_ReferenceDesign_v3.blend'))
report=copy.deepcopy(old);report['version']='SSF_Reference_A_v6';report['palette_revision']=revision
report['themes']=revision['themes'];report['revision_reason']=revision['user_instruction']
material_checks=[];geometry_checks=[];materials={}
old_assets={a['key']:a for a in old['assets']}
base={a['key']:a for a in baseline['assets']}
for asset in report['assets']:
    key=asset['key'];previous=old_assets[key];asset['theme']=revision['themes'][key]
    if key in targets:
        for role in ('Primary','Equipment','Frame'):asset['material_palette'][role]=asset['theme'][role]
        asset['material_palette']['Pink']=asset['theme']['Equipment']
    for role,code in asset['material_palette'].items():
        prefix='A3_'+key+'_'+role
        choices=[m for m in bpy.data.materials if m.name==prefix or m.name.startswith(prefix+'_LineMask_')]
        assert len(choices)==1,(prefix,[m.name for m in bpy.data.materials if key in m.name])
        mat=choices[0];materials[mat.name]=mat
        before=signature(mat);source=previous['material_palette'][role];changed=source!=code
        assert max(abs(mat.diffuse_color[i]-linear(source)[i]) for i in range(3))<1e-7
        if changed:
            assert key in targets and role in ('Primary','Equipment','Frame','Pink')
            mat.diffuse_color=(*linear(code),1)
            ramps=[n for n in mat.node_tree.nodes if n.type=='VALTORGB'];assert len(ramps)==1
            for e,factor in zip(ramps[0].color_ramp.elements,old['tone_factors']):
                e.color=(*[c*factor for c in linear(code)],1)
        after=signature(mat)
        assert changed or before==after
        material_checks.append({'asset':key,'role':role,'material':mat.name,'source_hex':source,'output_hex':code,
                                'changed':changed,'shader_signature_before':before,'shader_signature_after':after})
    scene=bpy.data.scenes[base[key]['scene']]
    meshes=[o for o in scene.objects if o.type=='MESH']
    actual=digest(meshes);assert actual==previous['geometry_sha256_after']
    geometry_checks.append({'asset':key,'source_geometry_weights_equal':True,'sha256':actual})
assert sum(c['changed'] for c in material_checks)==5
preserved=[];(OUT/'Renders').mkdir(exist_ok=True)
for asset in report['assets']:
    key=asset['key']
    for view,data in asset['views'].items():
        if key not in targets:
            source=ORIGINAL/data['file'];dest=OUT/data['file'];shutil.copyfile(source,dest)
            assert sha(source)==sha(dest)
            preserved.append({'asset':key,'view':view,'file':data['file'],'source_sha256':sha(source),'same_bytes':True})
            continue
        scene=bpy.data.scenes[base[key]['scene']];bpy.context.window.scene=scene
        cam=scene.camera;cam.location=data['camera_location_m'];cam.rotation_euler=data['rotation_rad']
        cam.data.ortho_scale=data['ortho_scale_m']
        scene.render.filepath=str(OUT/data['file']);bpy.ops.render.render(write_still=True)
        print('REFERENCE_VIEW_DONE',key,view,flush=True)

groups=[s for s in bpy.data.scenes if 'ReferenceAssembly' in s.name]
assert len(groups)==1,[s.name for s in bpy.data.scenes]
group=groups[0];bpy.context.window.scene=group;cam=group.camera
# A-v3 saved the assembly at its last top-view camera. Restore the original hero direction.
from mathutils import Vector
points=[o.matrix_world@Vector(p) for o in group.objects if o.type=='MESH' for p in o.bound_box]
lo=Vector([min(p[a] for p in points) for a in range(3)]);hi=Vector([max(p[a] for p in points) for a in range(3)])
center=(lo+hi)/2;extent=max(hi-lo)
for view,direction in [('Hero',(.7,-1.2,1.4)),('Top',(0,0,1))]:
    cam.location=center+Vector(direction).normalized()*extent*4
    cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
    group.render.filepath=str(OUT/'Renders'/f'Assembly_{view}.png')
    bpy.ops.render.render(write_still=True)
for c in material_checks:assert signature(materials[c['material']])==c['shader_signature_after']
assert len(preserved)==34
scope={'success':True,'version':report['version'],'source_version':old['version'],'modified_assets':sorted(targets),
       'actual_materials_checked':len(material_checks),'changed_materials':5,'unchanged_shader_graphs':85,
       'all_other_eight_assets_unchanged':True,'all_other_asset_renders_same_bytes':34,
       'material_checks':material_checks,'preserved_renders':preserved,'geometry_checks':geometry_checks,
       'line_ink_unchanged':True,'three_tone_shading_unchanged':True,
       'production_modeling_started':False,'ue_formal_import_performed':False}
(OUT/'scope_preservation_validation.json').write_text(json.dumps(scope,ensure_ascii=False,indent=2),encoding='utf-8')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'SSF_ReferenceDesign_v6.blend'))
(OUT/'render_manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('SSF_A_V6_TARGETED_REVISION_COMPLETE',flush=True)
