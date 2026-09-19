"""Light-mech closed curved armor and source-mounted recessed vents, in cm."""
import bpy, bmesh, json, math, hashlib
from pathlib import Path
from mathutils import Vector
import numpy as np

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919')
OUT = ROOT / 'Production_v8_HeadRefine'
OUT.mkdir(exist_ok=True)
(OUT / 'Previews').mkdir(exist_ok=True)
scene = bpy.data.scenes['Review_Mech_Lightest']
bpy.context.window.scene = scene
collection = bpy.data.collections['WORK_Mech_Lightest']
collection.hide_viewport = collection.hide_render = False
bpy.context.view_layer.update()
cover = bpy.data.objects['WORK_Mech_Lightest__Sealed_Armor_Fairing']
shoulder = bpy.data.objects['WORK_Mech_Lightest__HalfShoulder_Box']
template = cover.data.materials[0]
ink = bpy.data.materials['INK3_Outer_NoShadow']

def digest(mesh):
    h = hashlib.sha256()
    for data, prop, n, kind in [(mesh.vertices, 'co', len(mesh.vertices)*3, np.float32), (mesh.loops, 'vertex_index', len(mesh.loops), np.int32)]:
        a = np.empty(n, kind); data.foreach_get(prop, a); h.update(a.tobytes())
    return h.hexdigest()

before = {o.name:digest(o.data) for o in collection.objects if o.type == 'MESH' and not o.get('ink_layer')}
def linear(h):
    c = [int(h[i:i+2],16)/255 for i in (0,2,4)]
    return [v/12.92 if v <= .04045 else ((v+.055)/1.055)**2.4 for v in c] + [1]

def material(name, h):
    mat = template.copy(); mat.name = name
    mat.node_tree.nodes['Paint_x_Three_Tones'].inputs[1].default_value = linear(h)
    next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED').inputs['Base Color'].default_value = linear(h)
    mat.diffuse_color = linear(h)
    return mat

gold = material('V8_Head_Original_Ochre', 'B48A3F')
teal = material('V8_Vent_Cast_Frame', '355153')
finmat = material('V8_Vent_Louver', '3A5558')
dark = material('V8_Vent_Inner_Wall', '202B30')
gasket = material('V8_Armor_Gasket', '15232A')

removed = []
for obj in list(collection.objects):
    if '__ArmorSeam_' in obj.name or '__DETAIL_Cockpit_Jet_Intake_' in obj.name or '__DETAIL_HalfShoulder_Box_Cooling_Slat_' in obj.name or obj.name in ('INK3_Outline_WORK_Mech_Lightest__Sealed_Armor_Fairing', 'INK3_Outline_WORK_Mech_Lightest__HalfShoulder_Box'):
        removed.append(obj.name)
        bpy.data.objects.remove(obj, do_unlink=True)

def mesh(name, vertices, faces):
    me = bpy.data.meshes.new(name)
    me.from_pydata(vertices, [], faces); me.update()
    bm = bmesh.new(); bm.from_mesh(me)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    bm.to_mesh(me); bm.free(); me.update()
    return me

def part(name, vertices, faces, mat, anchor=cover, bevel=0, smooth=False):
    obj = anchor.copy(); obj.data = mesh(name, vertices, faces)
    obj.name = 'WORK_Mech_Lightest__V8_' + name
    obj.modifiers.clear(); collection.objects.link(obj)
    obj.data.materials.append(mat)
    for face in obj.data.polygons: face.use_smooth = smooth and len(face.vertices) == 4
    obj['production_revision'] = 8
    if bevel:
        mod = obj.modifiers.new('Controlled_Edge_Radius', 'BEVEL')
        mod.width = bevel; mod.segments = 3; mod.limit_method = 'ANGLE'; mod.angle_limit = math.radians(35)
    return obj

def outline(obj, width_cm=.4):
    bpy.context.view_layer.update()
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    me = bpy.data.meshes.new_from_object(evaluated)
    positions = np.empty(len(me.vertices)*3, np.float32)
    normals = np.empty_like(positions)
    me.vertices.foreach_get('co', positions); me.vertices.foreach_get('normal', normals)
    me.vertices.foreach_set('co', positions + normals*width_cm); me.update()
    shell = obj.copy(); shell.data = me; shell.name = 'INK8_Outline_' + obj.name
    shell.modifiers.clear(); collection.objects.link(shell)
    me.materials.clear(); me.materials.append(ink)
    for face in me.polygons: face.material_index = 0
    shell.visible_shadow = False; shell.hide_select = True
    shell['ink_layer'] = 'outer'; shell['width_world_m'] = width_cm*.01
    return shell

# Crown heights and widths follow the sealed source envelope, with actual
# longitudinal curvature and a transverse crown instead of planar slabs.
stations = np.array([
    (-147, 20, 3, -8), (-134, 23.5, 15, -1),
    (-118, 26.5, 35, 12), (-98, 30.5, 59, 29),
    (-76, 32.5, 80, 47), (-54, 33, 95, 61),
    (-32, 30, 105, 74), (-9, 26, 105, 81),
    (17, 25, 97, 73), (52, 23, 83, 66)
], dtype=float)
ys = stations[:,0]
values = stations[:,1:]
deltas = np.diff(values, axis=0) / np.diff(ys)[:,None]
tangents = np.zeros_like(values)
tangents[0], tangents[-1] = deltas[0], deltas[-1]
for i in range(1, len(ys)-1):
    for j in range(3):
        a, b = deltas[i-1,j], deltas[i,j]
        if a*b > 0:
            tangents[i,j] = 2*a*b/(a+b)

def profile(y):
    i = min(max(int(np.searchsorted(ys, y)-1), 0), len(ys)-2)
    span = ys[i+1]-ys[i]; t = (y-ys[i])/span
    return ((2*t**3-3*t*t+1)*values[i] + (t**3-2*t*t+t)*span*tangents[i]
            + (-2*t**3+3*t*t)*values[i+1] + (t**3-t*t)*span*tangents[i+1])

def cross_section(y, inset=0):
    w, top, bottom = profile(y); w -= inset; top -= inset; bottom += inset
    crown = min(6.5, (top-bottom)*.34)
    points = [(-w*.88,y,bottom),(-w,y,bottom+2.0),(-w,y,top-crown-2.0)]
    for u in np.linspace(-.97,.97,13):
        points.append((w*u,y,top-crown*abs(u)**3.5))
    points += [(w,y,top-crown-2.0),(w,y,bottom+2.0),(w*.88,y,bottom)]
    return points

def armor_geometry(intervals, inset=0):
    verts, faces = [], []
    for low, high in intervals:
        rings = np.linspace(low, high, max(3, int(math.ceil((high-low)/3))+1))
        start = len(verts); n = len(cross_section(low))
        for y in rings: verts.extend(cross_section(float(y), inset))
        faces.append(tuple(start+i for i in reversed(range(n))))
        for j in range(len(rings)-1):
            for i in range(n):
                a=start+j*n+i; b=start+j*n+(i+1)%n
                faces.append((a,b,b+n,a+n))
        faces.append(tuple(start+(len(rings)-1)*n+i for i in range(n)))
    return verts, faces

cuts = [-147,-99,-58,-18,52]
intervals = [(a+(.28 if i else 0),b-(.28 if i<len(cuts)-2 else 0)) for i,(a,b) in enumerate(zip(cuts,cuts[1:]))]
vertices, faces = armor_geometry(intervals)
cover.data = mesh('V8_Curved_Closed_Armor_Panels', vertices, faces)
cover.data.materials.append(gold); cover.modifiers.clear()
for face in cover.data.polygons: face.use_smooth = len(face.vertices) == 4
bevel = cover.modifiers.new('Panel_Edge_Radius', 'BEVEL')
bevel.width = .65; bevel.segments = 3; bevel.limit_method = 'ANGLE'; bevel.angle_limit = math.radians(34)
cover['production_revision'] = 8
cover['source_basis'] = 'Closed source-mounted armor; longitudinal and transverse crown, four fitted panels with sealed backing'
vertices, faces = armor_geometry([(-147,52)], .95)
backing = part('Head_Continuous_Sealed_Backing', vertices, faces, gasket, smooth=True)
outline(cover, .42)

def rounded_polygon(corners, radius=2.2, segments=4):
    out = []
    for i, point in enumerate(corners):
        p = Vector(point); last = Vector(corners[(i-1)%len(corners)]); nxt = Vector(corners[(i+1)%len(corners)])
        trim = min(radius, (p-last).length*.25, (nxt-p).length*.25)
        a = p + (last-p).normalized()*trim
        b = p + (nxt-p).normalized()*trim
        for t in np.linspace(0,1,segments+1):
            q=(1-t)**2*a + 2*(1-t)*t*p + t*t*b
            out.append(tuple(q))
    return np.array(out)

def cartridge(name, contour, center, U, V, N, outer_material, depth_scale=1, anchor=cover, inner=(.70,.82)):
    center,U,V,N = map(Vector,(center,U,V,N))
    # The rear rim intersects its mounting surface. A solid inner floor seals
    # the original body; the mouth and sloping walls create a real recess.
    rings = [(1,1,-.4), (1,1,7.5), (.94,.965,10.0), (*inner,10.0), (inner[0]-.04,inner[1]-.04,2.1)]
    verts=[]; n=len(contour)
    for sx,sy,d in rings:
        verts += [tuple(center+U*(u*sx)+V*(v*sy)+N*(d*depth_scale)) for u,v in contour]
    faces=[]
    for r in range(4):
        for i in range(n): faces.append((r*n+i,r*n+(i+1)%n,(r+1)*n+(i+1)%n,(r+1)*n+i))
    # Inner wall ends in a solid floor, with the rear completely closed.
    faces += [tuple(4*n+i for i in range(n)), tuple(reversed(range(n)))]
    obj=part(name+'_Frame',verts,faces,outer_material,anchor=anchor,smooth=True)
    obj.data.materials.append(dark)
    for face in obj.data.polygons:
        if face.index>=3*n: face.material_index=1
        if 2*n<=face.index<3*n: face.use_smooth=False
    outline(obj,.24)
    return obj

front_contour=rounded_polygon([(-12,-37),(12,-37),(18,-29),(20,31),(14,37),(-14,37),(-20,31),(-18,-29)],2.5)
up=Vector((0,.48480962,.87461971)); normal=Vector((0,-.87461971,.48480962)); horizontal=Vector((1,0,0))
front_parts=[]
for sign in (-1,1):
    center=Vector((sign*53.5,-66.0,37.0))
    frame=cartridge('FrontVent_'+str(sign),front_contour,center,horizontal,up,normal,teal)
    front_parts.append(frame.name)
    for i,v in enumerate((-23,-14,-5,4,13,22)):
        # A thick sloped airfoil with buried side ends, not a square rung.
        section=[(v-3.3,2.5),(v+2.0,7.0),(v+3.1,6.4),(v-1.8,2.0)]
        width=13.7
        verts=[tuple(center+horizontal*x+up*t+normal*d) for x in (-width,width) for t,d in section]
        faces=[(3,2,1,0),(4,5,6,7)]+[(j,(j+1)%4,(j+1)%4+4,j+4) for j in range(4)]
        part('FrontVent_'+str(sign)+'_Louver_'+str(i),verts,faces,finmat,bevel=.45)

# Side cartridge fits inside the source shoulder's real side silhouette.
# In particular it ends before the front slope, where the old bars floated.
side_contour=rounded_polygon([(-43,-24),(21,-24),(46,-6),(46,18),(-43,18)],3)
side_center=Vector((47.9,18,27))
side_frame=cartridge('Shoulder_Recessed_Cooling',side_contour,side_center,(0,1,0),(0,0,1),(1,0,0),gold,depth_scale=.44,anchor=shoulder,inner=(.84,.76))
for i,y in enumerate((-10,0,10,20,30,40,50)):
    high=42.0
    cross=[(y-2.8,.8),(y-.9,3.1),(y+1.1,3.1),(y+2.8,1.0)]
    # Cut each lower end along the actual diagonal mouth and bury it in the
    # surrounding wall. Constant-height ends would float at the rear slope.
    verts=[(47.9+d,t,7.12+max(0,t-35.325)*.65018) for t,d in cross]
    verts.extend((47.9+d,t,high) for t,d in cross)
    faces=[(3,2,1,0),(4,5,6,7)]+[(j,(j+1)%4,(j+1)%4+4,j+4) for j in range(4)]
    part('Shoulder_Cooling_Louver_'+str(i),verts,faces,finmat,anchor=shoulder,bevel=.4)
shoulder.modifiers['Machined_Edge_Chamfer'].width=1.15
shoulder.modifiers['Machined_Edge_Chamfer'].segments=3
shoulder.data.materials.append(gold)
for index in (12,13,14,15): shoulder.data.polygons[index].material_index=len(shoulder.data.materials)-1
# The old UV-derived line mask broke up along this tiny source UV seam.
# Keep its functional emission, with the rebuilt frame and contour supplying
# the side-panel lines rather than the obsolete source paint-boundary mask.
shoulder_mat=shoulder.data.materials[0].copy(); shoulder_mat.name='V8_Shoulder_SourcePaint_CleanEdges'
nodes=shoulder_mat.node_tree.nodes; links=shoulder_mat.node_tree.links
pre_ink=nodes['Fine_Ink_Overlay'].inputs[1].links[0].from_socket
links.new(pre_ink,nodes['Cel_Surface'].inputs['Color'])
shoulder.data.materials[0]=shoulder_mat
outline(shoulder,.55)

def captive_fastener(name, center, U, V, N, anchor):
    center,U,V,N=map(Vector,(center,U,V,N)); vertices=[]; count=12
    for radius,depth in ((.85,0),(.55,.18),(.48,.30)):
        for i in range(count):
            a=i*math.tau/count
            vertices.append(tuple(center+U*(math.cos(a)*radius)+V*(math.sin(a)*radius)+N*depth))
    faces=[]
    for r in range(2):
        for i in range(count): faces.append((r*count+i,r*count+(i+1)%count,(r+1)*count+(i+1)%count,(r+1)*count+i))
    faces.extend([tuple(2*count+i for i in range(count)),tuple(reversed(range(count)))])
    obj=part(name,vertices,faces,dark,anchor=anchor)
    obj.data.materials.append(finmat)
    for face in obj.data.polygons:
        if count<=face.index<2*count+1: face.material_index=1

for i,(u,v) in enumerate(((-38,14),(-38,-20),(40,13),(39,-6))):
    captive_fastener('Shoulder_Vent_Captive_Fastener_'+str(i),(52.32,18+u,27+v),(0,1,0),(0,0,1),(1,0,0),shoulder)

bpy.context.view_layer.update()
baseline=json.loads((ROOT/'Production_v3_InkCel/production_report.json').read_text())
def render(view,direction,center=None,scale=None):
    cam=scene.camera
    bounds=baseline['assets']['Mech_Lightest']['bounds']
    lo,hi=Vector(bounds['min']),Vector(bounds['max'])
    center=Vector(center) if center else (lo+hi)/2
    cam.location=center+Vector(direction).normalized()*12
    rot=(center-cam.location).to_track_quat('-Z','Y'); cam.rotation_euler=rot.to_euler()
    if scale is None:
        points=[Vector((x,y,z)) for x in (lo.x,hi.x) for y in (lo.y,hi.y) for z in (lo.z,hi.z)]
        right,up=rot@Vector((1,0,0)),rot@Vector((0,1,0))
        w=max(p.dot(right) for p in points)-min(p.dot(right) for p in points)
        h=max(p.dot(up) for p in points)-min(p.dot(up) for p in points)
        scale=max(w,h*scene.render.resolution_x/scene.render.resolution_y)*1.12
    cam.data.ortho_scale=scale
    scene.render.filepath=str(OUT/'Previews'/('Mech_Lightest_'+view+'.png'))
    bpy.ops.render.render(write_still=True)
    print('HEAD_V8_RENDERED '+view,flush=True)

for view,d in [('Hero',(1.5,-2,1.05)),('Front',(0,-1,0)),('Side',(1,0,0)),('Rear',(0,1,0))]: render(view,d)
render('Head_Close',(1.15,-2,1.0),(0,-.40,3.00),2.35)
render('Head_Profile',(1,0,0),(0,-.38,3.03),2.7)
render('Shoulder_Close',(2,-.60,.65),(1.10,.22,3.22),1.80)
render('Vent_Front',(0,-1,.32),(0,-.60,3.12),1.95)

# Leave an overall hero view in the saved review scene.
cam=scene.camera; center=Vector(baseline['assets']['Mech_Lightest']['bounds']['min'])+Vector(baseline['assets']['Mech_Lightest']['bounds']['max']); center*=.5
cam.location=center+Vector((1.5,-2,1.05)).normalized()*12; rotation=(center-cam.location).to_track_quat('-Z','Y');cam.rotation_euler=rotation.to_euler()
lo=Vector(baseline['assets']['Mech_Lightest']['bounds']['min']);hi=Vector(baseline['assets']['Mech_Lightest']['bounds']['max'])
points=[Vector((x,y,z)) for x in (lo.x,hi.x) for y in (lo.y,hi.y) for z in (lo.z,hi.z)]
right,up=rotation@Vector((1,0,0)),rotation@Vector((0,1,0))
w=max(p.dot(right) for p in points)-min(p.dot(right) for p in points);h=max(p.dot(up) for p in points)-min(p.dot(up) for p in points)
cam.data.ortho_scale=max(w,h*scene.render.resolution_x/scene.render.resolution_y)*1.12
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.shading.type='MATERIAL';area.spaces.active.region_3d.view_perspective='CAMERA';area.spaces.active.overlay.show_overlays=False
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Mechs_HeadRefined_v8.blend'),check_existing=False)

changed=[name for name,old in before.items() if name not in removed and digest(bpy.data.objects[name].data)!=old]
assert changed==[cover.name],changed
cover.data.calc_loop_triangles()
report={'success':True,'source':str(ROOT/'Production_v7_Black/Mechs_ComponentColors_v7.blend'),'revision':8,
        'head_color':'B48A3F','leg_color_retained':'587F9B','head_stations_cm':stations.tolist(),
        'panel_count':4,'head_vertices':len(cover.data.vertices),'head_base_triangles':len(cover.data.loop_triangles),
        'changed_existing_base_meshes':changed,'removed_replaced_details':removed,'untouched_base_geometry_hashes':{n:h for n,h in before.items() if n not in changed and n not in removed},
        'front_vent_frames':front_parts,'front_vent_louvers_per_side':6,'front_recess_depth_cm':7.9,
        'side_vent_frame':side_frame.name,'side_vent_louvers':7,'side_recess_depth_cm':3.476,
        'shoulder_side_complete_ochre_faces':[12,13,14,15],
        'source_sockets_and_rigs_preserved':True,'spider_untouched':True,'images':sorted(p.name for p in (OUT/'Previews').glob('*.png'))}
(OUT/'refinement_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('HEAD_V8_READY',flush=True)
