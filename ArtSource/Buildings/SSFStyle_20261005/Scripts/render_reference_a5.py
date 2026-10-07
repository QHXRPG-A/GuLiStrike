"""Source-constrained Stage A palette/line study. Never remesh, decimate or export production geometry."""
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import sys
import bpy
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
args = argparse.ArgumentParser()
args.add_argument('--draft', action='store_true')
args.add_argument('--assets', default='')
opts = args.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])
OUT = ROOT / ('Drafts_A_v5' if opts.draft else 'References_A_v5')
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'Renders').mkdir(exist_ok=True)
if not opts.draft and (OUT / 'reference_manifest.json').exists():
    raise RuntimeError('Published A-v5 is frozen. Use a new version for revisions.')
source = json.loads((ROOT / 'Source/source_manifest.json').read_text(encoding='utf-8'))
baseline = json.loads((ROOT / 'Baseline/blender_source_manifest.json').read_text(encoding='utf-8'))
bpy.ops.wm.open_mainfile(filepath=str(ROOT / 'Baseline/SSF_Untouched_Source_Baselines.blend'))
SIZE = 768 if opts.draft else 2048
SWATCHES = ['#1A182F','#45184D','#662249','#A34053','#EE9D58','#EABCBA',
            '#150017','#2A104B','#522C5C','#835061','#E3B6B1','#FEE4D9',
            '#021716','#032E30','#0A6F73','#0D9099','#6AA4BE','#274E61']
REVISION = json.loads((ROOT/'References_A_v5/palette_revision.json').read_text(encoding='utf-8'))
INK = REVISION['shared_ink']
THEMES = REVISION['themes']
assert all(c['source_hex'] in SWATCHES for c in REVISION['changes'])

def asset_palette(key):
    t = THEMES[key]
    # Historic component assignments use Orange/Berry as accent-role aliases.
    return {r:t[r] for r in ('Primary','Equipment','Frame')} | {
        'Orange':t['Accent'], 'Berry':t['Accent'], 'Pink':t['Equipment'],
        'Platform':REVISION['platform'], 'PlatformTop':THEMES['Floor']['Equipment'], 'Ink':INK}
# Explicit design assignments to complete source-connected components.
# All remaining choices below are tied to physical size/function, never random face colors.
OVERRIDES = {
 'AirBase': {'Equipment': [44, 48, 53, 58, 140, 145], 'Orange': [49, 54, 59, 45, 9]},
 'CloningCenter': {'Primary': [10, 14, 17, 25, 38], 'Equipment': [0, 8, 19, 11, 23],
                   'Berry': [1, 9, 46], 'Frame': [13, 18, 27, 28]},
 'CommandCenter': {'Primary': [92, 95], 'Orange': [3, 4], 'Equipment': [57, 60, 61, 64, 67, 68, 116, 117]},
 'MilitaryFactory': {'Primary': [1], 'Equipment': [0, 27, 30, 31, 32, 33, 34, 35, 37], 'Orange': [36]},
 'Reactor': {'Primary': [10, 24, 38], 'Equipment': [6, 20, 34, 48, 50, 52, 54],
             'Orange': [49], 'Frame': [2, 3, 4]},
 'StrategyCenter': {'Primary': [54, 131], 'Equipment': [55, 145, 148, 154, 157, 178, 179, 180, 181],
                    'Berry': [56, 57, 173]},
 'Drone': {'Primary': [6, 8], 'Equipment': [0, 1, 3], 'Orange': [2, 4, 5], 'Frame': [7]},
 'Lamp': {'Frame': [0, 1], 'Orange': [2]},
 'Floor': {'Platform': [1, 2], 'Orange': [3, 86, 95]},
}

def linear(code):
    rgb = [int(code[i:i+2], 16) / 255 for i in (1, 3, 5)]
    return tuple(x / 12.92 if x <= .04045 else ((x + .055) / 1.055) ** 2.4 for x in rgb)

def toon(key, role, palette, line_path=None):
    mat = bpy.data.materials.new('A5_' + key + '_' + role + ('_LineMask_' + line_path.stem if line_path else ''))
    mat.use_nodes = True; mat.diffuse_color = (*linear(palette[role]), 1)
    nt = mat.node_tree; nt.nodes.clear()
    output = nt.nodes.new('ShaderNodeOutputMaterial'); em = nt.nodes.new('ShaderNodeEmission')
    geom = nt.nodes.new('ShaderNodeNewGeometry'); dot = nt.nodes.new('ShaderNodeVectorMath')
    dot.operation = 'DOT_PRODUCT'; dot.inputs[1].default_value = Vector((.35, -.55, .76)).normalized()
    ramp = nt.nodes.new('ShaderNodeValToRGB'); ramp.color_ramp.interpolation = 'CONSTANT'
    cr = ramp.color_ramp; cr.elements.remove(cr.elements[1]); color = linear(palette[role])
    for i, (threshold, factor) in enumerate(zip((0,.12,.55),REVISION['tone_factors'])):
        e = cr.elements[0] if i == 0 else cr.elements.new(threshold)
        e.position = threshold; e.color = (*[c * factor for c in color], 1)
    nt.links.new(geom.outputs['Normal'], dot.inputs[0]); nt.links.new(dot.outputs['Value'], ramp.inputs['Fac'])
    surface = ramp.outputs['Color']
    if line_path and line_path.exists():
        tex = nt.nodes.new('ShaderNodeTexImage'); tex.name = 'Independent_Reference_LineMask'
        tex.image = bpy.data.images.load(str(line_path), check_existing=True)
        tex.image.colorspace_settings.name = 'Non-Color'
        scale = nt.nodes.new('ShaderNodeMath'); scale.operation = 'MULTIPLY'; scale.inputs[1].default_value = REVISION['internal_line_strength']
        mix = nt.nodes.new('ShaderNodeMixRGB'); mix.blend_type = 'MIX'
        mix.inputs[2].default_value = (*linear(INK), 1)
        nt.links.new(tex.outputs['Color'], scale.inputs[0]); nt.links.new(scale.outputs[0], mix.inputs[0])
        nt.links.new(surface, mix.inputs[1]); surface = mix.outputs[0]
    if role == 'Platform':
        # Reference design: regular 6 m floor plates, restricted to upward surfaces.
        split = nt.nodes.new('ShaderNodeSeparateXYZ')
        nt.links.new(geom.outputs['Position'], split.inputs[0])
        seams = []
        for axis in ('X','Y'):
            scale = nt.nodes.new('ShaderNodeMath'); scale.operation='MULTIPLY'; scale.inputs[1].default_value=1/6
            frac = nt.nodes.new('ShaderNodeMath'); frac.operation='FRACT'
            edge = nt.nodes.new('ShaderNodeMath'); edge.operation='LESS_THAN'; edge.inputs[1].default_value=.007
            nt.links.new(split.outputs[axis],scale.inputs[0]); nt.links.new(scale.outputs[0],frac.inputs[0])
            nt.links.new(frac.outputs[0],edge.inputs[0]); seams.append(edge.outputs[0])
        maximum=nt.nodes.new('ShaderNodeMath'); maximum.operation='MAXIMUM'
        nt.links.new(seams[0],maximum.inputs[0]); nt.links.new(seams[1],maximum.inputs[1])
        normal=nt.nodes.new('ShaderNodeSeparateXYZ'); nt.links.new(geom.outputs['Normal'],normal.inputs[0])
        up=nt.nodes.new('ShaderNodeMath'); up.operation='GREATER_THAN'; up.inputs[1].default_value=.7
        nt.links.new(normal.outputs['Z'],up.inputs[0])
        mask=nt.nodes.new('ShaderNodeMath'); mask.operation='MULTIPLY'
        nt.links.new(up.outputs[0],mask.inputs[0]); nt.links.new(maximum.outputs[0],mask.inputs[1])
        ink=nt.nodes.new('ShaderNodeMixRGB'); ink.inputs[2].default_value=(*linear(INK),1)
        strength=nt.nodes.new('ShaderNodeMath'); strength.operation='MULTIPLY'; strength.inputs[1].default_value=REVISION['platform_line_strength']
        nt.links.new(mask.outputs[0],strength.inputs[0]); nt.links.new(strength.outputs[0],ink.inputs[0])
        nt.links.new(surface,ink.inputs[1]); surface=ink.outputs[0]
    nt.links.new(surface, em.inputs['Color']); nt.links.new(em.outputs[0], output.inputs[0])
    return mat

def transparent_mark(key, light=False):
    path = ROOT / 'Source/Textures' / ('T_TB1_Light_PBR_Emission.png' if light else 'T_TB1_Logo.png')
    mat = bpy.data.materials.new('A5_' + key + '_SourceTransparentDisplay')
    mat.use_nodes = True
    nt = mat.node_tree; nt.nodes.clear()
    tex = nt.nodes.new('ShaderNodeTexImage'); tex.image = bpy.data.images.load(str(path), check_existing=True)
    em = nt.nodes.new('ShaderNodeEmission'); em.inputs['Color'].default_value = (*linear(THEMES[key]['Accent']), 1)
    trans = nt.nodes.new('ShaderNodeBsdfTransparent'); mix = nt.nodes.new('ShaderNodeMixShader')
    output = nt.nodes.new('ShaderNodeOutputMaterial')
    if light:
        tex.image.colorspace_settings.name = 'Non-Color'
        gain = nt.nodes.new('ShaderNodeMath'); gain.operation = 'MULTIPLY'
        gain.inputs[1].default_value = 4.2; gain.use_clamp = True
        nt.links.new(tex.outputs['Color'], gain.inputs[0]); nt.links.new(gain.outputs[0], mix.inputs[0])
    else:
        nt.links.new(tex.outputs['Alpha'], mix.inputs[0])
    nt.links.new(trans.outputs[0], mix.inputs[1]); nt.links.new(em.outputs[0], mix.inputs[2])
    nt.links.new(mix.outputs[0], output.inputs[0])
    mat.surface_render_method = 'DITHERED'
    mat.use_transparency_overlap = False
    mat.use_backface_culling = True
    return mat

def presentation_normals(o):
    # Only normal vectors in the reference scene; original geometry/FBX are frozen.
    data = o.data; incident = defaultdict(list)
    for p in data.polygons:
        for vi in p.vertices:
            key = tuple(round(x, 5) for x in data.vertices[vi].co)
            incident[key].append((p.normal.copy(), math.sqrt(max(p.area, 1e-10))))
    normals = [(0, 0, 0)] * len(data.loops)
    limit = math.cos(math.radians(42))
    for p in data.polygons:
        p.use_smooth = True
        for li in p.loop_indices:
            v = data.vertices[data.loops[li].vertex_index]
            n = Vector((0, 0, 0))
            for neighbor, weight in incident[tuple(round(x, 5) for x in v.co)]:
                if p.normal.dot(neighbor) > limit:
                    n += neighbor * weight
            normals[li] = tuple(n.normalized()) if n.length_squared else tuple(p.normal)
    data.normals_split_custom_set(normals)

def choose_role(key, part, extent):
    cid, bone = part['id'], (part['dominant_bone'] or '').lower()
    for role, ids in OVERRIDES.get(key, {}).items():
        if cid in ids:
            return role
    if key == 'Floor':
        return 'Primary' if max(part['dimensions_m']) < 4.0 else 'Platform'
    if key == 'Light': return 'Orange'
    if any(s in bone for s in ('hose', 'tube', 'vent', 'airing', 'gears', 'display')):
        return 'Equipment'
    if 'capsule' in bone: return 'Equipment'
    if 'antenna' in bone:
        return 'Primary' if min(part['dimensions_m']) > extent * .015 else 'Frame'
    size = max(part['dimensions_m'])
    if size < extent * .055 or min(part['dimensions_m']) < extent * .003:
        return 'Frame'
    return 'Equipment' if size < extent * .19 else 'Primary'

def geometry_digest(objects):
    h = hashlib.sha256()
    for o in sorted(objects, key=lambda o: o.name):
        for v in o.data.vertices:
            h.update(repr(tuple(v.co)).encode())
        for p in o.data.polygons:
            h.update(repr(tuple(p.vertices)).encode())
        for v in o.data.vertices:
            h.update(repr([(g.group, round(g.weight, 7)) for g in v.groups]).encode())
    return h.hexdigest()

def scene_style(scene):
    scene.render.engine = 'BLENDER_EEVEE'; scene.eevee.taa_render_samples = 48
    scene.render.resolution_x = scene.render.resolution_y = SIZE; scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'; scene.render.film_transparent = False
    scene.view_settings.view_transform = 'Standard'; scene.view_settings.look = 'None'
    nt = scene.world.node_tree
    nt.nodes.clear()
    background = nt.nodes.new('ShaderNodeBackground')
    world_output = nt.nodes.new('ShaderNodeOutputWorld')
    background.inputs['Color'].default_value = (*linear('#F3EEE5'), 1)
    background.inputs['Strength'].default_value = 1
    nt.links.new(background.outputs[0], world_output.inputs[0])
    scene.render.use_freestyle = True
    fs = scene.view_layers[0].freestyle_settings; fs.crease_angle = math.radians(135)
    ls = fs.linesets[0] if fs.linesets else fs.linesets.new('StructuralLines')
    ls.select_silhouette = False; ls.select_external_contour = False
    ls.select_border = False; ls.select_crease = True; ls.select_material_boundary = True
    ls.select_edge_mark = False; ls.select_suggestive_contour = False; ls.select_ridge_valley = False
    if ls.linestyle is None: ls.linestyle = bpy.data.linestyles.new('StructuralInk')
    ls.linestyle.color = linear(INK); ls.linestyle.thickness = 1.45 * SIZE / 2048
    outer = fs.linesets.new('OuterContour')
    outer.select_silhouette = True; outer.select_external_contour = True
    outer.select_border = False; outer.select_crease = False; outer.select_material_boundary = False
    outer.linestyle = bpy.data.linestyles.new('OuterInk')
    outer.linestyle.color = linear(INK); outer.linestyle.thickness = 2.6 * SIZE / 2048

asset_report = []
for rec in baseline['assets']:
    key = rec['key']; scene = bpy.data.scenes[rec['scene']]; bpy.context.window.scene = scene
    meshes = [o for o in scene.objects if o.type == 'MESH']
    before = geometry_digest(meshes)
    line = ROOT / 'ReferenceMasks' / (key + '_LineMask.png')
    palette = asset_palette(key)
    mats = {r: toon(key, r, palette, line if line.exists() else None) for r in palette}
    indices = {r: i for i, r in enumerate(mats)}
    mark = transparent_mark(key) if any('Logo' in s for m in rec['meshes'] for s in m['materials']) else None
    extent = max(rec['dimensions_m']); assignments = []
    roles = {p['id']: choose_role(key, p, extent) for p in rec['parts']}
    symmetry_pairs = []
    # The source's functional asymmetry stays intact. Match only existing physical mirror pairs.
    for p in [p for p in rec['parts'] if p['center_m'][0] < -.02]:
        x,y,z = p['center_m']
        matches = [q for q in rec['parts'] if q['center_m'][0] > .02
                   and max(abs(a-b) for a,b in zip(q['center_m'],(-x,y,z))) < .015
                   and max(abs(a-b) for a,b in zip(q['dimensions_m'],p['dimensions_m'])) < .015]
        if len(matches) == 1:
            q = matches[0]; roles[p['id']] = roles[q['id']]
            symmetry_pairs.append([p['id'],q['id'],roles[p['id']]])
    if key == 'Light': mark = transparent_mark(key, light=True)
    for o in meshes:
        old_slots = [s.name for s in o.material_slots]
        logo_polys = {p.index for p in o.data.polygons if 'Logo' in old_slots[p.material_index]}
        o.data.materials.clear()
        for m in mats.values(): o.data.materials.append(m)
        if mark: o.data.materials.append(mark)
        for part in [p for p in rec['parts'] if p['object'] == o.name]:
            role = roles[part['id']]
            for pi in part['polygons']:
                o.data.polygons[pi].material_index = len(mats) if mark and (pi in logo_polys or key == 'Light') else indices[role]
            assignments.append({'part_id': part['id'], 'dominant_bone': part['dominant_bone'], 'role': role})
        presentation_normals(o)
    assert before == geometry_digest(meshes), 'Stage A must not change topology, coordinates or weights'
    scene_style(scene)
    lo, hi = Vector(rec['min_m']), Vector(rec['max_m']); center = (lo + hi) / 2
    cam = scene.camera; cam.data.type = 'ORTHO'; cam.data.clip_end = extent * 50
    directions = {'Hero': (1, -1.3, .8), 'Front': (0, -1, 0), 'Left': (1, 0, 0), 'Back': (0, 1, 0)}
    if key in ('Floor', 'Drone'): directions['Top'] = (0, 0, 1)
    if key == 'Light':
        normal=(meshes[0].matrix_world.to_3x3().inverted().transposed() @ meshes[0].data.polygons[0].normal).normalized()
        directions['Front'] = tuple(normal)
        directions['Back'] = tuple(-normal)
    projections = []
    corners = [o.matrix_world @ Vector(p) for o in meshes for p in o.bound_box]
    for d in directions.values():
        rotation = (-Vector(d)).to_track_quat('-Z', 'Y')
        points = [rotation.inverted() @ (p - center) for p in corners]
        projections.append(max(max(p.x for p in points)-min(p.x for p in points),
                               max(p.y for p in points)-min(p.y for p in points)))
    cam.data.ortho_scale = max(projections) * 1.38
    row = {'key': key, 'theme': THEMES[key], 'material_palette':palette, 'dimensions_m': rec['dimensions_m'],
           'geometry_sha256_before': before, 'geometry_sha256_after': geometry_digest(meshes),
           'source_geometry_unchanged': True, 'parts': assignments, 'mirror_color_pairs': symmetry_pairs, 'views': {},
           'note': 'Stage A source-constrained material/normal/line study; not a remade Blender product'}
    for view, d in directions.items():
        cam.location = center + Vector(d).normalized() * extent * 4
        cam.rotation_euler = (center - cam.location).to_track_quat('-Z', 'Y').to_euler()
        row['views'][view] = {'camera_location_m': list(cam.location), 'rotation_rad': list(cam.rotation_euler),
                              'ortho_scale_m': cam.data.ortho_scale,
                              'file': 'Renders/' + key + '_' + view + '.png'}
        if not opts.assets or key in opts.assets.split(','):
            scene.render.filepath = str(OUT / row['views'][view]['file'])
            bpy.ops.render.render(write_still=True)
            print('REFERENCE_VIEW_DONE', key, view, flush=True)
            if not opts.draft and view in ('Hero','Front','Left','Back'):
                original_mats = [[s.material for s in o.material_slots] for o in meshes]
                gray = bpy.data.materials.get('SourceGray')
                for o in meshes:
                    for slot in o.material_slots: slot.material = gray
                scene.render.use_freestyle = False
                gray_dir = ROOT / 'Baseline/Matched_2048'
                gray_dir.mkdir(exist_ok=True)
                scene.render.filepath = str(gray_dir / (key + '_' + view + '_Gray.png'))
                bpy.ops.render.render(write_still=True)
                scene.render.use_freestyle = True
                for o, old in zip(meshes, original_mats):
                    for slot, mat in zip(o.material_slots, old): slot.material = mat
    asset_report.append(row)

assembly = None
if not opts.draft:
    # Review-only composition at original asset scale. This is not a gameplay level layout.
    group = bpy.data.scenes.new('A5_ReferenceAssembly')
    bpy.context.window.scene = group
    group.world = bpy.data.worlds.new('A5_AssemblyWorld'); group.world.use_nodes = True
    scene_style(group)
    layout = {'Floor': (0,0,0), 'AirBase': (-20,-15,1.8), 'CloningCenter':(-21,16,1.8),
              'CommandCenter':(0,-26,1.8), 'MilitaryFactory':(20,-24,1.8),
              'Reactor':(-33,10,1.8), 'StrategyCenter':(8.3,14,3.6), 'Drone':(20,-31,6.8)}
    assets_by_key = {r['key']:r for r in baseline['assets']}
    all_meshes=[]
    def instance(key, offset, suffix=''):
        scene = bpy.data.scenes[assets_by_key[key]['scene']]
        originals = [o for o in scene.objects if o.type in ('MESH','ARMATURE','EMPTY')]
        mapping = {}
        for o in originals:
            copy = o.copy(); copy.name = 'Assembly_' + key + suffix + '_' + o.name
            group.collection.objects.link(copy); mapping[o] = copy
        for original, copy in mapping.items():
            if original.parent in mapping:
                copy.parent = mapping[original.parent]
            else:
                copy.location += Vector(offset)
            for mod in copy.modifiers:
                if mod.type == 'ARMATURE' and mod.object in mapping:
                    mod.object = mapping[mod.object]
            if copy.type == 'MESH': all_meshes.append(copy)
    for key, offset in layout.items(): instance(key, offset)
    for i,pos in enumerate([(13,-33,1.8),(27,-33,1.8),(-28,12,1.8),(-14,12,1.8)]):
        instance('Lamp', pos, '_' + str(i))
        # Light is a source flat sprite; place below lamp head without adding volume.
        instance('Light', (pos[0],pos[1]-.95,pos[2]+3.75), '_' + str(i))
    bpy.context.view_layer.update()
    points=[o.matrix_world @ Vector(p) for o in all_meshes for p in o.bound_box]
    lo=Vector([min(p[a] for p in points) for a in range(3)])
    hi=Vector([max(p[a] for p in points) for a in range(3)])
    center=(lo+hi)/2; extent=max(hi-lo)
    cam=bpy.data.objects.new('AssemblyCamera',bpy.data.cameras.new('AssemblyCamera'))
    group.collection.objects.link(cam); group.camera=cam
    cam.data.type='ORTHO'; cam.data.clip_end=extent*50; cam.data.ortho_scale=extent*1.48
    cam.location=center+Vector((.7,-1.2,1.4)).normalized()*extent*4
    cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
    group.render.resolution_x=group.render.resolution_y=4096
    group.render.filepath=str(OUT/'Renders/Assembly_Hero.png')
    bpy.ops.render.render(write_still=True)
    cam.location=center+Vector((0,0,1))*extent*4
    cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
    group.render.filepath=str(OUT/'Renders/Assembly_Top.png')
    bpy.ops.render.render(write_still=True)
    assembly={'files':['Renders/Assembly_Hero.png','Renders/Assembly_Top.png'],
              'resolution':[4096,4096], 'layout_m':layout,
              'purpose':'Original-scale palette/assembly reference only; not an approved map layout'}

report = {'version': 'SSF_Reference_A_v5' if not opts.draft else 'SSF_A_Draft_v5',
          'stage': 'A', 'approvals': {'A': 'pending', 'B': 'not_started'},
          'blender_version': bpy.app.version_string, 'resolution': [SIZE, SIZE],
          'user_swatch_library': SWATCHES, 'themes': THEMES, 'shared_ink': INK,
          'revision_reason':'User clarified: lighten original dark swatches only; preserve original light/middle HEX and original shading factors',
          'palette_revision':REVISION,
          'art_light_direction': list(Vector((.35,-.55,.76)).normalized()),
          'tone_thresholds': [.12, .55], 'tone_factors': REVISION['tone_factors'],
          'line_method': 'Reference-only Freestyle geometric structure and silhouette + independent source-UV LineMask',
          'light_sprite_mask_gain':4.2, 'light_sprite_mask_color_space':'Non-Color',
          'assets': asset_report, 'assembly': assembly,
          'production_modeling_started': False, 'ue_formal_import_performed': False}
if not opts.draft:
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'SSF_ReferenceDesign_v5.blend'))
    (OUT / 'render_manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
else:
    (OUT / 'draft_manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print('SSF_REFERENCE_RENDER_COMPLETE', flush=True)
