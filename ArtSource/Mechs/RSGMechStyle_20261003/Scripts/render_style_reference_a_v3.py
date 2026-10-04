"""A-v3 reference palette only: sky-blue rear ring and pale-yellow head dome."""
import bpy
import copy
import hashlib
import json
from pathlib import Path

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT=ROOT/'References_A_v3'
OUT.mkdir(parents=True,exist_ok=True)
if (OUT/'reference_manifest.json').exists():
    raise RuntimeError('A-v3 already frozen; create a new sibling version.')
scene=bpy.context.scene
meshes=[o for o in scene.objects if o.type=='MESH']
old=json.loads((ROOT/'References_A_v2/reference_setup.json').read_text(encoding='utf-8'))
setup=copy.deepcopy(old)
parts=json.loads((ROOT/'Baseline/source_connected_parts.json').read_text())
assignments={a['component']:a for a in setup['component_color_assignments']}


def digest(obj):
    data={'name':obj.name,'matrix_world':[list(r) for r in obj.matrix_world],
          'vertices':[list(v.co) for v in obj.data.vertices],
          'polygons':[list(p.vertices) for p in obj.data.polygons]}
    return hashlib.sha256(json.dumps(data,separators=(',',':')).encode()).hexdigest()


before={o.name:digest(o) for o in meshes}
assert before==old['geometry_digest_after'],'Input geometry differs from A-v2'
hexcolor='#F4E4A1'
srgb=[int(hexcolor[i:i+2],16)/255 for i in (1,3,5)]
linear=[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in srgb]
yellow=bpy.data.materials['Reference_WarmWhite'].copy()
yellow.name='Reference_PaleYellow'
yellow.diffuse_color=(*linear,1)
for node in yellow.node_tree.nodes:
    if node.type=='VALTORGB':
        for element,factor in zip(node.color_ramp.elements,(.42,.74,1)):
            element.color=(*[v*factor for v in linear],1)
indices={}
for obj in meshes:
    obj.data.materials.append(yellow)
    indices[obj.name]={slot.name:index for index,slot in enumerate(obj.material_slots)}
changes=[]
for part in parts:
    cid=part['component']
    if cid not in (77,28551):
        continue
    color='SkyBlue' if cid==77 else 'PaleYellow'
    previous=assignments[cid]['base_color']
    assert previous=='WarmWhite', (cid,previous)
    obj=bpy.data.objects[part['object']]
    index=indices[obj.name]['Reference_'+color]
    for pi in part['polygon_ids']:
        obj.data.polygons[pi].material_index=index
    assignments[cid]['base_color']=color
    changes.append({'component':cid,'bone':part['dominant_bone'],
                    'region':'rear_circular_guard' if cid==77 else 'front_round_head_dome',
                    'from':previous,'to':color,
                    'srgb':'#87CEEB' if cid==77 else hexcolor,
                    'polygon_count':len(part['polygon_ids'])})
assert len(changes)==2
after={o.name:digest(o) for o in meshes}
assert before==after,'Palette revision changed geometry'
assert all(assignments[a['component']]['base_color']=='SkyBlue' for a in old['palette_revision'])
scene.render.resolution_x=scene.render.resolution_y=2048
scene.render.resolution_percentage=100
scene.render.use_freestyle=True
for view in ('Front','Left','Back','Hero'):
    pose=old['cameras'][view]
    scene.camera.location=pose['location_m']
    scene.camera.rotation_euler=pose['rotation_radians']
    scene.camera.data.type='ORTHO'
    scene.camera.data.ortho_scale=pose['ortho_scale_m']
    scene.render.filepath=str(OUT/f'RSG_A_v3_{view}_SourceStyle.png')
    bpy.ops.render.render(write_still=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'RSG_A_v3_ReferenceMaterialStudy.blend'))
setup.update({'version':'A-v3','supersedes_reference':'A-v2',
              'user_revision_request':'背后的环也换成天蓝色，头部的圆球换成浅黄色',
              'palette_revision':changes,'retained_A_v2_blue_parts':old['palette_revision'],
              'geometry_digest_before':before,'geometry_digest_after':after,
              'geometry_changed':False,'same_cameras_as_A_v2':True,
              'A_approval':'pending','B_approval':'not_started'})
setup['palette_srgb']['PaleYellow']=hexcolor
(OUT/'reference_setup.json').write_text(json.dumps(setup,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'version':'A-v3','rear_ring':'#87CEEB','head_dome':hexcolor,
                  'geometry_unchanged':before==after,'previous_blue_parts_retained':8,
                  'images_px':[2048,2048],'A':'pending'},ensure_ascii=False),flush=True)
