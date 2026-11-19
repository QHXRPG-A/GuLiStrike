"""Editable hard-surface reconstruction of approved Outpost reference A_v2.

Run in the connected Blender. Only this asset's tagged scene is rebuilt.
The user's pre-existing scenes and objects are preserved.
"""
import bpy
import bmesh
import json
import math
from pathlib import Path
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1/ArtSource/Buildings/OutpostSymmetry_20261029')
OUT = ROOT / 'Production_B_v1'
OWNER = 'GuLi.OutpostSymmetric.B_v1'
SCENE_NAME = 'OUTPOST_B_v1'
TARGET = (17.8776537773163, 20.2333757971781, 29.29927110671997)
SCENE = None
PARTS = None
RIG = None
ROOT_OBJECT = None
MATS = []
PALETTE = {'cream':'FEE4D9', 'blue':'274E61', 'sky':'6AA4BE',
           'dark':'1A182F', 'teal':'0D9099'}


def linear(hex_rgb):
    values = [int(hex_rgb[i:i+2], 16)/255 for i in (0, 2, 4)]
    return tuple(v/12.92 if v <= .04045 else ((v+.055)/1.055)**2.4 for v in values)


def toon_material(name, hex_rgb, emission=0):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat['asset_owner'] = OWNER
    mat['base_srgb'] = '#'+hex_rgb
    mat.diffuse_color = (*linear(hex_rgb), 1)
    mat.use_nodes = True
    nodes, links = mat.node_tree.nodes, mat.node_tree.links
    nodes.clear()
    output = nodes.new('ShaderNodeOutputMaterial')
    output.location = (580, 0)
    surface = nodes.new('ShaderNodeEmission')
    surface.location = (350, 0)
    links.new(surface.outputs[0], output.inputs['Surface'])
    if emission:
        surface.inputs['Color'].default_value = (*linear(hex_rgb), 1)
        surface.inputs['Strength'].default_value = emission
        mat['render_contract'] = 'functional emission'
        return mat
    diffuse = nodes.new('ShaderNodeBsdfDiffuse')
    diffuse.location = (-620, 0)
    diffuse.inputs['Color'].default_value = (1,1,1,1)
    rgb = nodes.new('ShaderNodeShaderToRGB')
    rgb.location = (-430, 0)
    links.new(diffuse.outputs[0], rgb.inputs[0])
    ramp = nodes.new('ShaderNodeValToRGB')
    ramp.location = (-190, 0)
    ramp.name = 'Three_Step_Shading'
    ramp.color_ramp.interpolation = 'CONSTANT'
    elements = ramp.color_ramp.elements
    elements.remove(elements[1])
    for index, (threshold, value) in enumerate(((0,.46),(.36,.73),(.76,1.0))):
        elem = elements[0] if index == 0 else elements.new(threshold)
        elem.position = threshold
        elem.color = (value,value,value,1)
    links.new(rgb.outputs['Color'], ramp.inputs[0])
    multiply = nodes.new('ShaderNodeMixRGB')
    multiply.location = (100, 0)
    multiply.blend_type = 'MULTIPLY'
    multiply.inputs[0].default_value = 1
    multiply.inputs[1].default_value = (*linear(hex_rgb), 1)
    links.new(ramp.outputs[0], multiply.inputs[2])
    links.new(multiply.outputs[0], surface.inputs['Color'])
    mat['render_contract'] = 'EEVEE diffuse -> shader-to-RGB -> 3 constant steps -> base tint'
    return mat


def property_driver(socket, property_name, expression):
    driver=socket.driver_add('default_value').driver
    variable=driver.variables.new();variable.name='mode'
    variable.type='SINGLE_PROP'
    variable.targets[0].id=ROOT_OBJECT
    variable.targets[0].data_path='["'+property_name+'"]'
    driver.expression=expression


def core_ownership_material(name, strength_scale):
    """Independent white/team emission contract; armor never uses this material."""
    group=bpy.data.node_groups.get('OP_Core_White_Team_Switch')
    if not group:
        group=bpy.data.node_groups.new('OP_Core_White_Team_Switch','ShaderNodeTree')
        group['asset_owner']=OWNER
        for socket_name,kind in (('Neutral White','NodeSocketColor'),
                                 ('Team Color','NodeSocketColor'),
                                 ('Use Team Color','NodeSocketFloat')):
            interface=group.interface.new_socket(name=socket_name,in_out='INPUT',socket_type=kind)
            if kind=='NodeSocketFloat':
                interface.min_value=0;interface.max_value=1
            else:interface.default_value=(1,1,1,1)
        group.interface.new_socket(name='Color',in_out='OUTPUT',socket_type='NodeSocketColor')
        inputs=group.nodes.new('NodeGroupInput');inputs.location=(-200,0)
        mix=group.nodes.new('ShaderNodeMixRGB');mix.blend_type='MIX';mix.location=(0,0)
        output=group.nodes.new('NodeGroupOutput');output.location=(200,0)
        group.links.new(inputs.outputs['Use Team Color'],mix.inputs[0])
        group.links.new(inputs.outputs['Neutral White'],mix.inputs[1])
        group.links.new(inputs.outputs['Team Color'],mix.inputs[2])
        group.links.new(mix.outputs[0],output.inputs['Color'])
    mat=bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat['asset_owner']=OWNER
    mat['render_contract']='Neutral White / Team Color / Use Team Color / Emission Strength'
    mat['neutral_white_required']=True
    mat.diffuse_color=(.8,.8,.8,1)
    mat.use_nodes=True
    nodes,links=mat.node_tree.nodes,mat.node_tree.links
    nodes.clear()
    palette=nodes.new('ShaderNodeMixRGB');palette.name='Preview_Ally_Enemy_Palette'
    palette.location=(-420,-150);palette.blend_type='MIX'
    palette.inputs[1].default_value=(*linear(PALETTE['sky']),1)
    palette.inputs[2].default_value=(*linear('A34053'),1)
    property_driver(palette.inputs[0],'Core_Color_Mode','1.0 if mode > 1.5 else 0.0')
    switch=nodes.new('ShaderNodeGroup');switch.node_tree=group
    switch.name='GuLi_CoreLight_Switch';switch.label='White / Team Color'
    switch.location=(-120,0)
    switch.inputs['Neutral White'].default_value=(1,1,1,1)
    property_driver(switch.inputs['Use Team Color'],'Core_Color_Mode','1.0 if mode > 0.5 else 0.0')
    links.new(palette.outputs[0],switch.inputs['Team Color'])
    emission=nodes.new('ShaderNodeEmission');emission.location=(180,0)
    property_driver(emission.inputs['Strength'],'Core_Emission_Strength','mode * '+str(strength_scale))
    links.new(switch.outputs['Color'],emission.inputs['Color'])
    output=nodes.new('ShaderNodeOutputMaterial');output.location=(400,0)
    links.new(emission.outputs[0],output.inputs['Surface'])
    return mat


def make_mesh(name, vertices, faces, material_ids=None, bevel=0, mirror=False):
    mesh = bpy.data.meshes.new(name+'_Mesh')
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    PARTS.objects.link(obj)
    obj.parent = ROOT_OBJECT
    obj['asset_owner'] = OWNER
    for mat in MATS:
        mesh.materials.append(mat)
    if material_ids is not None:
        for p, index in zip(mesh.polygons, material_ids):
            p.material_index = index
    if mirror:
        modifier = obj.modifiers.new('Symmetry_Upper_Lower', 'MIRROR')
        modifier.use_axis = (False, False, True)
        modifier.use_clip = True
        modifier.use_mirror_merge = True
    if bevel:
        modifier = obj.modifiers.new('Controlled_One_Segment_Bevel', 'BEVEL')
        modifier.width = bevel
        modifier.segments = 1
        modifier.limit_method = 'ANGLE'
        modifier.angle_limit = math.radians(22)
        modifier.affect = 'EDGES'
        modifier.harden_normals = True
        modifier.loop_slide = True
    return obj


def octagon(radius):
    k = math.sqrt(2)-1
    return [(radius*x,radius*y) for x,y in
            ((1,k),(k,1),(-k,1),(-1,k),(-1,-k),(-k,-1),(k,-1),(1,-k))]


def ring(name, profile, ids, inner=0, mirror=False, segments=8, offset=math.pi/8):
    vertices=[]
    for radius,z in profile:
        if segments == 8:
            points = octagon(radius)
        else:
            points=[(radius*math.cos(offset+2*math.pi*i/segments),
                     radius*math.sin(offset+2*math.pi*i/segments)) for i in range(segments)]
        vertices.extend((x,y,z) for x,y in points)
    faces=[]; mats=[]
    for j in range(len(profile)-1):
        for i in range(segments):
            faces.append((j*segments+i,j*segments+(i+1)%segments,
                          (j+1)*segments+(i+1)%segments,(j+1)*segments+i))
            mats.append(ids[j] if isinstance(ids,list) else ids)
    if inner:
        for z in (profile[0][1],profile[-1][1]):
            vertices.extend((x,y,z) for x,y in octagon(inner))
        start=(len(profile))*segments
        last=(len(profile)-1)*segments
        for i in range(segments):
            n=(i+1)%segments
            faces += [(i,start+i,start+n,n),
                      (last+i,last+n,start+segments+n,start+segments+i),
                      (start+i,start+segments+i,start+segments+n,start+n)]
            mats += [1,1,3]
    else:
        faces += [tuple(reversed(range(segments))),
                  tuple((len(profile)-1)*segments+i for i in range(segments))]
        mats += [3,3]
    return make_mesh(name,vertices,faces,mats,mirror=mirror)


def radial_prism(name, outline, width, angle, cap_mat=0, edge_mat=1, bevel=.07, mirror=False):
    c,s=math.cos(angle),math.sin(angle)
    vertices=[]
    for tangent in (-width/2,width/2):
        for r,z in outline:
            vertices.append((r*c-tangent*s,r*s+tangent*c,z))
    n=len(outline)
    faces=[tuple(reversed(range(n))),tuple(n+i for i in range(n))]
    faces += [(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    ids=[cap_mat,cap_mat]+[edge_mat]*n
    return make_mesh(name,vertices,faces,ids,bevel=bevel,mirror=mirror)


def radial_panel(name, angle, radius, width, z0, z1, thickness=.12, material=1, bevel=.035, mirror=False):
    return radial_prism(name,[(radius,z0),(radius+thickness,z0),
                             (radius+thickness,z1),(radius,z1)],
                        width,angle,material,material,bevel,mirror)


def add_arm_panel_seam(obj, angle):
    """One deliberate panel joint, without adding another armor layer."""
    bm=bmesh.new();bm.from_mesh(obj.data)
    normal=Vector((.10*math.cos(angle),.10*math.sin(angle),1))
    point=Vector((3.3*math.cos(angle),3.3*math.sin(angle),9.0))
    bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),
                          dist=.00001,plane_co=point,plane_no=normal,
                          clear_inner=False,clear_outer=False)
    bm.to_mesh(obj.data);bm.free();obj.data.update()
    for selected in bpy.context.selected_objects:selected.select_set(False)
    bpy.context.view_layer.objects.active=obj;obj.select_set(True)
    bpy.context.tool_settings.mesh_select_mode=(False,True,False)
    bpy.ops.object.mode_set(mode='EDIT')
    edit=bmesh.from_edit_mesh(obj.data)
    for face in edit.faces:face.select_set(False)
    for edge in edit.edges:edge.select_set(False)
    for vertex in edit.verts:vertex.select_set(False)
    for edge in edit.edges:
        if all(abs((v.co-point).dot(normal))<.0001 for v in edge.verts):edge.select_set(True)
    bmesh.update_edit_mesh(obj.data)
    bpy.ops.mesh.mark_freestyle_edge(clear=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    obj.select_set(False)


def add_camera(name, location, target, ortho, resolution=(1600,1800)):
    data=bpy.data.cameras.new(name+'_Data')
    obj=bpy.data.objects.new(name,data)
    RIG.objects.link(obj)
    obj['asset_owner']=OWNER
    obj.location=location
    obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()
    data.type='ORTHO'
    data.ortho_scale=ortho
    data.lens=55
    obj['resolution']=resolution
    return obj


def material_plain(name, color, roughness=1):
    mat=bpy.data.materials.new(name)
    mat['asset_owner']=OWNER
    mat.use_nodes=True
    node=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    node.inputs['Base Color'].default_value=(*linear(color),1)
    node.inputs['Roughness'].default_value=roughness
    return mat


def studio_ground_material():
    mat=bpy.data.materials.new('OP_Studio_Ground_Material')
    mat['asset_owner']=OWNER
    mat.use_nodes=True
    nodes,links=mat.node_tree.nodes,mat.node_tree.links
    nodes.clear()
    diffuse=nodes.new('ShaderNodeBsdfDiffuse')
    diffuse.inputs['Color'].default_value=(1,1,1,1)
    rgb=nodes.new('ShaderNodeShaderToRGB')
    links.new(diffuse.outputs[0],rgb.inputs[0])
    value=nodes.new('ShaderNodeMath');value.operation='MULTIPLY_ADD'
    value.inputs[1].default_value=.25
    value.inputs[2].default_value=.75
    value.use_clamp=True
    links.new(rgb.outputs[0],value.inputs[0])
    tint=nodes.new('ShaderNodeMixRGB');tint.blend_type='MULTIPLY'
    tint.inputs[0].default_value=1
    tint.inputs[1].default_value=(*linear('D3DFED'),1)
    links.new(value.outputs[0],tint.inputs[2])
    emission=nodes.new('ShaderNodeEmission')
    links.new(tint.outputs[0],emission.inputs['Color'])
    output=nodes.new('ShaderNodeOutputMaterial')
    links.new(emission.outputs[0],output.inputs['Surface'])
    return mat


def prepare_scene():
    global SCENE, PARTS, RIG, ROOT_OBJECT, MATS
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT/'Previews').mkdir(exist_ok=True)
    (OUT/'Evidence').mkdir(exist_ok=True)
    existing=bpy.data.scenes.get(SCENE_NAME)
    if existing:
        if existing.get('asset_owner')!=OWNER:
            raise RuntimeError('Scene name already belongs to other work')
        for obj in list(existing.objects):
            if obj.get('asset_owner')!=OWNER:
                raise RuntimeError('Foreign object in owned scene; stop rather than remove it')
        for obj in list(existing.objects):
            bpy.data.objects.remove(obj,do_unlink=True)
        SCENE=existing
    else:
        SCENE=bpy.data.scenes.new(SCENE_NAME)
        SCENE['asset_owner']=OWNER
    bpy.context.window.scene=SCENE
    SCENE.unit_settings.system='METRIC'
    SCENE.unit_settings.scale_length=1
    for name in ('OP_Editable_Parts','OP_Review_Rig'):
        col=bpy.data.collections.get(name)
        if col:
            if col.get('asset_owner')!=OWNER:raise RuntimeError('Foreign collection name')
        else:
            col=bpy.data.collections.new(name)
            col['asset_owner']=OWNER
        if col.name not in SCENE.collection.children:
            SCENE.collection.children.link(col)
    PARTS=bpy.data.collections['OP_Editable_Parts']
    RIG=bpy.data.collections['OP_Review_Rig']
    ROOT_OBJECT=bpy.data.objects.new('OP_Root_Midplane',None)
    ROOT_OBJECT['asset_owner']=OWNER
    ROOT_OBJECT['reference']='A_v2 / 88efc71716b987a4ca5e9f2f436219a228b4609da7a9359155a91bc5cf4766b7'
    ROOT_OBJECT['dimensions_target_m']=list(TARGET)
    ROOT_OBJECT['hover_clearance_m']=5.0
    ROOT_OBJECT['float_amplitude_m']=.5
    ROOT_OBJECT['float_period_s']=8.0
    ROOT_OBJECT['Core_Color_Mode']=0
    ROOT_OBJECT.id_properties_ui('Core_Color_Mode').update(
        min=0,max=2,soft_min=0,soft_max=2,
        description='中央光柱：0 中立白 / 1 己方蓝 / 2 敌方红')
    ROOT_OBJECT['Core_Emission_Strength']=1.65
    ROOT_OBJECT.id_properties_ui('Core_Emission_Strength').update(
        min=0,max=10,soft_min=0,soft_max=4,
        description='中央光柱独立发光强度，不改变外壳或其他灯带')
    ROOT_OBJECT.empty_display_size=2
    PARTS.objects.link(ROOT_OBJECT)
    MATS=[toon_material('OP_Armor_Cream',PALETTE['cream']),
          toon_material('OP_Armor_BlueGray',PALETTE['blue']),
          toon_material('OP_Trim_SkyBlue',PALETTE['sky']),
          toon_material('OP_Mechanism_Dark',PALETTE['dark']),
          toon_material('OP_Energy_Teal',PALETTE['teal'],1.7),
          toon_material('OP_Energy_Cyan',PALETTE['sky'],3.5),
          core_ownership_material('OP_Core_Ownership_Surface',.36),
          core_ownership_material('OP_Core_Ownership_Highlight',1.10)]


def build_geometry():
    ring('OP_Waist_Containment_Armor',
         [(8.55,-3.00),(10,-1.85),(10,-.40),(9.94,-.34),
          (9.94,.34),(10,.40),(10,1.85),(8.55,3.00)],
         [0,0,1,3,1,0,0],inner=2.3)
    ring('OP_Waist_Cyan_Continuous_Band',[(10.025,-.20),(10.025,.20)],4,inner=9.9)
    ring('OP_Waist_Light_Hot_Core',[(10.032,-.08),(10.032,.08)],5,inner=9.96)
    ring('OP_Waist_Recess_Frame',[(9.99,-.32),(9.99,-.19)],3,inner=9.9,mirror=True)

    rib=[(1.45,12.15),(2.50,12.35),(4.18,9.85),(7.75,4.55),
         (9.35,3.65),(9.50,1.65),(7.80,1.65),(7.30,3.45),
         (5.78,4.15),(2.75,9.55),(1.25,11.42)]
    crown=[(.80,14.05),(1.36,14.13),(2.58,12.78),
           (2.38,11.58),(1.53,11.42),(.92,12.50)]
    for i in range(4):
        angle=math.pi/4+i*math.pi/2
        arm=radial_prism('OP_Main_Armor_Rib_%02d'%i,rib,1.52,angle,
                         cap_mat=0,edge_mat=1,bevel=.065,mirror=True)
        add_arm_panel_seam(arm,angle)
        radial_prism('OP_Crown_Armor_%02d'%i,crown,1.02,angle,
                     cap_mat=0,edge_mat=1,bevel=.04,mirror=True)
        # Full-height guard spanning the belt, with mirrored attachment shoulders.
        strap=[(8.45,-4.30),(9.80,-3.10),(10.20,-1.80),(10.20,1.80),
               (9.80,3.10),(8.45,4.30),(8.12,4.30),(9.37,3.03),
               (9.69,1.70),(9.69,-1.70),(9.37,-3.03),(8.12,-4.30)]
        guard=radial_prism('OP_Belt_Long_Guard_%02d'%i,strap,1.75,angle,
                          cap_mat=1,edge_mat=1,bevel=.045)
        guard.modifiers['Controlled_One_Segment_Bevel'].material=2
        def guard_r(z):
            return 10.20-(z-1.80)*(.40/1.30) if z<3.10 else 9.80-(z-3.10)*(1.35/1.20)
        z0,z1=3.04,4.05
        recess=[(guard_r(z0)+.012,z0),(guard_r(z0)+.075,z0),
                (guard_r(z1)+.075,z1),(guard_r(z1)+.012,z1)]
        radial_prism('OP_Guard_Light_Recess_%02d'%i,recess,.56,angle,
                     cap_mat=3,edge_mat=3,bevel=.012,mirror=True)
        z0,z1=3.20,3.90
        slot=[(guard_r(z0)+.080,z0),(guard_r(z0)+.095,z0),
              (guard_r(z1)+.095,z1),(guard_r(z1)+.080,z1)]
        radial_prism('OP_Guard_Cyan_Slot_%02d'%i,slot,.20,angle,
                     cap_mat=5,edge_mat=5,bevel=0,mirror=True)
        def rib_inner_r(z):return 5.78-(z-4.15)*(3.03/5.40)
        z0,z1=6.00,7.15
        inner_light=[(rib_inner_r(z0)-.04,z0),(rib_inner_r(z0)-.012,z0),
                     (rib_inner_r(z1)-.012,z1),(rib_inner_r(z1)-.04,z1)]
        radial_prism('OP_Rib_Inner_Energy_%02d'%i,inner_light,.15,angle,
                     cap_mat=4,edge_mat=4,bevel=0,mirror=True)

    for i in range(4):
        angle=i*math.pi/2
        badge_profile=[(9.15,-2.70),(10.25,-1.65),(10.25,1.65),(9.15,2.70),
                       (8.91,2.70),(9.99,1.55),(9.99,-1.55),(8.91,-2.70)]
        badge=radial_prism('OP_Waist_Armor_Badge_%02d'%i,badge_profile,2.58,angle,
                           cap_mat=1,edge_mat=1,bevel=.045)
        badge.modifiers['Controlled_One_Segment_Bevel'].material=2
        radial_panel('OP_Waist_Badge_Inset_%02d'%i,angle,10.26,1.48,-1.10,1.10,
                     material=1,thickness=.018,bevel=.003)

    # Eight-sided core, matched collars, and end socket: intentionally few parts.
    ring('OP_Core_Dark_Spine',[(.95,2.0),(.95,11.55)],3,mirror=True)
    core=ring('OP_Core_Energy_Surface',[(1.01,2.25),(1.01,10.38)],6,mirror=True)
    core['emission_control']='OP_Root_Midplane.Core_Color_Mode'
    core['neutral_white_required']=True
    for i in range(4):
        radial_panel('OP_Core_Bright_Window_%02d'%i,i*math.pi/2,1.02,.37,2.45,10.20,
                     material=7,thickness=.022,bevel=0,mirror=True)
        radial_panel('OP_Core_Window_Frame_%02d'%i,math.pi/4+i*math.pi/2,
                     1.045,.18,2.18,10.55,material=1,thickness=.09,
                     bevel=.025,mirror=True)
    ring('OP_Core_Main_Collar',[(1.60,3.91),(1.94,4.10),
                              (1.94,4.70),(1.60,4.88)],
         [1,1,2],inner=.98,mirror=True)
    ring('OP_Core_Crown_Socket',[(1.26,10.20),(1.78,10.48),
                               (1.78,11.40),(1.38,11.64)],
         [3,1,1],inner=.95,mirror=True)
    ring('OP_Crown_Center_Spear',[(.74,11.55),(.88,12.18),
                                (.68,13.60),(.28,14.35),(.055,TARGET[2]/2)],
         [1,1,1,3],mirror=True)
    for i in range(4):
        angle=i*math.pi/2
        light_profile=[(.894,12.34),(.887,12.34),(.717,13.45),(.737,13.45)]
        radial_prism('OP_Crown_Cyan_Spear_Slit_%02d'%i,light_profile,.20,angle,
                     cap_mat=5,edge_mat=5,bevel=0,mirror=True)


def fit_dimensions():
    bpy.context.view_layer.update()
    dg=bpy.context.evaluated_depsgraph_get()
    coords=[o.matrix_world@v.co for o in PARTS.objects if o.type=='MESH'
            for v in o.evaluated_get(dg).data.vertices]
    minima=[min(v[i] for v in coords) for i in range(3)]
    maxima=[max(v[i] for v in coords) for i in range(3)]
    factor=Vector([TARGET[i]/(maxima[i]-minima[i]) for i in range(3)])
    seen=set()
    for obj in PARTS.objects:
        if obj.type!='MESH' or obj.data.name in seen:continue
        seen.add(obj.data.name)
        for vert in obj.data.vertices:
            vert.co.x*=factor.x;vert.co.y*=factor.y;vert.co.z*=factor.z
        obj.data.update()
    ROOT_OBJECT['construction_fit_xyz']=list(factor)
    bpy.context.view_layer.update()


def unwrap_parts():
    for obj in bpy.context.selected_objects:obj.select_set(False)
    meshes=[obj for obj in PARTS.objects if obj.type=='MESH']
    for obj in meshes:obj.select_set(True)
    bpy.context.view_layer.objects.active=meshes[0]
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66),island_margin=.012,
                             correct_aspect=True,scale_to_bounds=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    for obj in meshes:
        if obj.data.uv_layers:obj.data.uv_layers[0].name='UVMap'
        obj.select_set(False)


def setup_review():
    SCENE.render.engine='BLENDER_EEVEE'
    SCENE.render.resolution_x=1500
    SCENE.render.resolution_y=1700
    SCENE.render.resolution_percentage=100
    SCENE.render.image_settings.file_format='PNG'
    SCENE.render.image_settings.color_mode='RGBA'
    SCENE.render.film_transparent=False
    SCENE.view_settings.view_transform='Standard'
    SCENE.view_settings.look='None'
    SCENE.view_settings.exposure=0
    SCENE.view_settings.gamma=1
    SCENE.render.use_freestyle=True
    SCENE.render.line_thickness=1
    fs=SCENE.view_layers[0].freestyle_settings
    fs.mode='EDITOR'
    fs.crease_angle=math.radians(130)
    fs.use_culling=True
    fs.use_material_boundaries=True
    while len(fs.linesets):fs.linesets.remove(fs.linesets[0])
    for label,outline in (('Outline',True),('Structure',False)):
        line=fs.linesets.new('OP_'+label)
        for key in ('select_silhouette','select_border','select_crease','select_edge_mark',
                    'select_external_contour','select_material_boundary','select_contour',
                    'select_suggestive_contour','select_ridge_valley'):
            if hasattr(line,key):setattr(line,key,False)
        line.select_silhouette=outline
        line.select_border=outline
        line.select_crease=not outline
        line.select_edge_mark=not outline
        line.select_material_boundary=not outline
        style=bpy.data.linestyles.new('OP_'+label+'_Style')
        line.linestyle=style
        style.color=linear(PALETTE['dark'])
        style.thickness=1.45 if outline else .70
    world=bpy.data.worlds.new('OP_Studio_World')
    world['asset_owner']=OWNER
    SCENE.world=world
    world.use_nodes=True
    background=next(n for n in world.node_tree.nodes if n.type=='BACKGROUND')
    background.inputs['Color'].default_value=(*linear('BFCDDB'),1)
    background.inputs['Strength'].default_value=1
    for name,kind,location,energy,size in (
        ('OP_Key','AREA',(10,-18,35),11000,22),
        ('OP_Fill','AREA',(-25,-10,8),5600,24),
        ('OP_Rim','AREA',(10,18,20),6400,18)):
        light=bpy.data.lights.new(name+'_Data',kind)
        light.energy=energy;light.shape='DISK';light.size=size
        obj=bpy.data.objects.new(name,light)
        obj['asset_owner']=OWNER;RIG.objects.link(obj);obj.location=location
        obj.rotation_euler=(-obj.location).to_track_quat('-Z','Y').to_euler()
    # Ground is a review prop, not part of the asset or its triangle statistics.
    mesh=bpy.data.meshes.new('OP_Studio_Ground_Mesh')
    floor_z=-TARGET[2]/2-5
    mesh.from_pydata([(-10000,-10000,floor_z),(10000,-10000,floor_z),
                     (10000,10000,floor_z),(-10000,10000,floor_z)],[],[(0,1,2,3)])
    floor=bpy.data.objects.new('OP_Studio_Ground',mesh)
    floor['asset_owner']=OWNER;RIG.objects.link(floor)
    floor.data.materials.append(studio_ground_material())
    floor.hide_select=True
    hero=add_camera('OP_Camera_Hero',(70,-10,3),(0,0,-.5),38)
    add_camera('OP_Camera_Front',(70,0,0),(0,0,0),34)
    add_camera('OP_Camera_Side',(0,-70,0),(0,0,0),34)
    add_camera('OP_Camera_Back',(-70,0,0),(0,0,0),34)
    add_camera('OP_Camera_Underside',(60,-15,-12),(0,0,-3),38)
    SCENE.camera=hero
    image=bpy.data.images.load(str(ROOT/'References_A_v2/Outpost_Symmetric_A_v2.png'),check_existing=True)
    image.pack()
    image.use_fake_user=True
    SCENE['approved_reference_image']=image.name
    SCENE['reference_approval_A']='blender已开，一比一制作'
    SCENE['user_review_B']='pending'
    SCENE['saved_model_path']=str(OUT/'Outpost_Symmetric_B_v1.blend')


def summarize():
    bpy.context.view_layer.update()
    dg=bpy.context.evaluated_depsgraph_get()
    objects=[]; coords=[]; tris=0; base=0
    for obj in PARTS.objects:
        if obj.type!='MESH':continue
        obj.data.calc_loop_triangles()
        evaluated=obj.evaluated_get(dg)
        mesh=evaluated.to_mesh()
        mesh.calc_loop_triangles()
        triangles=len(mesh.loop_triangles)
        tris+=triangles;base+=len(obj.data.loop_triangles)
        coords.extend(obj.matrix_world@v.co for v in mesh.vertices)
        objects.append({'name':obj.name,'base_triangles':len(obj.data.loop_triangles),
                        'evaluated_triangles':triangles,
                        'modifiers':[{'name':m.name,'type':m.type} for m in obj.modifiers]})
        evaluated.to_mesh_clear()
    low=[min(v[i] for v in coords) for i in range(3)]
    high=[max(v[i] for v in coords) for i in range(3)]
    return {'scene':SCENE.name,'asset_mesh_objects':len(objects),
            'base_triangles':base,'evaluated_triangles_including_mirror_bevel':tris,
            'dimensions_m':[high[i]-low[i] for i in range(3)],
            'minimum_m':low,'maximum_m':high,'parts':objects,
            'materials':[{'name':m.name,'srgb':m.get('base_srgb'),'contract':m.get('render_contract')} for m in MATS],
            'review_ground_is_not_asset':True,'reference_approval_A':'user: blender已开，一比一制作',
            'user_review_B':'pending','ue_imported':False}


def build():
    prepare_scene()
    build_geometry()
    fit_dimensions()
    unwrap_parts()
    setup_review()
    report=summarize()
    (OUT/'Evidence/construction-readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    for obj in bpy.context.selected_objects:obj.select_set(False)
    ROOT_OBJECT.select_set(True)
    bpy.context.view_layer.objects.active=ROOT_OBJECT
    for area in bpy.context.screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.region_3d.view_perspective='CAMERA'
            area.spaces.active.overlay.show_overlays=False
            area.spaces.active.shading.type='MATERIAL'
            area.spaces.active.shading.use_scene_world=True
            area.spaces.active.shading.use_scene_lights=True
    text=bpy.data.texts.get('OUTPOST_B_v1_Build.py') or bpy.data.texts.new('OUTPOST_B_v1_Build.py')
    text.clear();text.write(Path(__file__).read_text(encoding='utf8'))
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Outpost_Symmetric_B_v1.blend'))
    return {k:v for k,v in report.items() if k not in ('parts','materials')}


BUILD_REPORT=build()
