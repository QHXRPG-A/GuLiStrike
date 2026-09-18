"""Material-only Ship component study on the preserved original meshes.

The user explicitly prohibited geometry changes. No bevel, remesh, subdivision,
armor additions or topology cleanup is allowed. UV/material data may change.
The optional outline is a separately switchable render shell. Existing authoring
sources are read-only. Source and finished body geometry hashes must match.
"""
import bpy
import collections
import hashlib
import importlib.util
import json
import math
import sys
from pathlib import Path
from mathutils import Vector, Matrix
from mathutils.kdtree import KDTree
import numpy as np

PROJECT=Path('D:/UE5.7/test1')
ROOT=PROJECT/'ArtSource/Ships/ShipComponentStyle_20260917'
OUT=ROOT/'Production/v4'
TEX=OUT/'Textures'
PRE=ROOT/'Previews/v4'
for folder in (OUT,TEX,PRE): folder.mkdir(parents=True,exist_ok=True)
SNAP=json.loads((ROOT/'Source/source_snapshot_v1.json').read_text(encoding='utf-8'))
SIZE=2048
PALETTE={'Pearl':'DDE6E6','Slate':'4B6577','Navy':'23384A','Steel':'8298A5',
         'Ochre':'C9903E','Cyan':'348B9F','Brick':'B16352','Ink':'213D50'}
UV_NAME='SC_PaintUV'
LINE_UV='SC_LineUV'
SOURCE_FILES={key:ROOT/'Source/ExistingAuthoring'/('SC_'+key+'.blend')
              for key in ('Twin_Barrel_Turret','CIWS')}
SOURCE_FILES['Thor_MissilePod']=ROOT/'Source/FBX_static_v1/SM_SC_Thor_MissilePod.fbx'

def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod);return mod

ink_bake=module('existing_ship_ink',PROJECT/'Scripts/Blender/bake_ship_lineart_mask.py')
ink_bake.SIZE=SIZE
rig_tools=module('existing_ship_rigs',PROJECT/'Scripts/Blender/build_ship_component_rigs.py')
measure_tools=module('existing_source_measurements',PROJECT/'Scripts/Blender/build_ship_component_style_models.py')

def dump(path,data):path.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
def filehash(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def point(p):return Vector((p[0]/100,-p[1]/100,p[2]/100))
def linear(h):
    rgb=[int(h[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(c/12.92 if c<=.04045 else ((c+.055)/1.055)**2.4 for c in rgb)+(1,)
def select(obj):
    bpy.ops.object.select_all(action='DESELECT');obj.select_set(True);bpy.context.view_layer.objects.active=obj
def bounds(points):return (Vector([min(p[i] for p in points) for i in range(3)]),Vector([max(p[i] for p in points) for i in range(3)]))

def load_original(key):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene=bpy.context.scene;scene.name=key+'_Material_Study'
    scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
    path=SOURCE_FILES[key]
    if key!='Thor_MissilePod':
        with bpy.data.libraries.load(str(path),link=False) as (src,dst):
            dst.objects=[n for n in src.objects if n in ('SKM_SC_'+key,'Armature')]
        for obj in dst.objects:
            if obj:scene.collection.objects.link(obj)
        body=next(o for o in dst.objects if o and o.type=='MESH')
        arm=next(o for o in dst.objects if o and o.type=='ARMATURE')
        arm.animation_data_clear()
        for bone in arm.pose.bones:bone.matrix_basis.identity()
        assert set(arm.data.bones.keys())=={'Root','BarrelPitch'}
        assert all(m.type=='ARMATURE' for m in body.modifiers),'Source has construction modifiers; inspect before changing it'
        assert body.matrix_world==Matrix.Identity(4),'Expected preserved authoring world basis'
    else:
        bpy.ops.import_scene.fbx(filepath=str(path),use_anim=False)
        body=next(o for o in scene.objects if o.type=='MESH');arm=None
        select(body);bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
        for obj in list(scene.objects):
            if obj!=body:bpy.data.objects.remove(obj,do_unlink=True)
    body.hide_set(False);body.hide_render=False
    collection=bpy.data.collections.new('ORIGINAL_BODY_UNCHANGED');scene.collection.children.link(collection)
    for obj in [body]+([arm] if arm else []):
        for c in list(obj.users_collection):c.objects.unlink(obj)
        collection.objects.link(obj)
    body['source_file']=str(path.relative_to(ROOT));body['production_scope']='Materials, UV and linework only; original body geometry retained'
    bpy.context.view_layer.update()
    src=json.loads((ROOT/'Source'/(key+'_original_geometry.json')).read_text(encoding='utf-8'))
    seed_by_id={i:c['seed'] for c in src['components'] for i in c['ids']}
    moving=set(rig_tools.SPECS[key]['moving']) if arm else set()
    group_names={g.index:g.name for g in body.vertex_groups}
    owners=[next((group_names[g.group] for g in v.groups if g.weight>.5),'Root') for v in body.data.vertices]
    trees={}
    for owner in set(owners):
        ids=[i for i in range(len(src['geometry']['points'])) if (seed_by_id[i] in moving)==(owner=='BarrelPitch')]
        kd=KDTree(len(ids))
        for i in ids:kd.insert(point(src['geometry']['points'][i]),i)
        kd.balance();trees[owner]=kd
    matches=[trees[owners[v.index]].find(v.co) for v in body.data.vertices]
    # Previous clearancing introduced vertices close to the moving shroud. A
    # nearest-vertex guess per triangle misclassified those fixed faces. Recover
    # each whole original part from its unchanged anchor vertices, constrained by
    # the existing rigid-bone ownership. No source coordinates or edges are edited.
    neighbors=collections.defaultdict(list)
    for edge in body.data.edges:
        a,b=edge.vertices;neighbors[a].append(b);neighbors[b].append(a)
    seeds=[None]*len(body.data.vertices);regions=[]
    for start in range(len(seeds)):
        if seeds[start] is not None:continue
        pending=[start];indices=[];visited={start}
        while pending:
            v=pending.pop();indices.append(v)
            for n in neighbors[v]:
                if n not in visited:visited.add(n);pending.append(n)
        assert len({owners[i] for i in indices})==1
        anchors=[i for i in indices if matches[i][2]<.0001]
        votes=collections.Counter(seed_by_id[matches[i][1]] for i in (anchors or indices))
        chosen,count=votes.most_common(1)[0]
        for i in indices:seeds[i]=chosen
        regions.append({'seed':chosen,'vertices':len(indices),'unchanged_anchors':len(anchors),'bone':owners[start],
                        'anchor_vote_fraction':count/sum(votes.values())})
    assert all((seed in moving)==(owner=='BarrelPitch') for seed,owner in zip(seeds,owners))
    body['material_region_recovery']=json.dumps(regions)
    print('ORIGINAL_PART_REGIONS',key,json.dumps(regions),flush=True)
    return body,arm,src,seeds

def material_labels(key,mesh,seeds):
    labels=[]
    bores={seed:measure_tools.thor_top_measurements(src_global,seed)[1] for seed in (866,0,641,450,215)} if key=='Thor_MissilePod' else {}
    for p in mesh.polygons:
        seed=collections.Counter(seeds[v] for v in p.vertices).most_common(1)[0][0]
        n=p.normal;c=p.center
        label='Pearl'
        if key=='Twin_Barrel_Turret':
            if seed in (152,642):label='Slate'
            elif seed in (224,232,724,732,122,240):label='Ochre'
            elif seed in (136,454,462,482,502):label='Slate'
            elif seed==326:label='Navy'
            elif seed in (0,57) and n.z<.4:label='Slate'
            elif seed in (438,522,538,554,578,594,610,626):label='Steel'
        elif key=='CIWS':
            if seed in (16,243,284):label='Slate'
            elif seed==57:label='Cyan'
            elif seed==194 or seed in (325,345,365):label='Steel'
            elif seed in (154,178):label='Slate'
            elif seed==97:
                if c.z<-.70:label='Navy'
                elif abs(c.x)<.98 and c.y<.65:label='Slate'
                elif abs(n.x)>.75:label='Cyan'
        else:
            if seed in (866,0,641,450,215):
                # Original top lid and chamfer remain exactly the same mesh.
                if n.z>.95 and c.z>2.88:label='Brick'
                elif c.z>2.28 and abs(n.z)>.15:label='Steel'
                elif n.z<-.5 or c.z<-2.4:label='Navy'
                elif abs(n.z)<.2:
                    label='Navy' if min(math.dist((c.x,-c.y),h) for h in bores[seed])<1.63 else 'Pearl'
            elif seed in (386,621,631,856,1091):label='Navy'
        labels.append(label)
    # Assign one paint color to each continuous coplanar region. Triangle
    # centroids must never create diagonal patches across a single armor face.
    parent=list(range(len(mesh.polygons)))
    def root(i):
        while parent[i]!=i:parent[i]=parent[parent[i]];i=parent[i]
        return i
    edges=collections.defaultdict(list)
    for p in mesh.polygons:
        ids=list(p.vertices)
        for a,b in zip(ids,ids[1:]+ids[:1]):
            edge=tuple(sorted(tuple(round(x,5) for x in mesh.vertices[v].co) for v in (a,b)))
            edges[edge].append(p.index)
    for faces in edges.values():
        for a in faces:
            for b in faces:
                if a>=b:continue
                pa,pb=mesh.polygons[a],mesh.polygons[b]
                if seeds[pa.vertices[0]]==seeds[pb.vertices[0]] and pa.normal.dot(pb.normal)>.999995:
                    parent[root(b)]=root(a)
    groups=collections.defaultdict(list)
    for p in mesh.polygons:groups[root(p.index)].append(p.index)
    for faces in groups.values():
        votes=collections.defaultdict(float)
        for i in faces:votes[labels[i]]+=mesh.polygons[i].area
        chosen=max(votes,key=votes.get)
        for i in faces:labels[i]=chosen
    return labels

def toon_material(name,base,mask=None):
    mat=bpy.data.materials.new(name);mat.use_nodes=True
    mat.diffuse_color=linear(PALETTE.get(base,PALETTE['Pearl']))
    nodes=mat.node_tree.nodes;links=mat.node_tree.links;nodes.clear()
    out=nodes.new('ShaderNodeOutputMaterial');out.location=(850,160)
    emit=nodes.new('ShaderNodeEmission');emit.location=(640,160);links.new(emit.outputs[0],out.inputs['Surface'])
    diffuse=nodes.new('ShaderNodeBsdfDiffuse');diffuse.location=(-900,0)
    diffuse.inputs['Color'].default_value=(1,1,1,1);diffuse.inputs['Roughness'].default_value=0
    light=nodes.new('ShaderNodeShaderToRGB');light.name='Scene_Light_And_Shadow';light.location=(-680,0)
    links.new(diffuse.outputs[0],light.inputs[0])
    luminance=nodes.new('ShaderNodeRGBToBW');luminance.location=(-500,0);links.new(light.outputs['Color'],luminance.inputs[0])
    ramp=nodes.new('ShaderNodeValToRGB');ramp.name='Three_Tone_Lighting';ramp.label='Scene light + self-shadow / 3 tones';ramp.location=(-290,0)
    ramp.color_ramp.interpolation='CONSTANT'
    ramp.color_ramp.elements[0].position=0;ramp.color_ramp.elements[0].color=(.43,.50,.60,1)
    ramp.color_ramp.elements[1].position=.46;ramp.color_ramp.elements[1].color=(1,1,1,1)
    mid=ramp.color_ramp.elements.new(.18);mid.color=(.72,.79,.84,1)
    links.new(luminance.outputs[0],ramp.inputs[0])
    uv=nodes.new('ShaderNodeUVMap');uv.uv_map=UV_NAME;uv.location=(-880,480)
    if isinstance(base,str):
        color=nodes.new('ShaderNodeRGB');color.outputs[0].default_value=linear(PALETTE[base]);color.location=(-620,330)
    else:
        color=nodes.new('ShaderNodeTexImage');color.image=base;color.name='BaseColor_2K';color.location=(-620,380)
        color.interpolation='Linear';color.extension='EXTEND';links.new(uv.outputs[0],color.inputs[0])
    multiply=nodes.new('ShaderNodeMixRGB');multiply.blend_type='MULTIPLY';multiply.inputs[0].default_value=1;multiply.location=(-120,220)
    links.new(color.outputs['Color'],multiply.inputs[1]);links.new(ramp.outputs['Color'],multiply.inputs[2])
    if mask:
        lineuv=nodes.new('ShaderNodeUVMap');lineuv.uv_map=LINE_UV;lineuv.location=(-880,-370)
        tex=nodes.new('ShaderNodeTexImage');tex.image=mask;tex.name='Internal_LineMask_2K';tex.location=(-630,-370)
        tex.interpolation='Linear';tex.extension='EXTEND';links.new(lineuv.outputs[0],tex.inputs[0])
        strength=nodes.new('ShaderNodeValue');strength.name='Line_Strength';strength.label='Internal ink 0 = off';strength.outputs[0].default_value=.85;strength.location=(-430,-150)
        factor=nodes.new('ShaderNodeMath');factor.operation='MULTIPLY';factor.use_clamp=True;factor.location=(-110,-160)
        links.new(tex.outputs['Color'],factor.inputs[0]);links.new(strength.outputs[0],factor.inputs[1])
        mix=nodes.new('ShaderNodeMixRGB');mix.inputs[2].default_value=linear(PALETTE['Ink']);mix.location=(370,160)
        links.new(factor.outputs[0],mix.inputs[0]);links.new(multiply.outputs[0],mix.inputs[1]);links.new(mix.outputs[0],emit.inputs['Color'])
    else:links.new(multiply.outputs[0],emit.inputs['Color'])
    mat['shader']='EEVEE diffuse Shader-to-RGB scene lighting + self-shadow, 3-tone ramp, BaseColor and independent unlit line mask'
    return mat

def outline(body,key):
    col=bpy.data.collections.new('OUTLINE_TOGGLE');bpy.context.scene.collection.children.link(col)
    obj=body.copy();obj.data=body.data.copy();obj.name=key+'_Optional_Outline';col.objects.link(obj)
    for m in list(obj.modifiers):obj.modifiers.remove(m)
    mat=bpy.data.materials.new(key+'_Outline_Ink');mat.use_nodes=True;mat.use_backface_culling=True
    nodes=mat.node_tree.nodes;nodes.clear();out=nodes.new('ShaderNodeOutputMaterial');em=nodes.new('ShaderNodeEmission')
    em.inputs['Color'].default_value=linear(PALETTE['Ink']);mat.node_tree.links.new(em.outputs[0],out.inputs['Surface'])
    obj.data.materials.clear();obj.data.materials.append(mat)
    for p in obj.data.polygons:p.material_index=0
    # Flip only the separate shell, then offset along its inward normals with
    # negative strength. The original object's vertices are never touched.
    obj.data.flip_normals()
    obj.data.normals_split_custom_set([(0,0,0)]*len(obj.data.loops))
    width=.010 if key=='CIWS' else .065
    disp=obj.modifiers.new('Outline_Width','DISPLACE');disp.strength=-width;disp.mid_level=0;disp.direction='NORMAL'
    for original in body.modifiers:
        skin=obj.modifiers.new(original.name,original.type);skin.object=original.object
    obj.visible_shadow=False
    obj['render_helper_only']=True;obj['outline_width_m']=width;obj['no_collision']=True
    return obj

def socket_markers(key,arm):
    markers=[]
    for s in SNAP['parts'][key]['sockets']:
        ob=bpy.data.objects.new(s['name'],None);bpy.context.scene.collection.objects.link(ob)
        ob.location=point(s.get('mesh_space',{}).get('location',s['location']))
        ob.rotation_euler=(0,0,-math.pi/2) if arm else (0,0,0)
        ob.empty_display_type='ARROWS';ob.empty_display_size=.15 if key=='CIWS' else .7
        ob.hide_render=True;ob['ue_socket']=json.dumps(s)
        if arm:
            bpy.context.view_layer.update();world=ob.matrix_world.copy()
            ob.parent=arm;ob.parent_type='BONE';ob.parent_bone='BarrelPitch'
            bpy.context.view_layer.update();ob.matrix_world=world
        markers.append(ob)
    bpy.context.view_layer.update();return markers

def stage(body):
    scene=bpy.context.scene;scene.render.engine='BLENDER_EEVEE'
    scene.render.resolution_x=1440;scene.render.resolution_y=1080;scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA'
    scene.render.film_transparent=False;scene.view_settings.view_transform='Standard'
    scene.view_settings.look='None'
    scene.world=bpy.data.worlds.new('BlueGray_Studio');scene.world.use_nodes=True
    scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.21,.29,.34,1)
    scene.world.node_tree.nodes['Background'].inputs[1].default_value=.8
    camera=bpy.data.objects.new('Material_Review_Camera',bpy.data.cameras.new('Material_Review_Camera'));scene.collection.objects.link(camera);scene.camera=camera
    camera.data.type='ORTHO';camera.data.clip_start=.01;camera.data.clip_end=3000
    points=[body.matrix_world@v.co for v in body.data.vertices];lo,hi=bounds(points);span=max(hi-lo)
    groundmat=bpy.data.materials.new('Studio_Ground');groundmat.use_nodes=True
    bsdf=groundmat.node_tree.nodes.get('Principled BSDF');bsdf.inputs['Base Color'].default_value=(.26,.35,.40,1);bsdf.inputs['Roughness'].default_value=.9
    bpy.ops.mesh.primitive_plane_add(size=span*200,location=(0,0,lo.z-span*.025));ground=bpy.context.object;ground.name='Studio_Ground';ground.data.materials.append(groundmat)
    light=bpy.data.objects.new('Studio_Key',bpy.data.lights.new('Studio_Key','SUN'));scene.collection.objects.link(light)
    light.data.energy=2;light.data.angle=.14;light.rotation_euler=Vector((.45,.55,-.75)).to_track_quat('-Z','Y').to_euler()
    return camera,ground,points

def aim(camera,points,direction,margin=1.16):
    lo,hi=bounds(points);center=(lo+hi)/2;span=max(hi-lo)
    camera.location=center+Vector(direction).normalized()*span*3
    camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler()
    bpy.context.view_layer.update();coords=[camera.matrix_world.inverted()@p for p in points]
    a,b=bounds(coords);offset=Vector(((a.x+b.x)/2,(a.y+b.y)/2,0));camera.location+=camera.rotation_euler.to_matrix()@offset
    camera.data.ortho_scale=max(b.x-a.x,(b.y-a.y)*4/3)*margin

def render(path):
    bpy.context.scene.render.filepath=str(path);bpy.ops.render.render(write_still=True)

def unwrap(body):
    old=[u.name for u in body.data.uv_layers]
    uv=body.data.uv_layers.new(name=UV_NAME);body.data.uv_layers.active=uv;uv.active_render=True
    select(body);bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66),margin_method='FRACTION',island_margin=.008,
                             area_weight=0,correct_aspect=True,scale_to_bounds=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    uv=body.data.uv_layers[UV_NAME]
    for row in uv.data:row.uv=row.uv*.97+Vector((.015,.015))
    line=body.data.uv_layers.new(name=LINE_UV)
    values=np.empty(len(uv.data)*2,dtype=np.float32);uv.data.foreach_get('uv',values);line.data.foreach_set('uv',values)
    body.data.uv_layers.active=uv;uv.active_render=True
    return uv,old

def save_image(key,suffix,pixels,noncolor=False):
    name='T_SC_'+key+'_'+suffix+'_2K';im=bpy.data.images.new(name,width=SIZE,height=SIZE,alpha=False,float_buffer=True)
    im.colorspace_settings.name='Non-Color' if noncolor else 'sRGB'
    if pixels.ndim==2:pixels=np.repeat(pixels[:,:,None],3,axis=2)
    rgba=np.ones((SIZE,SIZE,4),dtype=np.float32)
    # Image.save writes generated buffer values directly. Encode linear paint to
    # sRGB bytes before saving; the sampled BaseColor is then decoded exactly once.
    rgba[:,:,:3]=pixels if noncolor else np.where(pixels<=.0031308,pixels*12.92,1.055*np.power(pixels,1/2.4)-.055)
    im.pixels.foreach_set(rgba.ravel());im.filepath_raw=str(TEX/(name+'.png'));im.file_format='PNG';im.save()
    # Reload the actual delivery bytes so the preview exercises the saved texture.
    saved=bpy.data.images.load(im.filepath_raw,check_existing=False);saved.colorspace_settings.name=im.colorspace_settings.name
    saved.name=name+'_Packed';saved.pack();saved.filepath='//Textures/'+name+'.png'
    bpy.data.images.remove(im)
    return saved

def color_atlas(body,uv,labels):
    mesh=body.data;mesh.calc_loop_triangles()
    paint=np.zeros((SIZE,SIZE,3),dtype=np.float32);orm=np.zeros_like(paint);filled=np.zeros((SIZE,SIZE),dtype=bool)
    for tri in mesh.loop_triangles:
        coords=np.array([uv.data[i].uv[:] for i in tri.loops],dtype=np.float32)*SIZE
        a,b=coords[1]-coords[0],coords[2]-coords[0];det=a[0]*b[1]-a[1]*b[0]
        if abs(det)<1e-7:continue
        low=np.maximum(np.floor(coords.min(axis=0)).astype(int)-1,0);high=np.minimum(np.ceil(coords.max(axis=0)).astype(int)+1,SIZE)
        x0,y0=low;x1,y1=high
        x=np.arange(x0,x1,dtype=np.float32)[None,:]+.5-coords[0,0];y=np.arange(y0,y1,dtype=np.float32)[:,None]+.5-coords[0,1]
        s=(x*b[1]-y*b[0])/det;t=(a[0]*y-a[1]*x)/det
        inside=(s>=-.001)&(t>=-.001)&(s+t<=1.001)
        label=labels[tri.polygon_index];paint[y0:y1,x0:x1][inside]=linear(PALETTE[label])[:3]
        metallic=.65 if label in ('Steel','Slate','Navy') else .1;rough=.5 if metallic>.2 else .64
        orm[y0:y1,x0:x1][inside]=(1,rough,metallic);filled[y0:y1,x0:x1]|=inside
    coverage=float(filled.mean())
    for _ in range(8):
        weight=np.zeros((SIZE,SIZE),dtype=np.float32);p=np.zeros_like(paint);o=np.zeros_like(orm)
        for axis,shift in ((0,1),(0,-1),(1,1),(1,-1)):
            neighbor=np.roll(filled,shift,axis=axis);weight+=neighbor
            p+=np.roll(paint,shift,axis=axis)*neighbor[:,:,None];o+=np.roll(orm,shift,axis=axis)*neighbor[:,:,None]
        new=(~filled)&(weight>0);paint[new]=p[new]/weight[new,None];orm[new]=o[new]/weight[new,None];filled|=new
    return paint,orm,coverage

def line_faces(mesh,labels,key):
    # Match geometric edges across imported split vertices. Coplanar triangle
    # diagonals and small faceting edges are deliberately excluded.
    edges=collections.defaultdict(list)
    for p in mesh.polygons:
        ids=list(p.vertices)
        for a,b in zip(ids,ids[1:]+ids[:1]):
            pa=tuple(round(v,5) for v in mesh.vertices[a].co);pb=tuple(round(v,5) for v in mesh.vertices[b].co)
            edges[tuple(sorted((pa,pb)))].append(p.index)
    result={p.index:[] for p in mesh.polygons};selected=0
    radius=.006 if key=='CIWS' else .033
    minimum=.045 if key=='CIWS' else .23
    for (a,b),faces in edges.items():
        faces=list(set(faces));va,vb=Vector(a),Vector(b)
        if (va-vb).length<minimum:continue
        sharp=len(faces)==1
        if len(faces)>1:
            sharp=any(mesh.polygons[x].normal.dot(mesh.polygons[y].normal)<math.cos(math.radians(40))
                      or labels[x]!=labels[y] for i,x in enumerate(faces) for y in faces[i+1:])
        if not sharp:continue
        selected+=1
        for p in faces:result[p].append((np.array(a,dtype=np.float32),np.array(b,dtype=np.float32),radius))
    return result,selected,radius

def pose(arm,angle,pivot):
    delta=Matrix.Rotation(math.radians(-angle),4,'X')
    arm.pose.bones['BarrelPitch'].matrix=Matrix.Translation(pivot)@delta@Matrix.Translation(-pivot)@arm.data.bones['BarrelPitch'].matrix_local
    bpy.context.view_layer.update()

def motion_audit(key,body,arm,markers):
    if not arm:return {'status':'not_applicable_static'}
    groups={g.index:g.name for g in body.vertex_groups}
    owners=[]
    for v in body.data.vertices:
        active=[g for g in v.groups if g.weight>1e-7]
        assert len(active)==1 and abs(active[0].weight-1)<1e-7,'Original source is not rigid single weight'
        owners.append(groups[active[0].group])
    pivot=point(SNAP['parts'][key]['bones'][1]['local']['location'])
    assert (arm.data.bones['BarrelPitch'].head_local-pivot).length<.00001
    original=[v.co.copy() for v in body.data.vertices]
    socket_rest=[m.matrix_world.translation.copy() for m in markers]
    maxerr=0;socketerr=0
    for angle in range(-15,76):
        pose(arm,angle,pivot);delta=Matrix.Rotation(math.radians(-angle),4,'X')
        evaluated=body.evaluated_get(bpy.context.evaluated_depsgraph_get());mesh=evaluated.to_mesh()
        for i,v in enumerate(mesh.vertices):
            expected=original[i] if owners[i]=='Root' else pivot+delta@(original[i]-pivot)
            maxerr=max(maxerr,(v.co-expected).length)
        evaluated.to_mesh_clear()
        for m,p in zip(markers,socket_rest):socketerr=max(socketerr,(m.matrix_world.translation-(pivot+delta@(p-pivot))).length)
    pose(arm,0,pivot)
    crossing=rig_tools.collision_audit(body,[1 if o=='BarrelPitch' else 0 for o in owners],
               {'moving':[1],'pivot':SNAP['parts'][key]['bones'][1]['local']['location'],'sign':1},step=1)
    assert maxerr<.00005 and socketerr<.0001,(maxerr,socketerr)
    return {'status':'passed' if not any(crossing.values()) else 'source_surface_crossings_recorded',
            'sample_count':91,'angles_degrees':[-15,75],'rigid_max_error_m':maxerr,'socket_max_error_m':socketerr,
            'single_bone_weight_one':True,'surface_crossings_by_angle':crossing,'geometry_modified_to_resolve_crossings':False}

def build(key):
    global src_global
    body,arm,src_global,seeds=load_original(key);mesh=body.data
    before=ink_bake.geometry_hash(mesh);normal_before=np.array([n.vector[:] for n in mesh.corner_normals],dtype=np.float32)
    labels=material_labels(key,mesh,seeds)
    # Save original polygons as selectable semantic regions, without splitting,
    # moving, rebuilding, joining or changing any body geometry.
    part=mesh.attributes.new('OriginalComponentSeed','INT','FACE')
    for p in mesh.polygons:part.data[p.index].value=collections.Counter(seeds[v] for v in p.vertices).most_common(1)[0][0]
    mats={name:toon_material(key+'_'+name,name) for name in sorted(set(labels))}
    mesh.materials.clear()
    for mat in mats.values():mesh.materials.append(mat)
    slots={name:i for i,name in enumerate(mats)}
    for p,label in zip(mesh.polygons,labels):p.material_index=slots[label]
    markers=socket_markers(key,arm)
    camera,ground,points=stage(body)
    shell=outline(body,key)
    aim(camera,points,(1.1,-1.7,1.65))
    render(PRE/(key+'_material_preview.png'))
    print('MATERIAL_PALETTE_PREVIEW_READY',key,flush=True)
    uv,old_uv=unwrap(body)
    assert ink_bake.geometry_hash(mesh)==before,'UV operation altered body topology'
    paint,orm,coverage=color_atlas(body,uv,labels)
    color=save_image(key,'BaseColor',paint);packed=save_image(key,'ORM',orm,True)
    del paint,orm
    edges,count,radius=line_faces(mesh,labels,key)
    maskvalues,linecoverage=ink_bake.raster_mask(mesh,uv,edges)
    mask=save_image(key,'LineMask',maskvalues,True)
    mat=toon_material('M_SC_'+key+'_Toon',color,mask)
    # Keep the source palette slots as editable materials, and let each slot use
    # the shared atlas. Material slots remain meaningful for later UE authoring.
    mesh.materials.clear();mesh.materials.append(mat)
    for p in mesh.polygons:p.material_index=0
    body['original_geometry_sha256']=before;body['internal_line_mask']=mask.filepath
    body['source_uv_channels']=json.dumps(old_uv);body['paint_uv']=UV_NAME;body['line_uv']=LINE_UV
    motion=motion_audit(key,body,arm,markers)
    geometry_after=ink_bake.geometry_hash(mesh)
    normal_after=np.array([n.vector[:] for n in mesh.corner_normals],dtype=np.float32)
    assert geometry_after==before,'Body geometry changed during material work'
    assert np.array_equal(normal_before,normal_after),'Original corner normals changed'
    mesh.calc_loop_triangles();lo,hi=bounds(points)
    report={'key':key,'stage':'material_candidate_B_pending','scope':'Original mesh retained; only shading, textures, line mask and optional render outline',
       'source_file':str(SOURCE_FILES[key].relative_to(ROOT)),'source_file_sha256':filehash(SOURCE_FILES[key]),
       'original_geometry_sha256':before,'finished_body_geometry_sha256':geometry_after,'body_geometry_unchanged':True,
       'original_corner_normals_unchanged':True,'material_regions':json.loads(body['material_region_recovery']),
       'connection_fix':'Whole original connected parts and original rigid-bone ownership define color regions; no per-triangle nearest-shroud assignment',
       'vertices':len(mesh.vertices),'polygons':len(mesh.polygons),'body_triangles':len(mesh.loop_triangles),
       'outline_triangles':len(mesh.loop_triangles),'body_material_slots':1,'dimensions_blender_m':list(hi-lo),
       'uv_channels':[u.name for u in mesh.uv_layers],'source_uv_channels':old_uv,'texture_size':[SIZE,SIZE],
       'texture_files':[im.filepath for im in (color,packed,mask)],'atlas_coverage':coverage,'selected_structural_edges':count,
       'internal_line_halfwidth_m':radius,'outline_width_m':shell['outline_width_m'],'motion':motion,
       'sockets':[{'name':m.name,'rest_location_blender_m':list(m.matrix_world.translation)} for m in markers],
       'user_A':'approved color reference; original geometry supersedes concept silhouette','user_B':'pending','UE_formal_validation':'not_run'}
    dump(OUT/(key+'_material_report.json'),report)
    for view,direction in [('hero',(1.1,-1.7,1.65)),('front',(0,-1,0)),('right',(1,0,0)),('rear',(0,1,0)),('top',(0,0,1))]:
        aim(camera,points,direction);render(PRE/(key+'_'+view+'.png'))
    aim(camera,points,(1.1,-1.7,1.65));shell.hide_render=True
    # Same camera, original body and neutral studio shading for direct comparison.
    old_engine=bpy.context.scene.render.engine;bpy.context.scene.render.engine='BLENDER_WORKBENCH'
    shading=bpy.context.scene.display.shading;shading.light='STUDIO';shading.color_type='SINGLE';shading.single_color=(.57,.61,.64)
    shading.show_cavity=True;shading.cavity_type='BOTH';shading.show_shadows=True;ground.hide_render=True
    render(PRE/(key+'_original_geometry_gray.png'))
    bpy.context.scene.render.engine=old_engine;ground.hide_render=False;shell.hide_render=False
    scene=bpy.context.scene;scene['scope']='MATERIALS ONLY / ORIGINAL GEOMETRY RETAINED';scene['review_B']='pending'
    select(body)
    for area in bpy.context.screen.areas if bpy.context.screen else []:
        if area.type=='VIEW_3D':area.spaces.active.region_3d.view_perspective='CAMERA';area.spaces.active.shading.type='MATERIAL'
    path=OUT/(key+'_OriginalMesh_MaterialCandidate.blend');bpy.ops.wm.save_as_mainfile(filepath=str(path))
    report['blend_sha256']=filehash(path);dump(OUT/(key+'_material_report.json'),report)
    print('ORIGINAL_MATERIAL_CANDIDATE_READY',key,json.dumps({k:report[k] for k in ('body_geometry_unchanged','body_triangles','body_material_slots')}),flush=True)

if __name__=='__main__':
    keys=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else list(SOURCE_FILES)
    for key in keys:build(key)
    print('SHIP_ORIGINAL_MATERIAL_STUDY_COMPLETE',flush=True)
