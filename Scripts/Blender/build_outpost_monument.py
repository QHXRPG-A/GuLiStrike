"""Build the reference-driven GuLiStrike outpost in a new Blender scene.

Run with Blender's Python API. Existing scenes are preserved. The source meshes
remain editable; export meshes are evaluated copies with metre-scale geometry.
"""
from pathlib import Path
import math
import json
import bpy
import bmesh
import numpy as np
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1')
ASSET = ROOT / 'ArtSource/Buildings/OutpostMonument'
PREVIEWS = ROOT / 'outputs/outpost-monument-20260905'
HEIGHT_M = 300.0
S = HEIGHT_M / 80.0
TILE_M = 4.0
SCENE_NAME = 'GS_OutpostMonument_v01'


def texture_file(name, array, non_color=False):
    height, width = array.shape[:2]
    if array.ndim == 2:
        array = np.repeat(array[:, :, None], 3, axis=2)
    rgba = np.ones((height, width, 4), dtype=np.float32)
    rgba[:, :, :3] = np.clip(array, 0, 1)
    img = bpy.data.images.new(name, width=width, height=height, alpha=False)
    img.colorspace_settings.name = 'Non-Color' if non_color else 'sRGB'
    img.pixels.foreach_set(rgba.ravel())
    img.filepath_raw = str(ASSET / 'Textures' / (name + '.png'))
    img.file_format = 'PNG'
    img.save()
    return img


def make_concrete():
    """Four-metre seamless PBR tile, with fine aggregate and formwork marks."""
    n = 2048
    rng = np.random.default_rng(20260905)
    ys, xs = np.mgrid[0:n, 0:n].astype(np.float32) / n

    def noise(gx, gy):
        grid = rng.random((gy, gx)).astype(np.float32) - .5
        x, y = xs * gx, ys * gy
        ix, iy = x.astype(np.int32), y.astype(np.int32)
        fx, fy = x - ix, y - iy
        fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        return (grid[iy % gy, ix % gx] * (1-fx) * (1-fy)
                + grid[iy % gy, (ix+1) % gx] * fx * (1-fy)
                + grid[(iy+1) % gy, ix % gx] * (1-fx) * fy
                + grid[(iy+1) % gy, (ix+1) % gx] * fx * fy)

    broad = noise(6, 6)
    mid = noise(40, 40)
    fine = noise(360, 360)
    grain = rng.random((n, n)).astype(np.float32) - .5
    streak = noise(140, 4)
    dy = np.minimum(np.mod(ys, .5), .5-np.mod(ys, .5)) * TILE_M
    seam = np.exp(-np.square(dy / .006))
    holes = np.zeros((n, n), dtype=np.float32)
    for hx, hy in ((.25, .22), (.75, .22), (.25, .72), (.75, .72)):
        dist = np.sqrt(((xs-hx)*TILE_M)**2 + ((ys-hy)*TILE_M)**2)
        holes = np.maximum(holes, np.clip((.019-dist)/.007, 0, 1))
    height = .00045 * mid + .0004 * fine + .00008 * grain - .003 * seam - .009 * holes
    gray = .62 + .05*broad + .025*mid + .016*fine + .009*grain + .014*streak
    gray -= .055*seam + .105*holes
    rgb = np.stack((gray*1.015, gray*1.007, gray*.974), axis=2)
    rough = np.clip(.80 + .10*mid + .06*fine + .04*seam, .62, .94)
    dx = (np.roll(height, -1, axis=1)-np.roll(height, 1, axis=1))/(2*TILE_M/n)
    dy = (np.roll(height, -1, axis=0)-np.roll(height, 1, axis=0))/(2*TILE_M/n)
    normals = np.stack((-dx, -dy, np.ones_like(dx)), axis=2)
    normals /= np.linalg.norm(normals, axis=2, keepdims=True)
    normal_rgb = normals*.5+.5
    albedo = texture_file('T_OutpostConcrete_BaseColor', rgb)
    roughness = texture_file('T_OutpostConcrete_Roughness', rough, True)
    normal = texture_file('T_OutpostConcrete_NormalGL', normal_rgb, True)
    normal_dx = normal_rgb.copy()
    normal_dx[:, :, 1] = 1-normal_dx[:, :, 1]
    texture_file('T_OutpostConcrete_NormalDX', normal_dx, True)
    mat = bpy.data.materials.new('M_Outpost_FairFacedConcrete')
    mat.use_nodes = True
    mat.diffuse_color = (.58, .57, .54, 1)
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    bsdf = next(n for n in nodes if n.type == 'BSDF_PRINCIPLED')
    bsdf.inputs['Roughness'].default_value = .8
    bsdf.inputs['Metallic'].default_value = 0
    texnodes = []
    for img, name, y in ((albedo, 'Base color · 4 m tile', 300),
                         (roughness, 'Roughness', 0),
                         (normal, 'Tangent normal · OpenGL', -300)):
        tex = nodes.new('ShaderNodeTexImage')
        tex.name = name
        tex.label = name
        tex.location = (-500, y)
        tex.image = img
        tex.extension = 'REPEAT'
        texnodes.append(tex)
    links.new(texnodes[0].outputs['Color'], bsdf.inputs['Base Color'])
    links.new(texnodes[1].outputs['Color'], bsdf.inputs['Roughness'])
    bump = nodes.new('ShaderNodeNormalMap')
    bump.inputs['Strength'].default_value = .65
    bump.location = (-160, -250)
    links.new(texnodes[2].outputs['Color'], bump.inputs['Color'])
    links.new(bump.outputs['Normal'], bsdf.inputs['Normal'])
    mat['tile_size_metres'] = TILE_M
    return mat


def recalc(mesh):
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(mesh)
    bm.free()
    mesh.update()


def extrude_profile(name, profile, z0, z1, collection, material, bevel=.035):
    """Closed solid with continuous perimeter UVs; all mesh origins at ground zero."""
    n = len(profile)
    verts = [(x*S, y*S, z*S) for z in (z0, z1) for x, y in profile]
    faces = [tuple(reversed(range(n))), tuple(range(n, 2*n))]
    faces.extend((i, (i+1)%n, (i+1)%n+n, i+n) for i in range(n))
    mesh = bpy.data.meshes.new(name + '_Mesh')
    mesh.from_pydata(verts, [], faces)
    mesh.validate()
    recalc(mesh)
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    mesh.materials.append(material)
    uv = mesh.uv_layers.new(name='UV0_Concrete4m')
    lengths = [0.0]
    for i in range(n):
        lengths.append(lengths[-1] + math.dist(profile[i], profile[(i+1)%n])*S)
    for poly in mesh.polygons:
        if poly.index < 2:
            for li in poly.loop_indices:
                v = mesh.vertices[mesh.loops[li].vertex_index].co
                uv.data[li].uv = (v.x/TILE_M, v.y/TILE_M)
        else:
            i = poly.index-2
            for li in poly.loop_indices:
                vi = mesh.loops[li].vertex_index
                edge_end = vi % n != i
                u = lengths[i+int(edge_end)] / TILE_M
                v = (z0 if vi < n else z1)*S/TILE_M
                uv.data[li].uv = (u, v)
            poly.use_smooth = True
    if hasattr(mesh, 'set_sharp_from_angle'):
        mesh.set_sharp_from_angle(angle=math.radians(35))
    bevel_mod = obj.modifiers.new('Concrete edge radius', 'BEVEL')
    bevel_mod.width = bevel*S
    bevel_mod.segments = 3
    bevel_mod.limit_method = 'ANGLE'
    bevel_mod.angle_limit = math.radians(35)
    bevel_mod.harden_normals = True
    weighted = obj.modifiers.new('Architectural normals', 'WEIGHTED_NORMAL')
    weighted.keep_sharp = True
    weighted.weight = 40
    obj['role'] = name.split('_', 2)[-1]
    obj['height_metres'] = (z1-z0)*S
    return obj


def capsule(cx, cy, length, thickness, angle=0, steps=12):
    a = math.radians(angle)
    direction = Vector((math.sin(a), math.cos(a)))
    p1 = Vector((cx, cy))-direction*(length-thickness)/2
    p2 = Vector((cx, cy))+direction*(length-thickness)/2
    phi = math.atan2(direction.y, direction.x)
    r = thickness/2
    return [(p.x+r*math.cos(t), p.y+r*math.sin(t))
            for p, start in ((p2, phi-math.pi/2), (p1, phi+math.pi/2))
            for t in np.linspace(start, start+math.pi, steps+1)]


def arc_profile(radius, a0, a1, cx=0, cy=0, inner=None, steps=64):
    angles = np.linspace(math.radians(a0), math.radians(a1), steps+1)
    outer = [(cx+radius*math.cos(a), cy+radius*math.sin(a)) for a in angles]
    if inner is not None:
        outer += [(cx+inner*math.cos(a), cy+inner*math.sin(a)) for a in reversed(angles)]
    return outer


def simple_material(name, rgb, roughness=.8):
    mat = bpy.data.materials.new(name)
    mat.diffuse_color = (*rgb, 1)
    mat.use_nodes = True
    bsdf = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    bsdf.inputs['Base Color'].default_value = (*rgb, 1)
    bsdf.inputs['Roughness'].default_value = roughness
    return mat


def collection(name, scene):
    coll = bpy.data.collections.new(name)
    scene.collection.children.link(coll)
    return coll


def link_operator_object(obj, target, name):
    obj.name = name
    for c in list(obj.users_collection):
        c.objects.unlink(obj)
    target.objects.link(obj)
    return obj


def make_human(stage, material, xy=(42, -35)):
    pieces = []
    bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=8, radius=.13,
                                       location=(*xy, 1.67))
    obj = link_operator_object(bpy.context.object, stage, 'REF_Human_1p8m_Head')
    obj.scale = (.78, .88, 1)
    pieces.append(obj)
    for name, loc, scale in (
        ('Torso', (xy[0], xy[1], 1.25), (.19, .11, .30)),
        ('Pelvis', (xy[0], xy[1], .91), (.16, .105, .10)),
        ('LegL', (xy[0]-.105, xy[1], .44), (.075, .085, .43)),
        ('LegR', (xy[0]+.105, xy[1], .44), (.075, .085, .43)),
        ('ArmL', (xy[0]-.25, xy[1], 1.14), (.055, .07, .31)),
        ('ArmR', (xy[0]+.25, xy[1], 1.14), (.055, .07, .31))):
        bpy.ops.mesh.primitive_uv_sphere_add(segments=12, ring_count=8, radius=1, location=loc)
        obj = link_operator_object(bpy.context.object, stage, 'REF_Human_1p8m_'+name)
        obj.scale = scale
        pieces.append(obj)
    for obj in pieces:
        obj.data.materials.append(material)
        for p in obj.data.polygons:
            p.use_smooth = True


def camera(name, location, target, coll, lens=52, ortho=None):
    data = bpy.data.cameras.new(name)
    obj = bpy.data.objects.new(name, data)
    coll.objects.link(obj)
    obj.location = location
    obj.rotation_euler = (Vector(target)-obj.location).to_track_quat('-Z', 'Y').to_euler()
    data.lens = lens
    data.clip_start = .1
    data.clip_end = 10000
    if ortho:
        data.type = 'ORTHO'
        data.ortho_scale = ortho
    return obj


def build():
    if bpy.data.scenes.get(SCENE_NAME):
        raise RuntimeError('The v01 scene already exists; inspect/edit it instead of rebuilding over it.')
    for folder in (ASSET, ASSET/'Textures', ASSET/'Exports', ASSET/'References', PREVIEWS):
        folder.mkdir(parents=True, exist_ok=True)
    if bpy.context.object and bpy.context.object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    scene = bpy.data.scenes.new(SCENE_NAME)
    bpy.context.window.scene = scene
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 1
    scene.unit_settings.length_unit = 'METERS'
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 32
    scene.cycles.use_denoising = True
    scene.cycles.device = 'CPU'
    scene.render.resolution_x = 1100
    scene.render.resolution_y = 1400
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.view_settings.view_transform = 'AgX'
    scene.render.film_transparent = False
    model = collection('GS_01_Editable_Architecture', scene)
    stage = collection('GS_90_Presentation_Only', scene)
    cameras = collection('GS_91_Cameras_Lighting', scene)
    concrete = make_concrete()
    parts = []

    def solid(name, profile, lo, hi, bevel=.035):
        obj = extrude_profile('GS_OP_'+name, profile, lo, hi, model, concrete, bevel)
        parts.append(obj)
        return obj

    # Uneven vertical blades, all rooted into the ground.
    solid('01_CrownBlade', capsule(2.25, 2.25, 11.5, 1.15, -8), 0, 80)
    solid('02_WestBlade', capsule(-3.15, .65, 12.4, 1.18, -15), 0, 69)
    solid('03_EastBlade', capsule(5.15, 1.4, 10.6, 1.05, 12), 0, 58)
    solid('04_BackBlade', capsule(-.3, 5.8, 8.6, .95, 76), 0, 63)
    # Hollow C-section spine; the top opening stays visible from above.
    solid('05_OpenCrownSpine', arc_profile(1.15, 20, 340, 0, .25, inner=.76), 0, 78.5, .025)
    # Broad lower drums and staggered projecting shells.
    solid('06_LowerWestDrum', arc_profile(7.35, 167, 262, inner=4.95), 0, 40.5)
    solid('07_LowerEastDrum', arc_profile(6.95, 278, 376, inner=5.05), 0, 36)
    solid('08_FrontShoulder', arc_profile(6.1, 194, 331, inner=3.35), 20, 48.5)
    solid('09_UpperHalfDrum', arc_profile(3.5, 194, 374, -.25, .15), 37, 65.5)
    solid('10_MiddleHalfDrum', arc_profile(4.7, 191, 337, -.35, -.75), 30, 53)
    solid('11_LeftUpperPetal', arc_profile(4.2, 80, 200, -1.9, 1.7, inner=2.35), 20, 52)
    solid('12_RearHalfDrum', arc_profile(4.8, -3, 164, .65, 1.1, inner=2.8), 0, 44)
    # One thick open arc at midheight: the reference's dominant horizontal gesture.
    solid('13_MonumentCollar', arc_profile(9.5, 179, 354, inner=7.38, steps=96), 30.5, 41.5, .045)
    solid('14_CollarRearReturn', arc_profile(8.45, -2, 75, inner=6.45), 29.1, 39.1)
    # Long vertical fins underneath and beside the collar; deep recesses remain open.
    solid('15_WestButtress', capsule(-7.65, .5, 5.8, .9, -6), 0, 57)
    solid('16_EastButtress', capsule(7.4, 1.4, 6.5, 1.15, 9), 0, 45.5)
    solid('17_FrontReveal', capsule(-2.75, -4.75, 1.8, .72, 18), 0, 54.5)
    solid('18_LowFloatingPetal', arc_profile(7.48, 198, 240, inner=6.72, steps=28), 9, 30.1)
    solid('19_SideFloatingPetal', arc_profile(7.15, 302, 350, inner=6.42, steps=28), 17, 42.5)
    solid('20_NarrowSpineFin', capsule(-.85, 2.9, 4.3, .65, 48), 0, 73)
    solid('21_InnerLowerColumn', arc_profile(1.35, 0, 360, .1, -2, steps=48)[:-1], 0, 37)

    groundmat = simple_material('PRESENTATION_Ground', (.065, .077, .082), .95)
    bpy.ops.mesh.primitive_plane_add(size=20000, location=(0, 0, -.045))
    ground = link_operator_object(bpy.context.object, stage, 'PRESENTATION_Ground_NotExported')
    ground.data.materials.append(groundmat)
    humanmat = simple_material('PRESENTATION_ScaleFigure', (.25, .11, .04))
    make_human(stage, humanmat)
    world = bpy.data.worlds.new('GS_Outpost_Daylight')
    world.use_nodes = True
    scene.world = world
    nodes, links = world.node_tree.nodes, world.node_tree.links
    sky = nodes.new('ShaderNodeTexSky')
    sky.sky_type = ('MULTIPLE_SCATTERING' if 'MULTIPLE_SCATTERING' in
                    sky.bl_rna.properties['sky_type'].enum_items.keys() else 'NISHITA')
    sky.sun_elevation = math.radians(34)
    sky.sun_rotation = math.radians(220)
    sky.sun_intensity = .8
    sky.air_density = 1.1
    if hasattr(sky, 'dust_density'):
        sky.dust_density = 1.5
    background = next(n for n in nodes if n.type == 'BACKGROUND')
    links.new(sky.outputs['Color'], background.inputs['Color'])
    background.inputs['Strength'].default_value = .3
    hero = camera('CAM_01_Hero', (440, -665, 375), (0, 0, 143), cameras, lens=60)
    camera('CAM_02_Reverse', (-460, 605, 370), (0, 0, 145), cameras, lens=60)
    camera('CAM_03_Ground', (92, -155, 4), (0, 0, 128), cameras, lens=24)
    camera('CAM_04_FormDetail', (87, -147, 102), (0, -8, 153), cameras, lens=62)
    camera('CAM_05_Orthographic', (470, -690, 420), (0, 0, 148), cameras, ortho=340)
    scene.camera = hero
    scene['asset_intent'] = '300 m reference-driven concrete outpost monument; visual prototype.'
    scene['game_integration'] = 'Not yet applied to the UE building catalog.'
    scene['reference_images'] = 8
    for area in bpy.context.screen.areas:
        if area.type == 'VIEW_3D':
            space = area.spaces.active
            space.clip_end = 20000
            space.shading.type = 'MATERIAL'
            space.region_3d.view_distance = 650
            space.region_3d.view_location = (0, 0, 145)
            space.region_3d.view_rotation = hero.rotation_euler.to_quaternion()
            space.region_3d.view_perspective = 'PERSP'
    bpy.ops.object.select_all(action='DESELECT')
    parts[0].select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.context.view_layer.update()
    bounds = [obj.matrix_world @ Vector(v) for obj in parts for v in obj.bound_box]
    mn = [min(v[i] for v in bounds) for i in range(3)]
    mx = [max(v[i] for v in bounds) for i in range(3)]
    manifest = {'asset':SCENE_NAME, 'status':'Blender visual prototype',
                'blender_version':bpy.app.version_string, 'dimensions_metres':[mx[i]-mn[i] for i in range(3)],
                'bounds_min_metres':mn, 'bounds_max_metres':mx,
                'source_objects':len(parts), 'source_vertices':sum(len(o.data.vertices) for o in parts),
                'height_assumption':'300 m selected for user-confirmed megastructure direction',
                'texture_tile_metres':TILE_M, 'ue_catalog_changed':False,
                'source_scene':SCENE_NAME, 'source_script':'Scripts/Blender/build_outpost_monument.py'}
    (ASSET/'asset_manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    bpy.ops.wm.save_as_mainfile(filepath=str(ASSET/'OutpostMonument_v01.blend'))
    return manifest


if __name__ == '__main__':
    result = build()
