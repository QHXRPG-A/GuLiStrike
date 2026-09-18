"""Batch04 approved surface pass, retaining exact original meshes and rigs."""
import ast
import collections
import importlib.util
import inspect
import json
import math
import os
import sys
from pathlib import Path
import bpy
import numpy as np
from mathutils import Vector
from mathutils.kdtree import KDTree

PROJECT = Path('D:/UE5.7/test1')
ROOT = PROJECT / 'ArtSource/Ships/ShipComponentStyle_20260917/Batch04'
VERSION = os.environ.get('SHIP_BATCH04_VERSION', 'v1')
KEYS = ('Bottom_Twin_Barrel_Turret', 'High_Rate_Fire_Cannon', 'Incendiary_Bomb_LaunchBay', 'Missile_Bay')
RIGGED = KEYS[:2]
spec = importlib.util.spec_from_file_location('batch04_base', PROJECT / 'Scripts/Blender/style_ship_component_original_materials.py')
s = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s)
s.ROOT = ROOT
s.OUT = ROOT / 'Production' / VERSION
s.TEX = s.OUT / 'Textures'
s.PRE = ROOT / 'Previews' / VERSION
s.SNAP = json.loads((ROOT / 'Source/source_snapshot_v1.json').read_text(encoding='utf-8'))
s.SOURCE_FILES = {k: ROOT / (f'Source/ExistingAuthoring/SC_{k}.blend' if k in RIGGED else f'Source/FBX_static_v1/SM_SC_{k}.fbx') for k in KEYS}
s.PALETTE.update({'SteelBlue': '4C7891', 'BurntOrange': 'C66B38'})
for folder in (s.OUT, s.TEX, s.PRE):
    folder.mkdir(parents=True, exist_ok=True)
APPROVAL = json.loads((ROOT / 'approval_A_20260917.json').read_text(encoding='utf-8'))
assert APPROVAL['stage'] == 'A' and APPROVAL['decision'] == 'approved'
for key, part in APPROVAL['parts'].items():
    for field in ('reference_sheet', 'original_authoring_source', 'original_inspection_blend', 'original_geometry_snapshot'):
        record = part[field]
        assert s.filehash(ROOT / record['path']) == record['sha256'], (key, field)

# Reuse the established static loading, coplanar assignment and mask selection.
helper_path = PROJECT / 'Scripts/Blender/style_ship_component_batch03_materials.py'
tree = ast.parse(helper_path.read_text(encoding='utf-8'))
for node in tree.body:
    if isinstance(node, ast.FunctionDef) and node.name in ('load_original', 'coherent_planes', 'line_faces', 'in_polygon'):
        if node.name == 'load_original':
            node.name = 'load_static_original'
        exec(compile(ast.Module(body=[node], type_ignores=[]), str(helper_path), 'exec'), globals())
original_load = s.load_original

def load_original(key):
    result=original_load(key) if key in RIGGED else load_static_original(key)
    if result[1]:result[1]['ship_component_key']=key
    return result

original_pose=s.pose
def pose(arm,angle,pivot):
    key=arm.get('ship_component_key',bpy.context.scene.name)
    return original_pose(arm,-angle if key.startswith('Bottom_') else angle,pivot)

# Bottom-mounted guns use the existing outward -Z positive-elevation convention.
# Reuse the current 91-pose audit with its expected rigid transform sign matched.
audit_source=inspect.getsource(s.motion_audit)
audit_source=audit_source.replace('math.radians(-angle)','math.radians(-angle * (-1 if key.startswith("Bottom_") else 1))')
audit_source=audit_source.replace("'sign':1", "'sign':(-1 if key.startswith('Bottom_') else 1)")
exec(compile(audit_source,__file__,'exec'),s.__dict__)

def material_labels(key, mesh, seeds):
    scale = 1 if key in RIGGED else .01
    labels = []
    for p in mesh.polygons:
        seed = collections.Counter(seeds[v] for v in p.vertices).most_common(1)[0][0]
        c, n = p.center * scale, p.normal
        label = 'Slate'
        if key == 'Bottom_Twin_Barrel_Turret':
            if seed == 0:
                label = 'Pearl' if n.z > .3 else 'Navy'
            elif seed in (169, 287):
                label = 'Ochre'
            elif seed in (104, 301):
                label = 'Slate'
            elif seed in (199, 642):
                label = 'Slate' if n.z > -.2 else 'Navy'
            elif seed in (438, 522, 538, 554, 578, 594, 610, 626, 161, 191, 412, 570):
                label = 'Steel'
        elif key == 'High_Rate_Fire_Cannon':
            if seed in (0, 389):
                label = 'SteelBlue'
            elif seed in (12, 43, 60):
                label = 'Pearl' if n.z > .1 or c.z > .8 else 'Navy'
            elif seed == 324:
                label = 'Pearl' if n.z > -.2 else 'Navy'
            elif seed == 518:
                label = 'Pearl' if n.z > .99 and c.z < 3.5 else 'SteelBlue'
            elif seed in (168, 223, 251, 445, 353, 365, 377, 409, 421, 433):
                label = 'Steel'
            elif seed in (29, 98, 196, 345, 401, 566):
                label = 'Navy'
        elif key == 'Incendiary_Bomb_LaunchBay':
            # Original repeated part classes, no extra geometry or decorative strips.
            if seed in (2058, 2222):
                label = 'BurntOrange' if abs(n.y) > .8 else 'Slate'
            elif seed in (1182, 1332, 1492, 1778, 1812):
                label = 'BurntOrange' if n.z > .2 else 'Navy'
            elif seed in (1150, 1300, 1460, 1620, 1746, 1844, 1984, 2114, 2202, 2342):
                label = 'BurntOrange'
            elif seed in (1134, 1284, 1444, 1604, 1730):
                label = 'Pearl' if n.z > .3 else 'Navy'
            elif seed in (1168, 1318, 1478, 1764, 1798):
                label = 'Pearl' if n.z > -.1 else 'Navy'
            elif seed in (1062, 1098, 1202, 1248, 1362, 1408, 1522, 1568, 1648, 1694):
                label = 'Steel'
            elif seed in (1864, 1918, 2004, 2266):
                label = 'Pearl' if abs(n.y) > .7 else 'Slate'
            elif seed < 1062 or seed in (2438, 2462, 2488, 2512, 2536, 2562, 2588, 2612):
                label = 'BurntOrange' if n.z > .7 and c.z > 1.7 else ('Pearl' if n.z > .2 else 'Slate')
            elif c.z < -3:
                label = 'Navy'
        else:
            if seed in (184, 208):
                label = 'Brick' if n.z > .7 else 'Pearl'
            elif seed in (0, 56):
                label = 'Pearl' if n.z > .3 or abs(n.y) > .8 else 'Navy'
            elif seed == 32:
                label = 'Slate'
            else:
                label = 'Steel'
        labels.append(label)
    labels = coherent_planes(mesh, seeds, labels)
    names = list(s.PALETTE)
    index = mesh.attributes.new('SC_PaintPaletteIndex', 'INT', 'FACE')
    colors = mesh.color_attributes.new(name='SC_EditablePaintColor', type='FLOAT_COLOR', domain='CORNER')
    for p, label in zip(mesh.polygons, labels):
        index.data[p.index].value = names.index(label)
        for loop in p.loop_indices:
            colors.data[loop].color = s.linear(s.PALETTE[label])
    s.dump(s.OUT / f'{key}_paint_regions.json', {'reference_version': APPROVAL['parts'][key]['reference_version'],
        'palette_sRGB': s.PALETTE, 'palette_index_names': names, 'face_labels': labels,
        'face_count_by_color': dict(collections.Counter(labels)), 'geometry_modified': False})
    return labels

original_outline = s.outline
def outline(body, key):
    shell = original_outline(body, key)
    width = max(body.dimensions) * .0012
    shell.modifiers['Outline_Width'].strength = -width / body.scale.x
    shell['outline_width_m'] = width
    return shell

original_save = s.save_image
def save_image(*args, **kwargs):
    im = original_save(*args, **kwargs)
    im.use_fake_user = True
    return im

original_toon = s.toon_material
def toon_material(*args, **kwargs):
    mat = original_toon(*args, **kwargs)
    mat.use_fake_user = True
    return mat

# Flat editable flame motif, sampled from native cubic curves (not image generation).
FLAME_CURVES = [((0,1),(.35,.68),(.5,.48),(.30,.12)), ((.30,.12),(.5,.18),(.53,.40),(.52,.49)),
    ((.52,.49),(1,-.12),(.93,-.78),(.05,-1)), ((.05,-1),(-1,-.9),(-1,-.2),(-.5,.24)),
    ((-.5,.24),(-.22,.55),(-.03,.73),(0,1))]
CUT_CURVES = [((.02,-.20),(-.6,-.62),(-.44,-.96),(.02,-1)), ((.02,-1),(.43,-.88),(.46,-.60),(.02,-.20))]
CORE_CURVES = [((.02,-.61),(-.25,-.93),(-.07,-1),(.02,-1)), ((.02,-1),(.22,-.88),(.15,-.76),(.02,-.61))]
def curve_polygon(curves):
    return [tuple((1-t)**3*np.array(a)+3*(1-t)**2*t*np.array(b)+3*(1-t)*t*t*np.array(c)+t**3*np.array(d))
        for a,b,c,d in curves for t in np.linspace(0,1,20,endpoint=False)]

original_atlas = s.color_atlas
original_unwrap = s.unwrap
def unwrap(body):
    uv, old = original_unwrap(body)
    if 'Incendiary_Bomb_LaunchBay' in body['source_file']:
        # Reserve readable texel density for the two functional flame panels.
        # This changes only newly-created paint/line UVs, never any original UV.
        for item in uv.data:
            item.uv.y = .26 + item.uv.y * .73
        for p in body.data.polygons:
            c = body.matrix_world @ p.center
            if abs(p.normal.y) > .99 and abs(c.y) > 37.9:
                for loop in p.loop_indices:
                    pos = body.matrix_world @ body.data.vertices[body.data.loops[loop].vertex_index].co
                    uv.data[loop].uv = ((.02 if c.y < 0 else .52) + (pos.x + 11.3) / 24.8 * .46,
                        .025 + (pos.z + 4.8) / 9.8 * .185)
        for src, dst in zip(uv.data, body.data.uv_layers[s.LINE_UV].data):
            dst.uv = src.uv
    return uv, old

def color_atlas(body, uv, labels):
    paint, orm, coverage = original_atlas(body, uv, labels)
    if 'Incendiary_Bomb_LaunchBay' not in body['source_file']:
        return paint, orm, coverage
    mesh = body.data
    vertices = np.array([(body.matrix_world @ v.co)[:] for v in mesh.vertices], dtype=np.float32)
    polygons = [curve_polygon(c) for c in (FLAME_CURVES,CUT_CURVES,CORE_CURVES)]
    counts = collections.Counter()
    for tri in mesh.loop_triangles:
        p = mesh.polygons[tri.polygon_index]
        center = body.matrix_world @ p.center
        if abs(p.normal.y) < .99 or abs(center.y) < 37.9:
            continue
        coords = np.array([uv.data[i].uv[:] for i in tri.loops],dtype=np.float32)*s.SIZE
        a,b = coords[1]-coords[0],coords[2]-coords[0]
        det = a[0]*b[1]-a[1]*b[0]
        if abs(det)<1e-7:
            continue
        x0,y0=np.maximum(np.floor(coords.min(axis=0)).astype(int)-1,0)
        x1,y1=np.minimum(np.ceil(coords.max(axis=0)).astype(int)+1,s.SIZE)
        x=np.arange(x0,x1,dtype=np.float32)[None,:]+.5-coords[0,0]
        y=np.arange(y0,y1,dtype=np.float32)[:,None]+.5-coords[0,1]
        u=(x*b[1]-y*b[0])/det;v=(a[0]*y-a[1]*x)/det
        inside=(u>=0)&(v>=0)&(u+v<=1)
        pts=vertices[tri.vertices]
        pos=pts[0]+u[:,:,None]*(pts[1]-pts[0])+v[:,:,None]*(pts[2]-pts[0])
        dpdx=(b[1]*(pts[1]-pts[0])-a[1]*(pts[2]-pts[0]))/det
        dpdy=(-b[0]*(pts[1]-pts[0])+a[0]*(pts[2]-pts[0]))/det
        mask=np.zeros(inside.shape,dtype=np.float32)
        for dx,dy in ((-.25,-.25),(.25,-.25),(-.25,.25),(.25,.25)):
            q=pos+dx*dpdx+dy*dpdy
            px=(q[:,:,0]-1.8)/2.7;pz=(q[:,:,2]-.1)/3.4
            outer,cut,core=[in_polygon(px,pz,poly) for poly in polygons]
            mask+=((outer & ~cut)|core)*.25
        mask*=inside
        pixels=paint[y0:y1,x0:x1]
        pixels[:]=pixels*(1-mask[:,:,None])+np.array(s.linear(s.PALETTE['Pearl'])[:3])*mask[:,:,None]
        orm[y0:y1,x0:x1][mask>0]=(1,.64,.1)
        counts['front' if center.y<0 else 'rear']+=int(mask.sum())
    assert set(counts)=={'front','rear'} and min(counts.values())>100,counts
    s.dump(s.OUT/'Incendiary_Bomb_LaunchBay_flame_markings.json',{'method':'Flat BaseColor paint on both original exterior end panels',
        'no_new_geometry':True,'planes_y_m':[-38.018,38.018],'center_xz_m':[1.8,.1],'half_size_xz_m':[2.7,3.4],
        'pixels_painted':dict(counts),'outer_curves':FLAME_CURVES,'cut_curves':CUT_CURVES,'core_curves':CORE_CURVES})
    def path(curves):
        first=curves[0][0]
        return f'M {200+first[0]*160:.2f} {220-first[1]*190:.2f} '+ ' '.join('C '+' '.join(f'{200+x*160:.2f},{220-y*190:.2f}' for x,y in segment[1:]) for segment in curves)+' Z'
    svg='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 440"><title>Incendiary end-panel flame</title>'
    for curves,color in ((FLAME_CURVES,'DDE6E6'),(CUT_CURVES,'C66B38'),(CORE_CURVES,'DDE6E6')):
        svg+=f'<path fill="#{color}" d="{path(curves)}"/>'
    (s.OUT/'Incendiary_Bomb_LaunchBay_flame.svg').write_text(svg+'</svg>',encoding='utf-8')
    return paint,orm,coverage

s.load_original=load_original
s.material_labels=material_labels
s.outline=outline
s.line_faces=line_faces
s.save_image=save_image
s.toon_material=toon_material
s.color_atlas=color_atlas
s.unwrap=unwrap
s.pose=pose

def build(key):
    s.build(key)
    path=s.OUT/f'{key}_material_report.json'
    report=json.loads(path.read_text(encoding='utf-8'))
    report.update({'candidate_version':VERSION,'reference_version':APPROVAL['parts'][key]['reference_version'],
        'user_A':{'decision':'approved','record':'approval_A_20260917.json','user_message':APPROVAL['user_message']},
        'user_B':'no_separate_visual_pass; latest_user_directly_authorized_implementation_and_formal_import',
        'release_authorization':'../UE_Integration/release_authorization_20260917.json',
        'mechanical_type':'skeletal' if key in RIGGED else 'static'})
    if key not in RIGGED:
        report['internal_line_halfwidth_local_units']=report['internal_line_halfwidth_m']
        report['internal_line_halfwidth_m']*=.01
    s.dump(path,report)

if __name__=='__main__':
    args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else list(KEYS)
    for key in args:
        assert key in KEYS
        build(key)
    print('BATCH04_MATERIAL_PASS_COMPLETE',flush=True)
