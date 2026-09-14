"""GuLiStrike unit ores, v01. Run in Blender 5.2+; no third-party add-ons.

Interactive: exec(compile(open(__file__, encoding='utf-8').read(), __file__, 'exec'))
CLI build: blender --background --python this_file.py -- --build
CLI previews: blender --background OreResourceVariants_v01.blend --python this_file.py -- --render
Only datablocks tagged with OWNER are replaced by build(). Other scenes are preserved.
The geometry generator is deterministic. Presentation instances share the 24 asset meshes.
"""
from pathlib import Path
import json
import math
import random
import sys
import time

import bpy
import bmesh
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'outputs/ore-resource-variants-20260910'
ASSET = ROOT / 'ArtSource/Resources/Ores/OreResourceVariants_v01.blend'
OWNER = 'GuLiStrike.OreVariants.v01'
STATES = ('Full', 'Partial', 'Remnant')
FAMILY_NAMES = {
    'Blue': ('Radial Crown', 'Offset Twins', 'Broad Fan', 'Arc Ridge'),
    'Red': ('High Spire', 'Forked Pillars', 'Blade Fan', 'Leaning Ridge'),
}
CHINESE_NAMES = {
    'Blue': ('单峰放射', '双峰错列', '宽冠扇簇', '三峰弧脊'),
    'Red': ('高柱尖塔', '双柱分叉', '刀片扇束', '倾斜脊簇'),
}
SCENE_REVIEW = 'ORE • 24 Unit Review'
SCENE_DETAIL = 'ORE • Family Studio'
SCENE_FIELD = 'ORE • Cluster Examples'


def owned(data):
    data['ore_generator'] = OWNER
    return data


def collection(name, parent):
    c = owned(bpy.data.collections.new(name))
    parent.children.link(c)
    return c


def new_scene(name):
    s = owned(bpy.data.scenes.new(name))
    s.unit_settings.system = 'METRIC'
    s.unit_settings.scale_length = 1.0
    s.unit_settings.length_unit = 'METERS'
    s.render.engine = 'BLENDER_EEVEE'
    s.render.resolution_percentage = 100
    s.render.image_settings.file_format = 'PNG'
    s.render.image_settings.color_mode = 'RGB'
    s.render.film_transparent = False
    s.render.image_settings.color_depth = '8'
    s.view_settings.view_transform = 'AgX'
    s.view_settings.look = 'AgX - Medium High Contrast'
    s.view_settings.exposure = -0.05
    w = owned(bpy.data.worlds.new(name + ' World'))
    w.use_nodes = True
    background = next(n for n in w.node_tree.nodes if n.type == 'BACKGROUND')
    background.inputs['Color'].default_value = (0.15, 0.20, 0.28, 1)
    background.inputs['Strength'].default_value = 0.22
    s.world = w
    return s


def clear_generated():
    # Ownership is explicit; never delete by a broad name prefix or clear the file.
    fallback = next((s for s in bpy.data.scenes if s.get('ore_generator') != OWNER), None)
    if not fallback:
        fallback = bpy.data.scenes.new('Scene')
    for w in bpy.context.window_manager.windows:
        if w.scene.get('ore_generator') == OWNER:
            w.scene = fallback
    for attr in ('objects', 'scenes', 'collections', 'meshes', 'curves', 'materials', 'cameras', 'lights', 'worlds', 'texts'):
        pool = getattr(bpy.data, attr)
        for item in list(pool):
            if item.get('ore_generator') == OWNER:
                pool.remove(item, do_unlink=True)


def plain_material(name, color, roughness=0.7, emission=0):
    mat = owned(bpy.data.materials.new(name))
    mat.diffuse_color = (*color, 1)
    mat.use_nodes = True
    bs = next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')
    bs.inputs['Base Color'].default_value = (*color, 1)
    bs.inputs['Roughness'].default_value = roughness
    if emission:
        bs.inputs['Emission Color'].default_value = (*color, 1)
        bs.inputs['Emission Strength'].default_value = emission
    return mat


def ore_material(kind, crystal):
    mat = plain_material('M_Ore_' + kind + ('_Crystal' if crystal else '_Rock'),
                         (0.025, 0.25, 0.5) if kind == 'Blue' else (0.5, 0.025, 0.012))
    nt = mat.node_tree
    bs = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    col = nt.nodes.new('ShaderNodeVertexColor')
    col.layer_name = 'OreColor'
    col.label = 'Authored facet colour • linear RGB'
    col.location = (-550, 180)
    nt.links.new(col.outputs['Color'], bs.inputs['Base Color'])
    bs.inputs['Roughness'].default_value = 0.27 if crystal else 0.79
    bs.inputs['Metallic'].default_value = 0.20 if crystal else 0.02
    bs.inputs['Specular IOR Level'].default_value = 0.42 if crystal else 0.25
    if crystal:
        bs.inputs['Coat Weight'].default_value = 0.16
        bs.inputs['Coat Roughness'].default_value = 0.22
        glow = nt.nodes.new('ShaderNodeAttribute')
        glow.attribute_name = 'OreGlow'
        glow.location = (-550, -70)
        multiply = nt.nodes.new('ShaderNodeMath')
        multiply.operation = 'MULTIPLY'
        multiply.inputs[1].default_value = 0.22
        multiply.location = (-280, -70)
        nt.links.new(glow.outputs['Fac'], multiply.inputs[0])
        nt.links.new(multiply.outputs[0], bs.inputs['Emission Strength'])
        nt.links.new(col.outputs['Color'], bs.inputs['Emission Color'])
    return mat


def mix(a, b, t):
    return tuple(x * (1 - t) + y * t for x, y in zip(a, b))


class Part:
    def __init__(self, name, role, vertices, faces, colors, glow, material):
        self.name, self.role = name, role
        self.vertices, self.faces = vertices, faces
        self.colors, self.glow, self.material = colors, glow, material

    def clipped(self, height, slope, cap_color):
        """Clip convex rings to z <= h + ax + by; reuse all surviving coordinates.

        The plane uses the part's centroid in XY, so tilts do not translate the cut.
        Every intersection is shared through coordinate welding, then a sealed cap
        is triangulated. UVs later use unchanged local surface coordinates.
        """
        center = Vector(tuple(sum(v[i] for v in self.vertices) / len(self.vertices) for i in range(3)))
        verts, faces, colors, glow = [], [], [], []
        lookup, intersections, edge_cuts = {}, {}, {}

        def add(v):
            key = tuple(round(c, 7) for c in v)
            if key not in lookup:
                lookup[key] = len(verts)
                verts.append(tuple(v))
            return lookup[key]

        def dist(v):
            return v[2] - height - slope[0] * (v[0] - center.x) - slope[1] * (v[1] - center.y)

        for fi, face in enumerate(self.faces):
            poly = [Vector(self.vertices[i]) for i in face]
            output = []
            for i, p in enumerate(poly):
                q = poly[(i + 1) % len(poly)]
                dp, dq = dist(p), dist(q)
                if dp <= 1e-7:
                    output.append(p)
                if (dp < -1e-7 and dq > 1e-7) or (dp > 1e-7 and dq < -1e-7):
                    edge_key = tuple(sorted((face[i], face[(i+1) % len(face)])))
                    if edge_key not in edge_cuts:
                        pa, pb = (Vector(self.vertices[j]) for j in edge_key)
                        da, db = dist(pa), dist(pb)
                        edge_cuts[edge_key] = pa + (pb-pa) * (da / (da-db))
                    v = edge_cuts[edge_key]
                    output.append(v)
                    intersections[tuple(round(c, 7) for c in v)] = v
            ids = [add(v) for v in output]
            ids = list(dict.fromkeys(ids))
            if len(ids) >= 3:
                faces.append(ids)
                colors.append(self.colors[fi])
                glow.append(self.glow[fi])
        if len(intersections) >= 3:
            ring = list(intersections.values())
            c = sum(ring, Vector()) / len(ring)
            ring.sort(key=lambda p: math.atan2(p.y - c.y, p.x - c.x))
            radius = max((v-c).length for v in ring)
            # A shallow irregular concavity makes a cleavage surface, not a saw cut.
            c.z -= min(.22, radius*.16)
            c.x += radius*.11
            mid = add(c)
            for i, v in enumerate(ring):
                faces.append([mid, add(v), add(ring[(i + 1) % len(ring)])])
                colors.append(tuple(x * (0.62 + 0.15 * (i % 5)) for x in cap_color))
                glow.append(0.12)
        return Part(self.name, self.role, verts, faces, colors, glow, self.material)


def crystal(kind, name, role, x, y, height, radius, lean=(0, 0), yaw=0, slender=1, seed=0):
    rng = random.Random(seed)
    n = 6 if kind == 'Blue' else 5
    angle = math.radians(yaw)
    corners = []
    for j in range(n):
        a = angle + j * 2 * math.pi / n
        r = radius * rng.uniform(0.94, 1.07)
        corners.append(Vector((math.cos(a) * r, math.sin(a) * r * slender, 0)))
    cross = []
    for j in range(n):
        prev, p, nxt = corners[(j-1) % n], corners[j], corners[(j+1) % n]
        bevel = 0.055 if kind == 'Blue' else 0.045
        cross.extend((p.lerp(prev, bevel), p.lerp(nxt, bevel)))
    count = len(cross)
    # A stable long prism followed by asymmetric broad shoulder and narrow crown.
    ring_specs = [(0, 0.66), (0.11, 0.98), (0.38, 0.97),
                  (0.64 if kind == 'Blue' else 0.72, 1.0),
                  (0.83 if kind == 'Blue' else 0.91, 0.67), (1.0, 0.08)]
    verts, faces, colors, glow = [], [], [], []
    basez = 0.0
    for k, (t, size) in enumerate(ring_specs):
        for j, p in enumerate(cross):
            z = basez + height * t
            if k in (2, 3, 4):
                z += height * (0.035 if k == 2 else 0.04) * math.sin(j / count * math.tau + angle)
            if k == 5:
                z += height * 0.009 * math.cos(j / count * math.tau)
            twist = (0,0,-.040,.034,0,-.025)[k]
            px = p.x*math.cos(twist)-p.y*math.sin(twist)
            py = p.x*math.sin(twist)+p.y*math.cos(twist)
            verts.append((x + lean[0] * t + px * size, y + lean[1] * t + py * size, z))
    dark = (0.002, 0.016, 0.075) if kind == 'Blue' else (0.050, 0.0015, 0.007)
    bright = (0.014, 0.32, 0.63) if kind == 'Blue' else (0.65, 0.027, 0.009)
    edgecolor = (0.12, 0.64, 0.87) if kind == 'Blue' else (0.86, 0.10, 0.025)
    for k in range(5):
        for j in range(count):
            a, b = k * count + j, k * count + (j+1) % count
            c, d = b + count, a + count
            anglej = angle + j / count * math.tau
            directional = (math.cos(anglej + 2.25) + 1) * 0.5
            value = 0.06 + directional**1.6 * 0.68 + 0.055 * k + rng.uniform(-0.07, 0.07)
            basecolor = mix(dark, bright, min(0.97, max(0.1, value)))
            if k>=3:
                basecolor = mix(basecolor,edgecolor,.16 if k==3 else .48)
            bevel_face = j % 2 == 0
            if bevel_face:
                basecolor = mix(basecolor, edgecolor, 0.35 + 0.30 * directional)
            def face(ids, col):
                faces.append(ids)
                colors.append(col)
                glow.append((0.40 if k > 1 else 0.10) if bevel_face else 0)
            if not bevel_face:
                face([a, b, c], tuple(ch * (0.75 if k in (1,2) else .93) for ch in basecolor))
                face([a, c, d], tuple(ch * 1.10 for ch in basecolor))
            else:
                face([a, b, c, d], basecolor)
    faces.append(list(reversed(range(count))))
    colors.append(dark)
    glow.append(0)
    faces.append(list(range(5*count, 6*count)))
    colors.append(edgecolor)
    glow.append(0.55)
    return Part(name, role, verts, faces, colors, glow, 0)


def rock(kind, name, role, x, y, rx, ry, h, yaw, seed):
    rng = random.Random(seed)
    n = 6
    rings = [(0, 0.68), (h*0.19, 1), (h*0.72, 0.89), (h, 0.42)]
    verts, faces = [], []
    corner_r = [rng.uniform(0.87, 1.12) for _ in range(n)]
    for k, (z, size) in enumerate(rings):
        for j in range(n):
            a = yaw + j * math.tau / n
            zz = z if k == 0 else z + h * rng.uniform(-0.065, 0.065)
            verts.append((x + math.cos(a)*rx*size*corner_r[j], y + math.sin(a)*ry*size*corner_r[j], max(0, zz)))
    for k in range(3):
        for j in range(n):
            a, b = k*n+j, k*n+(j+1)%n
            c, d = b+n, a+n
            if k == 2 and j % 3 == 1:
                faces.extend(([a,b,c], [a,c,d]))
            else:
                faces.append([a,b,c,d])
    faces.extend((list(reversed(range(n))), list(range(n*3,n*4))))
    base = (0.072, 0.098, 0.125) if kind == 'Blue' else (0.43, 0.405, 0.35)
    colors = []
    for i, f in enumerate(faces):
        z = sum(verts[v][2] for v in f) / len(f) / h
        value = rng.uniform(0.66, 1.12) * (0.64 + z*0.43)
        colors.append(tuple(c*value for c in base))
    return Part(name, role, verts, faces, colors, [0]*len(faces), 1)


def family_parts(kind, family):
    # x, y, height, radius, leanX, leanY, yaw, cross-section depth
    presets = {
        ('Blue',1): [(-.25,.45,7.45,1.38,.25,.10,17,1), (1.35,.8,4.9,.75,.90,.25,5,.9)],
        ('Blue',2): [(-1.40,.7,7.45,1.05,-.5,.2,13,1), (1.40,-.30,6.3,1.18,.7,-.12,35,1)],
        ('Blue',3): [(-2.05,.50,5.85,.87,-1.20,.1,8,.83), (-.55,.8,7.35,1.10,-.55,.2,28,.85), (1.20,.3,6.8,.99,.6,.1,48,.86), (2.6,.8,4.9,.73,1.1,.15,18,.8)],
        ('Blue',4): [(-2.1,-.20,5.7,1,-.6,-.25,17,.94), (0,1.3,7.4,1.08,.2,.4,37,.91), (2.2,.1,6.25,1,.7,-.25,11,.94)],
        ('Red',1): [(-.35,.5,7.65,.97,.10,.25,16,.84), (1.20,.70,5.1,.53,.30,.1,45,.82)],
        ('Red',2): [(-.82,.40,7.6,.91,-1.5,.25,10,.84), (1.0,-.15,6.9,.82,1.2,-.1,27,.83)],
        ('Red',3): [(-2.0,.5,5.7,.83,-1.8,0,2,.40), (-.70,.65,7.45,.93,-.9,0,2,.40), (.70,.5,6.9,.89,.5,0,5,.4), (2.0,.5,5.6,.8,1.7,0,5,.42)],
        ('Red',4): [(-2.1,-.5,4.6,.83,2.0,.25,25,.63), (-.65,.6,7.25,1.0,2.65,.35,28,.68), (1.5,1.1,5.8,.82,1.9,.2,28,.70)],
    }
    rng = random.Random((1 if kind == 'Blue' else 2)*1000 + family*31)
    parts = []
    idx = 0
    for spec in presets[(kind,family)]:
        x,y,h,r,lx,ly,yaw,depth = spec
        parts.append(crystal(kind,f'Core_{idx:02d}','Core',x,y,h,r,(lx,ly),yaw,depth,1200+family*100+idx))
        idx += 1
    for i,(x,y) in enumerate([(-1.2,-1.0),(1.1,-1.2),(.1,-1.8)]):
        parts.append(crystal(kind,f'Seed_{i:02d}','Seed',x,y,rng.uniform(1.05,1.55),
                             .32 if kind=='Blue' else .25,(x*.18,y*.12),i*23,1 if kind=='Blue' else .72,1700+family*20+i))
    # Authored silhouettes dominate; secondary crystals are deterministic variation.
    for i in range(11 if kind == 'Blue' else 9):
        a = math.tau * i/(11 if kind == 'Blue' else 9) + rng.uniform(-.18,.18)
        r = rng.uniform(1.65,2.9)
        x, y = math.cos(a)*r, math.sin(a)*r
        front = (1-math.sin(a))*0.5
        h = rng.uniform(2.1,3.75) + (1-front)*.5
        rr = rng.uniform(.44,.72) if kind == 'Blue' else rng.uniform(.34,.55)
        lean = (math.cos(a)*rng.uniform(.7,1.5), math.sin(a)*rng.uniform(.7,1.25))
        if family == 3:
            y *= .72
        if kind == 'Red' and family == 4:
            lean = (1.35, .22)
        parts.append(crystal(kind,f'Mid_{i:02d}','Middle',x,y,h,rr,lean,rng.uniform(0,65),.94 if kind=='Blue' else .62,2000+family*100+i))
    for i in range(18 if kind == 'Blue' else 15):
        a = math.tau*i/(18 if kind=='Blue' else 15) + rng.uniform(-.12,.12)
        r = rng.uniform(3.20,4.1)
        h = rng.uniform(.8,1.8)
        parts.append(crystal(kind,f'Edge_{i:02d}','Edge',math.cos(a)*r,math.sin(a)*r,h,rng.uniform(.24,.43),
                             (math.cos(a)*.85,math.sin(a)*.85),rng.uniform(0,70),1 if kind=='Blue' else .64,3000+family*100+i))
    # Grouped boulders, not a single round pedestal. Every bottom sits on local Z=0.
    for i in range(7):
        a = i*math.tau/7+.3
        r = 0 if i==0 else rng.uniform(.9,1.65)
        parts.append(rock(kind,f'Rock_Core_{i:02d}','RockCore',math.cos(a)*r,math.sin(a)*r,
                          rng.uniform(.86,1.48),rng.uniform(.8,1.25),rng.uniform(.9,1.45),a,4000+family*100+i))
    for i in range(13):
        a = i*math.tau/13+.12
        r = rng.uniform(2.35,3.25)
        parts.append(rock(kind,f'Rock_Mid_{i:02d}','RockMiddle',math.cos(a)*r,math.sin(a)*r,
                          rng.uniform(.75,1.24),rng.uniform(.70,1.02),rng.uniform(.68,1.17),a,5000+family*100+i))
    for i in range(14):
        a = i*math.tau/14 + rng.uniform(-.18,.18)
        r = rng.uniform(3.85,4.65)
        parts.append(rock(kind,f'Rock_Edge_{i:02d}','RockEdge',math.cos(a)*r,math.sin(a)*r,
                          rng.uniform(.42,.89),rng.uniform(.40,.77),rng.uniform(.34,.73),a,6000+family*100+i))
    # A single family scale is baked into all stages; no stage re-centering.
    # Broad fans and arc ridges also differ in plan-view silhouette.
    warp = (1.10,.73) if family==3 else ((1.04,.88) if family in (2,4) else (1,1))
    for p in parts:
        p.vertices=[(x*warp[0],y*warp[1],z) for x,y,z in p.vertices]
    vv = [v for p in parts for v in p.vertices]
    width = max(max(v[0] for v in vv)-min(v[0] for v in vv),max(v[1] for v in vv)-min(v[1] for v in vv))
    sx, sz = 12/width, 8/max(v[2] for v in vv)
    for p in parts:
        p.vertices = [(x*sx,y*sx,z*sz) for x,y,z in p.vertices]
    return parts


def parts_for_state(parts, kind, state):
    if state == 'Full':
        return parts
    cap = (.025,.26,.38) if kind == 'Blue' else (.36,.030,.016)
    out = []
    for p in parts:
        number = int(p.name.rsplit('_',1)[-1])
        if p.role == 'Seed':
            out.append(p)
            continue
        if p.role in ('Edge','RockEdge'):
            continue
        if state == 'Partial':
            if p.role == 'Core':
                top = max(v[2] for v in p.vertices)
                if number%2==1 and top<6.65:
                    out.append(p)
                else:
                    out.append(p.clipped(top*(.78 if number%2==0 else .86),(.17,-.14),cap))
            elif p.role == 'Middle':
                if number%3 != 1:
                    out.append(p if number%4==2 else p.clipped(max(v[2] for v in p.vertices)*.68,(-.18,.10),cap))
            elif p.role == 'RockMiddle':
                if number%3 != 1:
                    out.append(p)
            else:
                out.append(p)
        else:
            if p.role == 'Core':
                out.append(p.clipped(2.45 - .17*(number%3),(.04,-.05),cap))
            elif p.role == 'Middle' and number in (2,5,8):
                out.append(p.clipped(1.05 + .09*(number%3),(.05,.04),cap))
            elif p.role == 'RockCore':
                out.append(p)
    return out


def make_unit(kind, family, state, parts, materials, coll):
    name = f'Ore_{kind}_{family:02d}_{state}'
    verts, faces, colors, glows, material_ids, groups = [], [], [], [], [], []
    for p in parts:
        start = len(verts)
        verts.extend(p.vertices)
        faces.extend([tuple(start+i for i in f) for f in p.faces])
        colors.extend(p.colors)
        glows.extend(p.glow)
        material_ids.extend([p.material]*len(p.faces))
        groups.append((p.name, list(range(start,len(verts)))))
    mesh = owned(bpy.data.meshes.new(name + '_Mesh'))
    mesh.from_pydata(verts, [], faces)
    mesh.validate(verbose=False)
    mesh.update()
    obj = owned(bpy.data.objects.new(name, mesh))
    coll.objects.link(obj)
    for mat in materials:
        mesh.materials.append(mat)
    col = mesh.color_attributes.new(name='OreColor',type='FLOAT_COLOR',domain='CORNER')
    glow = mesh.attributes.new(name='OreGlow',type='FLOAT',domain='FACE')
    uv = mesh.uv_layers.new(name='UV_LocalTriplanar')
    # Surface projection is stable in object space and therefore across state swaps.
    for i, poly in enumerate(mesh.polygons):
        poly.material_index = material_ids[i]
        poly.use_smooth = False
        glow.data[i].value = glows[i]
        dominant = max(range(3),key=lambda j: abs(poly.normal[j]))
        axes = ((1,2),(0,2),(0,1))[dominant]
        for li in poly.loop_indices:
            col.data[li].color = (*colors[i],1)
            p = mesh.vertices[mesh.loops[li].vertex_index].co
            uv.data[li].uv = (p[axes[0]]/12+.5, p[axes[1]]/12+.5)
    for group, indices in groups:
        obj.vertex_groups.new(name=group).add(indices,1.0,'REPLACE')
    obj['ore_type'] = kind
    obj['ore_family'] = family
    obj['ore_state'] = state
    obj['ore_family_name'] = CHINESE_NAMES[kind][family-1]
    obj['ore_role'] = 'unit_asset'
    obj['origin_contract'] = 'Ground pivot (0,0,0), Z up, +Y forward. Metres. Scale baked.'
    obj['depletion_contract'] = 'Full > Partial > Remnant; zero destroys the entity. No empty mesh.'
    obj['part_count'] = len(parts)
    mesh.calc_loop_triangles()
    obj['triangles'] = len(mesh.loop_triangles)
    return obj


def mesh_object(name, verts, faces, mat, coll):
    mesh = owned(bpy.data.meshes.new(name+' Mesh'))
    mesh.from_pydata(verts,[],faces)
    mesh.update()
    o = owned(bpy.data.objects.new(name,mesh))
    coll.objects.link(o)
    if mat:
        mesh.materials.append(mat)
    return o


def floor(name, xmin,xmax,ymin,ymax,mat,coll,z=-.04):
    return mesh_object(name,[(xmin,ymin,z),(xmax,ymin,z),(xmax,ymax,z),(xmin,ymax,z)],[(0,1,2,3)],mat,coll)


def text_object(name,body,loc,size,mat,coll,align='CENTER',rotation=None):
    data = owned(bpy.data.curves.new(name,'FONT'))
    data.body = body
    data.align_x = align
    data.size = size
    data.extrude = 0
    o = owned(bpy.data.objects.new(name,data))
    coll.objects.link(o)
    o.location = loc
    o.visible_shadow = False
    if rotation:
        o.rotation_euler = rotation
    data.materials.append(mat)
    return o


def camera(scene,name,position,target,ortho):
    data = owned(bpy.data.cameras.new(name))
    data.type = 'ORTHO'
    data.ortho_scale = ortho
    data.clip_end = 1500
    o = owned(bpy.data.objects.new(name,data))
    scene.collection.objects.link(o)
    aim_camera(o,position,target,ortho)
    scene.camera = o
    return o


def aim_camera(cam,position,target,ortho):
    cam.location = position
    cam.rotation_euler = (Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler()
    cam.data.ortho_scale = ortho


def light(scene,name,loc,energy,size,color,target=(0,0,0)):
    data = owned(bpy.data.lights.new(name,'AREA'))
    data.energy = energy
    data.shape = 'DISK'
    data.size = size
    data.color = color
    o = owned(bpy.data.objects.new(name,data))
    scene.collection.objects.link(o)
    o.location = loc
    o.rotation_euler = (Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
    return o


def studio_lights(scene,scale=1,center=(0,0,0)):
    center = Vector(center)
    for name,loc,power,size,color in (
        ('Key',(-14,-18,29),9500,12,(1,.89,.77)),
        ('Fill',(18,-4,17),3800,14,(.64,.82,1)),
        ('Rim',(4,19,26),14500,11,(.79,.90,1)),
        ('Crystal strip',(-6,-15,17),3400,7,(.91,.98,1)),
    ):
        light(scene,scene.name+' '+name,center+Vector(loc)*scale,power*scale*scale,size*scale,color,center)


def linked_unit(source,coll,name,position,angle=0,scale=1):
    obj = owned(bpy.data.objects.new(name,source.data))
    coll.objects.link(obj)
    obj.location = position
    obj.rotation_euler[2] = angle
    obj.scale = (scale,)*3
    obj['ore_role'] = 'review_instance'
    obj['source_asset'] = source.name
    obj['ore_type'] = source['ore_type']
    obj['ore_state'] = source['ore_state']
    return obj


def field_ground_material():
    m = plain_material('M_Review_FieldGround',(.09,.12,.07),.94)
    nt=m.node_tree
    bs=next(n for n in nt.nodes if n.type=='BSDF_PRINCIPLED')
    noise=nt.nodes.new('ShaderNodeTexNoise')
    noise.inputs['Scale'].default_value=0.53
    noise.inputs['Detail'].default_value=2.0
    tex=nt.nodes.new('ShaderNodeTexCoord')
    nt.links.new(tex.outputs['Object'],noise.inputs['Vector'])
    ramp=nt.nodes.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].position=.23
    ramp.color_ramp.elements[0].color=(.027,.043,.020,1)
    ramp.color_ramp.elements[1].position=.78
    ramp.color_ramp.elements[1].color=(.12,.16,.060,1)
    e=ramp.color_ramp.elements.new(.47)
    e.color=(.057,.079,.03,1)
    nt.links.new(noise.outputs['Fac'],ramp.inputs[0])
    nt.links.new(ramp.outputs['Color'],bs.inputs['Base Color'])
    return m


def make_presentations(assets):
    review = bpy.data.scenes[SCENE_REVIEW]
    decor = collection('Review • Labels and Ground',review.collection)
    ground = plain_material('M_Review_Slate',(.041,.055,.068),.87)
    panel = plain_material('M_Review_Tile',(.055,.072,.089),.8)
    label = plain_material('M_Review_Label',(.55,.68,.76),.8,.25)
    blue = plain_material('M_Review_BlueAccent',(.025,.48,.7),.5,.5)
    red = plain_material('M_Review_RedAccent',(.72,.055,.022),.5,.5)
    floor('Review background',-130,130,-120,120,ground,decor)
    for kind, center, accent in [('Blue',-28,blue),('Red',28,red)]:
        text_object(kind+' Title',kind.upper()+'  /  UNIT ORES',(center,38,.04),1.70,accent,decor)
        for j,state in enumerate(STATES):
            x=center+(j-1)*16
            text_object(kind+state+' Column',state.upper(),(x,33,.045),.95,label,decor)
        for f in range(1,5):
            y=24-(f-1)*17
            for j,state in enumerate(STATES):
                x=center+(j-1)*16
                obj=assets[(kind,f,state)]
                obj.location=(x,y,0)
                floor(f'Tile {kind} {f} {state}',x-7.35,x+7.35,y-7.15,y+7.15,panel,decor,z=-.018)
                text_object(f'ID {kind} {f} {state}',f'{kind[0]}{f:02d}  /  {state.upper()}',(x,y-6.5,.028),.68,label,decor)
            text_object(f'Family {kind} {f}',f'{f:02d}   {FAMILY_NAMES[kind][f-1].upper()}',(center,y+7.7,.04),.73,accent,decor)
    cam=camera(review,'Review Camera',(0,-88,140),(0,4,0),112)
    review.render.resolution_x=2200
    review.render.resolution_y=1500
    studio_lights(review,3)
    review['review_instructions']='Blue left, Red right. Four family rows, Full/Partial/Remnant columns. Select one Ore_* object and Numpad Period for detail. Other scenes contain family and cluster examples.'
    detail=new_scene(SCENE_DETAIL)
    coll=collection('Studio • Linked Units',detail.collection)
    dc=collection('Studio • Ground',detail.collection)
    floor('Studio floor',-200,200,-200,200,ground,dc)
    for kind in ('Blue','Red'):
        for f in range(1,5):
            for j,state in enumerate(STATES):
                o=linked_unit(assets[(kind,f,state)],coll,f'Studio_{kind}_{f:02d}_{state}',((j-1)*15,0,0))
                o.hide_render=not (kind=='Blue' and f==1)
                o.hide_viewport=not (kind=='Blue' and f==1)
    for j,state in enumerate(STATES):
        label_obj=text_object('Studio label '+state,state.upper(),((j-1)*15,-8,.02),.75,label,dc)
        label_obj.hide_render=True
    camera(detail,'Family Camera',(15,-52,35),(0,0,2.6),49)
    detail.render.resolution_x=1800
    detail.render.resolution_y=1000
    studio_lights(detail,1.30)
    fields=new_scene(SCENE_FIELD)
    fc=collection('Fields • Presentation Ground',fields.collection)
    fg=field_ground_material()
    floor('Field ground',-200,200,-160,160,fg,fc)
    rng=random.Random(102609)
    for kind,cx in [('Blue',-40),('Red',40)]:
        group=collection(f'{kind} • Static cluster example',fields.collection)
        positions=[(-4.8,-2.5),(4.8,-2.1),(0,6.1)]
        for i,(x,y) in enumerate(positions):
            linked_unit(assets[(kind,i+1,'Full')],group,f'Field_{kind}_Core_{i:02d}',(cx+x,y,0),rng.uniform(-.5,.5))
        for i in range(8):
            a=math.tau*i/8+.22
            r=rng.uniform(13.0,14.3)
            linked_unit(assets[(kind,i%4+1,'Partial')],group,f'Field_{kind}_Middle_{i:02d}',(cx+math.cos(a)*r,math.sin(a)*r,0),rng.uniform(-math.pi,math.pi))
        for i in range(15):
            a=math.tau*i/15+.1+rng.uniform(-.055,.055)
            r=rng.uniform(20.5,23)
            linked_unit(assets[(kind,i%4+1,'Remnant')],group,f'Field_{kind}_Edge_{i:02d}',(cx+math.cos(a)*r,math.sin(a)*r,0),rng.uniform(-math.pi,math.pi))
    camera(fields,'Cluster Camera',(-5,-55,64),(-40,0,0),62)
    fields.render.resolution_x=1800
    fields.render.resolution_y=1500
    studio_lights(fields,3)
    return review,detail,fields


def report_assets(assets):
    rows=[]
    for (kind,f,state),o in assets.items():
        m=o.data
        m.calc_loop_triangles()
        bm=bmesh.new()
        bm.from_mesh(m)
        bad_edges=sum(not e.is_manifold for e in bm.edges)
        inconsistent=sum(not e.is_contiguous for e in bm.edges)
        degenerate=sum(fa.calc_area()<1e-10 for fa in bm.faces)
        volume=bm.calc_volume(signed=True)
        bm.free()
        v=[v.co for v in m.vertices]
        bounds=[[min(p[a] for p in v),max(p[a] for p in v)] for a in range(3)]
        rows.append({'name':o.name,'ore_type':kind,'family':f,'family_name':CHINESE_NAMES[kind][f-1],
                     'state':state,'vertices':len(m.vertices),'triangles':len(m.loop_triangles),
                     'material_slots':len(m.materials),'bounds_local_m':bounds,
                     'dimensions_m':[b[1]-b[0] for b in bounds],
                     'part_count':o['part_count'],'non_manifold_edges':bad_edges,
                     'inconsistent_edges':inconsistent,'degenerate_faces':degenerate,
                     'signed_volume_m3':volume})
    errors=[]
    for row in rows:
        if row['triangles']>15000 or row['material_slots']!=2 or row['non_manifold_edges'] or row['inconsistent_edges'] or row['degenerate_faces'] or row['signed_volume_m3']<=0:
            errors.append(row['name']+': topology/material/budget failure')
        if abs(row['bounds_local_m'][2][0])>1e-6:
            errors.append(row['name']+': ground mismatch')
    for kind in ('Blue','Red'):
        for f in range(1,5):
            group=[next(r for r in rows if r['ore_type']==kind and r['family']==f and r['state']==s) for s in STATES]
            for prev,nxt in zip(group,group[1:]):
                if nxt['triangles']>prev['triangles'] or nxt['dimensions_m'][2]>=prev['dimensions_m'][2] or nxt['signed_volume_m3']>=prev['signed_volume_m3']:
                    errors.append(nxt['name']+': state progression failure')
    continuity=[]
    def group_positions(o):
        groups={g.index:g.name for g in o.vertex_groups}
        out={name:[] for name in groups.values()}
        for v in o.data.vertices:
            for g in v.groups:
                out[groups[g.group]].append(tuple(round(c,5) for c in v.co))
        return out
    for kind in ('Blue','Red'):
        for family in range(1,5):
            master=group_positions(assets[(kind,family,'Full')])
            for state in ('Partial','Remnant'):
                o=assets[(kind,family,state)]
                current=group_positions(o)
                preserved=0
                for name,positions in current.items():
                    if name.startswith(('Rock','Seed')):
                        if set(positions)!=set(master[name]):
                            errors.append(o.name+': persistent part moved '+name)
                        preserved+=1
                    else:
                        baseline={p for p in master[name] if abs(p[2])<1e-5}
                        if not baseline.issubset(set(positions)):
                            errors.append(o.name+': crystal ground ring moved '+name)
                continuity.append({'name':o.name,'unchanged_whole_parts':preserved,
                                   'retained_part_groups':len(current),'ground_rings_match_full':True})
    report={'generator':OWNER,'blender_version':bpy.app.version_string,'units':'metres','asset_count':len(rows),
            'materials':'Crystal slot 0 / Rock slot 1. OreColor corner colour / OreGlow face scalar.',
            'errors':errors,'models':rows,'continuity':continuity,
            'note':'Volume sums watertight disconnected art parts; internal part overlaps are intentional and not resource amounts.'}
    (OUT/'asset_manifest.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
    return report


def set_review_view():
    scene=bpy.data.scenes[SCENE_REVIEW]
    for w in bpy.context.window_manager.windows:
        w.scene=scene
        for a in w.screen.areas:
            if a.type=='VIEW_3D':
                s=a.spaces.active
                s.shading.type='RENDERED'
                s.shading.use_scene_world=True
                s.shading.use_scene_lights=True
                s.shading.studiolight_rotate_z=.4
                s.overlay.show_overlays=False
                s.region_3d.view_perspective='CAMERA'
                s.region_3d.view_camera_zoom=6
    for o in scene.objects:
        o.select_set(False)
    o=bpy.data.objects.get('Ore_Blue_01_Full')
    if o:
        scene.view_layers[0].objects.active=o
        o.select_set(True)


def save():
    ASSET.parent.mkdir(parents=True,exist_ok=True)
    set_review_view()
    bpy.ops.wm.save_as_mainfile(filepath=str(ASSET),check_existing=False)


def build():
    OUT.mkdir(parents=True,exist_ok=True)
    clear_generated()
    review=new_scene(SCENE_REVIEW)
    assets={}
    for kind in ('Blue','Red'):
        coll=collection(f'{kind} • 12 Unit Assets',review.collection)
        mats=(ore_material(kind,True),ore_material(kind,False))
        for f in range(1,5):
            parts=family_parts(kind,f)
            for state in STATES:
                assets[(kind,f,state)]=make_unit(kind,f,state,parts_for_state(parts,kind,state),mats,coll)
    make_presentations(assets)
    report=report_assets(assets)
    if report['errors']:
        raise RuntimeError(report['errors'])
    source=Path(__file__) if '__file__' in globals() else ROOT/'Scripts/Blender/build_ore_resource_variants.py'
    txt=owned(bpy.data.texts.new('README • Ore resource models'))
    txt.write('GuLiStrike | 24 unit ore variants\n\nOne cluster = many unit ores.\nEach unit: Full > Partial > Remnant > entity destroyed at zero.\nNo empty/depleted asset.\n\nScene: ORE • 24 Unit Review — 4 family rows x 3 state columns per ore type.\nScene: ORE • Family Studio — isolated state comparison.\nScene: ORE • Cluster Examples — linked static instances, center to edge size gradient.\n\nAll 24 Ore_* assets: editable meshes, metres, ground pivot, baked scale, 2 material slots.\nOreColor and OreGlow are authored attributes; material graph shows their use.\nVertex groups retain individual crystals and rocks for editing.\nLocal projection UVs are stable across stage changes; they are not a unique baked atlas.\n\nSource script: '+str(source)+'\nFinal UE export/material and resource logic follow visual approval.\n')
    readme=ASSET.parent/'README.md'
    if readme.exists():
        txt.write('\n\n'+readme.read_text(encoding='utf-8'))
    if source.exists():
        code=owned(bpy.data.texts.new('build_ore_resource_variants.py'))
        code.write(source.read_text(encoding='utf-8'))
        code.filepath=str(source)
    save()
    return {'assets':len(assets),'triangles_range':[min(r['triangles'] for r in report['models']),max(r['triangles'] for r in report['models'])],
            'errors':report['errors'],'blend':str(ASSET),'scene':review.name}


def overlay(scene,title,subtitle,footer,accent_kind):
    # Camera-space type stays legible at every oblique view, without image editing.
    old=bpy.data.collections.get('ORE • Render Captions')
    if old:
        for o in list(old.objects):
            data=o.data
            bpy.data.objects.remove(o,do_unlink=True)
            if data.users==0:
                bpy.data.curves.remove(data)
        bpy.data.collections.remove(old)
    c=collection('ORE • Render Captions',scene.collection)
    cam=scene.camera
    ratio=scene.render.resolution_x/scene.render.resolution_y
    width=cam.data.ortho_scale*min(1,ratio)
    height=width/ratio
    def unlit(name,color):
        m=bpy.data.materials.get(name)
        if not m:
            m=owned(bpy.data.materials.new(name))
            m.use_nodes=True
            m.node_tree.nodes.clear()
            emission=m.node_tree.nodes.new('ShaderNodeEmission')
            emission.inputs[0].default_value=(*color,1)
            emission.inputs[1].default_value=1
            out=m.node_tree.nodes.new('ShaderNodeOutputMaterial')
            path=m.node_tree.nodes.new('ShaderNodeLightPath')
            transparent=m.node_tree.nodes.new('ShaderNodeBsdfTransparent')
            shader=m.node_tree.nodes.new('ShaderNodeMixShader')
            m.node_tree.links.new(path.outputs['Is Camera Ray'],shader.inputs[0])
            m.node_tree.links.new(transparent.outputs[0],shader.inputs[1])
            m.node_tree.links.new(emission.outputs[0],shader.inputs[2])
            m.node_tree.links.new(shader.outputs[0],out.inputs['Surface'])
        return m
    mat=unlit('M_Caption_Light',(.55,.67,.76))
    accent=unlit('M_Caption_'+accent_kind,(.02,.5,.8) if accent_kind=='Blue' else (.8,.10,.035))
    def line(name,body,x,y,size,material,align='LEFT'):
        o=text_object(name,body,(0,0,0),size,material,c,align)
        o.parent=cam
        o.location=(x,y,-.5)
    line('Render heading',title,-width*.445,height*.411,width*.025,accent)
    line('Render subtitle',subtitle,-width*.444,height*.363,width*.0105,mat)
    if footer=='STAGES':
        for i,state in enumerate(STATES):
            local=cam.rotation_euler.to_matrix().transposed() @ (Vector(((i-1)*15,0,0))-cam.location)
            line('Stage '+state,state.upper(),local.x,-height*.33,width*.016,mat,'CENTER')
        line('Render footer','GULISTRIKE  /  v01     •     UNIT ASSET REVIEW',-width*.444,-height*.455,width*.009,mat)
    else:
        line('Render footer',footer,-width*.444,-height*.455,width*.0103,mat)


def render_one(job,quality=100):
    typ,kind,f=job
    if typ=='overview':
        s=bpy.data.scenes[SCENE_REVIEW]
        cx=-28 if kind=='Blue' else 28
        other='Red' if kind=='Blue' else 'Blue'
        for o in s.objects:
            if o.get('ore_generator')==OWNER:
                if other in o.name:
                    o.hide_render=True
                elif kind in o.name:
                    o.hide_render=False
        aim_camera(s.camera,(cx,-65,118),(cx,3,0),74)
        s.render.resolution_x=1600
        s.render.resolution_y=1800
        # The physical labels are useful in Blender; only footer needed on overview.
        overlay(s,'','',f'GULISTRIKE  /  {kind.upper()} ORE   •   12 UNIT ASSETS   /   v01',kind)
        filename=f'01_{kind}_Overview.png' if kind=='Blue' else '02_Red_Overview.png'
    elif typ=='family':
        s=bpy.data.scenes[SCENE_DETAIL]
        for o in s.objects:
            if o.get('ore_role')=='review_instance':
                visible=o['source_asset'].startswith(f'Ore_{kind}_{f:02d}_')
                o.hide_render=not visible
                o.hide_viewport=not visible
        aim_camera(s.camera,(11,-52,35),(0,0,2.4),48)
        s.render.resolution_x=1800
        s.render.resolution_y=1000
        overlay(s,f'{kind.upper()}  {f:02d}  /  {FAMILY_NAMES[kind][f-1].upper()}',
                'UNIT ORE   /   MATCHED DEPLETION STATES   /   METRES',
                'STAGES',kind)
        filename=f'{2+f if kind=="Blue" else 6+f:02d}_{kind}_{f:02d}_Stages.png'
    elif typ=='field':
        s=bpy.data.scenes[SCENE_FIELD]
        cx=-40 if kind=='Blue' else 40
        aim_camera(s.camera,(cx+33,-52,62),(cx,0,0),66)
        s.render.resolution_x=1800
        s.render.resolution_y=1500
        overlay(s,f'{kind.upper()}  /  CLUSTER STUDY','STATIC ASSEMBLY   /   LINKED UNIT ORES',
                'FULL CORE   >   PARTIAL MIDDLE   >   REMNANT EDGE',kind)
        filename='11_Blue_Cluster.png' if kind=='Blue' else '12_Red_Cluster.png'
    else:
        raise ValueError(typ)
    s.render.resolution_percentage=quality
    s.render.filepath=str(OUT/filename)
    bpy.ops.render.render(write_still=True,scene=s.name)
    return {'file':filename,'bytes':(OUT/filename).stat().st_size,'scene':s.name}


def render_all(pilot=False):
    jobs=[('family','Blue',1),('family','Red',1)] if pilot else (
        [('overview',k,0) for k in ('Blue','Red')]+[('family',k,f) for k in ('Blue','Red') for f in range(1,5)]+[('field',k,0) for k in ('Blue','Red')])
    done=[]
    for job in jobs:
        start=time.time()
        record=render_one(job,60 if pilot else 100)
        record['seconds']=round(time.time()-start,2)
        done.append(record)
        (OUT/('pilot_render_manifest.json' if pilot else 'render_manifest.json')).write_text(json.dumps(done,indent=2),encoding='utf-8')
        print('ORE_RENDER_DONE '+json.dumps(record),flush=True)
    return done


def render_qa():
    """Requested top and close/low checks, stored separately from 12 deliverables."""
    qadir=OUT/'qa'
    qadir.mkdir(exist_ok=True)
    done=[]
    s=bpy.data.scenes[SCENE_REVIEW]
    for o in s.objects:
        if 'Blue' in o.name or 'Red' in o.name:
            o.hide_render=False
    aim_camera(s.camera,(0,3,155),(0,3,0),112)
    s.render.resolution_x=2200
    s.render.resolution_y=1600
    s.render.resolution_percentage=100
    overlay(s,'','','TOP VIEW  /  ALL 24 UNITS', 'Blue')
    s.render.filepath=str(qadir/'QA_01_AllUnits_Top.png')
    bpy.ops.render.render(write_still=True,scene=s.name)
    done.append(s.render.filepath)
    s=bpy.data.scenes[SCENE_DETAIL]
    for kind,f,state,position,target,scale in [
        ('Blue',3,'Partial',(10,-14,13),(0,0,2.5),16),
        ('Red',4,'Partial',(-11,-16,6),(0,0,2.5),16),
        ('Red',3,'Remnant',(9,-12,8),(0,0,1.0),12),
    ]:
        for o in s.objects:
            if o.get('ore_role')=='review_instance':
                visible=o['source_asset']==f'Ore_{kind}_{f:02d}_{state}'
                o.hide_render=not visible
                if visible:
                    o.location=(0,0,0)
        position=Vector(target)+(Vector(position)-Vector(target))*3
        aim_camera(s.camera,position,target,scale)
        s.render.resolution_x=1400
        s.render.resolution_y=1200
        s.render.resolution_percentage=100
        overlay(s,f'{kind.upper()} {f:02d} / {state.upper()}','','CAPS / GROUND CONTACT / FACETS',kind)
        s.render.filepath=str(qadir/f'QA_{kind}_{f:02d}_{state}_Close.png')
        bpy.ops.render.render(write_still=True,scene=s.name)
        done.append(s.render.filepath)
    (qadir/'qa_render_manifest.json').write_text(json.dumps(done,indent=2),encoding='utf-8')
    return done


if __name__=='__main__':
    args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
    if '--qa' in args:
        render_qa()
    elif '--render' in args or '--pilot' in args:
        render_all(pilot='--pilot' in args)
    else:
        print(json.dumps(build(),ensure_ascii=False))
