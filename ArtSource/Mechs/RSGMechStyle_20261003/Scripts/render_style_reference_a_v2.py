"""A-v2 reference-only palette revision: a little sky blue on eight source parts."""
import bpy
import copy
import hashlib
import json
from pathlib import Path
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT = ROOT / 'References_A_v2'
OUT.mkdir(parents=True, exist_ok=True)
if (OUT/'reference_manifest.json').exists():
    raise RuntimeError('A-v2 already published; create a new sibling version before editing.')
scene = bpy.context.scene
meshes = [o for o in scene.objects if o.type == 'MESH']
old = json.loads((ROOT/'References_A_v1/reference_setup.json').read_text(encoding='utf-8'))
setup = copy.deepcopy(old)
parts = json.loads((ROOT/'Baseline/source_connected_parts.json').read_text(encoding='utf-8'))
assignments = {a['component']: a for a in setup['component_color_assignments']}


def geometry_digest(obj):
    content = {'name': obj.name, 'matrix_world': [list(r) for r in obj.matrix_world],
               'vertices': [list(v.co) for v in obj.data.vertices],
               'polygons': [list(p.vertices) for p in obj.data.polygons]}
    return hashlib.sha256(json.dumps(content, separators=(',',':')).encode()).hexdigest()


before = {o.name: geometry_digest(o) for o in meshes}
hexcolor = '#87CEEB'
srgb = [int(hexcolor[i:i+2],16)/255 for i in (1,3,5)]
linear = [v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in srgb]
sky = bpy.data.materials['Reference_Coral'].copy()
sky.name = 'Reference_SkyBlue'
sky.diffuse_color = (*linear, 1)
for node in sky.node_tree.nodes:
    if node.type == 'VALTORGB':
        for element,factor in zip(node.color_ramp.elements, (.42,.74,1)):
            element.color = (*[v*factor for v in linear], 1)
indices = {}
for obj in meshes:
    indices[obj.name] = len(obj.data.materials)
    obj.data.materials.append(sky)

changes = []
for part in parts:
    bone = part['dominant_bone'] or ''
    dx,dy,dz = part['dimensions_m']
    x,y,z = part['center_m']
    previous = assignments[part['component']]['base_color']
    shin_insert = (bone.startswith(('FrontLeg','MiddleLeg','BackLeg')) and 'Leg3_' in bone
                   and .84<dz<.96 and part['vertex_count']>200 and previous=='Coral')
    gun_cover = (bone.startswith('ShotgunTop') and dx>.45 and dy>.7 and dz>.8
                 and y>-2 and previous=='Coral')
    if not (shin_insert or gun_cover):
        continue
    obj = bpy.data.objects[part['object']]
    for pi in part['polygon_ids']:
        obj.data.polygons[pi].material_index = indices[obj.name]
    assignments[part['component']]['base_color'] = 'SkyBlue'
    changes.append({'component': part['component'], 'bone': bone,
                    'region': 'shin_service_insert' if shin_insert else 'upper_weapon_outboard_cover',
                    'from': previous, 'to': 'SkyBlue', 'srgb': hexcolor,
                    'polygon_count': len(part['polygon_ids'])})
assert len(changes)==8, changes
assert sum(c['region']=='shin_service_insert' for c in changes)==6
assert sum(c['region']=='upper_weapon_outboard_cover' for c in changes)==2
for p in [p for p in parts if p['component'] in {c['component'] for c in changes}]:
    x,y,z = p['center_m']
    side = '_R' if p['dominant_bone'].endswith('_L') else '_L'
    peer_bone = p['dominant_bone'][:-2]+side
    mirrored = [q for q in parts if q['dominant_bone']==peer_bone
                and max(abs(a-b) for a,b in zip(q['center_m'],(-x,y,z)))<.002]
    assert len(mirrored)==1 and assignments[mirrored[0]['component']]['base_color']=='SkyBlue'

after = {o.name: geometry_digest(o) for o in meshes}
assert before==after, 'Palette revision changed source geometry'
scene.render.resolution_x = scene.render.resolution_y = 2048
scene.render.resolution_percentage = 100
scene.render.use_freestyle = True
camera = scene.camera
for view in ('Front','Left','Back','Hero'):
    pose = old['cameras'][view]
    camera.location = pose['location_m']
    camera.rotation_euler = pose['rotation_radians']
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = pose['ortho_scale_m']
    scene.render.filepath = str(OUT/f'RSG_A_v2_{view}_SourceStyle.png')
    bpy.ops.render.render(write_still=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'RSG_A_v2_ReferenceMaterialStudy.blend'))
setup.update({'version':'A-v2', 'supersedes_reference':'A-v1',
              'user_revision_request':'加一点天蓝色', 'palette_revision':changes,
              'geometry_digest_before':before, 'geometry_digest_after':after,
              'geometry_changed':False, 'same_cameras_as_A_v1':True,
              'A_approval':'pending', 'B_approval':'not_started'})
setup['palette_srgb']['SkyBlue'] = hexcolor
(OUT/'reference_setup.json').write_text(json.dumps(setup,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'version':'A-v2','sky_blue':hexcolor,'changed_parts':len(changes),
                  'geometry_unchanged':before==after,'four_images_px':[2048,2048],
                  'A':'pending'},ensure_ascii=False),flush=True)
