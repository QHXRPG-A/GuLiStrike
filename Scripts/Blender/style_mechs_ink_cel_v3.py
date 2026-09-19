"""Five requested assets: symmetric light-mech paint, three-tone surface and ink.

Run against SpiderMech_SourceStyle_v2.blend. Source body geometry is read-only;
line masks and independently bound outline shells belong to this new version.
"""
import bpy,bmesh,sys,math,json,hashlib,time,collections
import numpy as np
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
sys.path.insert(0,'D:/UE5.7/test1/Scripts/Blender')
import mech_production_common as m
OUT=m.ROOT/'Production_v3_InkCel';OUT.mkdir(exist_ok=True);(OUT/'Textures').mkdir(exist_ok=True)
m.OUT=OUT
KEYS=['Mech_Lightest','FireWeapon_01','MissileWeapon_01','Machinegun_lvl1','SpiderMech']
REPORT={'success':False,'source':bpy.data.filepath,'assets':{},'symmetry':[]}
def checkpoint():
    (OUT/'production_report.json').write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
def digest(mesh):
    h=hashlib.sha256()
    for data,prop,n,kind in [(mesh.vertices,'co',len(mesh.vertices)*3,np.float32),(mesh.loops,'vertex_index',len(mesh.loops),np.int32)]:
        a=np.empty(n,dtype=kind);data.foreach_get(prop,a);h.update(a.tobytes())
    return h.hexdigest()
def linear_hex(h):
    values=[int(h[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in values)+(1,)
INK=linear_hex('152632')
def mirror_paint(o):
    mesh=o.data;attr=mesh.color_attributes['SourceRegion_CleanPaint']
    left=[f for f in mesh.polygons if f.center.x<-.001]
    tree=BVHTree.FromPolygons([v.co for v in mesh.vertices],[tuple(f.vertices) for f in left])
    tolerance=max(max(v.co[i] for v in mesh.vertices)-min(v.co[i] for v in mesh.vertices) for i in range(3))*.0015
    fixed=0;matched=0
    for f in mesh.polygons:
        if f.center.x<=.001:continue
        target=Vector((-f.center.x,f.center.y,f.center.z));near=tree.find_nearest(target)
        if near[3]>tolerance:continue
        old=left[near[2]];n=Vector((-f.normal.x,f.normal.y,f.normal.z))
        if n.dot(old.normal)<.97:continue
        c=attr.data[old.loop_start].color[:];matched+=1
        if any(abs(attr.data[f.loop_start].color[i]-c[i])>.0001 for i in range(4)):fixed+=1
        for li in f.loop_indices:attr.data[li].color=c
    REPORT['symmetry'].append({'object':o.name,'matched_positive_faces':matched,'repainted_faces':fixed,'reference_half':'local negative X','tolerance_local':tolerance})
def analyze_ink(o,span,scale):
    me=o.data;bm=bmesh.new();bm.from_mesh(me)
    originals=bm.faces.layers.int.new('Original_Face')
    for f in bm.faces:f[originals]=f.index
    deform=bm.verts.layers.deform.active
    pairs=bmesh.ops.find_doubles(bm,verts=list(bm.verts),dist=.0001)['targetmap']
    if deform:
        pairs={a:b for a,b in pairs.items() if dict(a[deform])==dict(b[deform])}
    bmesh.ops.weld_verts(bm,targetmap=pairs);bm.normal_update()
    color=bm.loops.layers.float_color.get('SourceRegion_CleanPaint')
    possible=set()
    for e in bm.edges:
        if not e.link_faces:continue
        if e.is_boundary:take=True
        elif len(e.link_faces)==2:
            a,b=e.link_faces
            paint=color and sum(abs(a.loops[0][color][i]-b.loops[0][color][i]) for i in range(3))>.12
            take=e.calc_face_angle(0)>math.radians(38) or bool(paint)
        else:take=False
        if take:possible.add(e)
    selected=[];rejected=0
    while possible:
        first=possible.pop();chain={first};todo=[first]
        while todo:
            edge=todo.pop()
            for v in edge.verts:
                for adjacent in v.link_edges:
                    if adjacent in possible:possible.remove(adjacent);chain.add(adjacent);todo.append(adjacent)
        verts={v for e in chain for v in e.verts}
        extent=max(max(v.co[i] for v in verts)-min(v.co[i] for v in verts) for i in range(3))*scale
        length=sum(e.calc_length() for e in chain)*scale
        if length<span*.045 or extent<span*.025:rejected+=len(chain);continue
        selected.extend(chain)
    perface=collections.defaultdict(list)
    for e in selected:
        segment=[np.array(v.co[:],dtype=np.float32) for v in e.verts]
        for f in e.link_faces:perface[f[originals]].append(segment)
    count=len(selected);bm.free()
    return perface,{'selected_structural_edges':count,'discarded_small_edges':rejected}
def line_masks(o,perface,radius,tag):
    me=o.data;me.calc_loop_triangles();size=2048 if len(me.vertices)>100000 else 4096
    uv=me.uv_layers[0]
    masks=[np.zeros((size,size),dtype=np.float32) for _ in me.materials]
    covered=[np.zeros((size,size),dtype=bool) for _ in me.materials]
    for t in me.loop_triangles:
        if t.polygon_index not in perface:continue
        coords=np.array([uv.data[i].uv[:] for i in t.loops],dtype=np.float32)*size
        a,b=coords[1]-coords[0],coords[2]-coords[0];det=a[0]*b[1]-a[1]*b[0]
        if abs(det)<1e-8:continue
        low=np.maximum(np.floor(coords.min(axis=0)).astype(int),0);high=np.minimum(np.ceil(coords.max(axis=0)).astype(int),size)
        x0,y0=low;x1,y1=high
        if x0>=x1 or y0>=y1:continue
        gx=np.arange(x0,x1,dtype=np.float32)[None,:]+.5;gy=np.arange(y0,y1,dtype=np.float32)[:,None]+.5
        dx,dy=gx-coords[0,0],gy-coords[0,1]
        s=(dx*b[1]-dy*b[0])/det;u=(a[0]*dy-a[1]*dx)/det;inside=(s>=0)&(u>=0)&(s+u<=1)
        if not inside.any():continue
        vv=np.array([me.vertices[i].co[:] for i in t.vertices],dtype=np.float32)
        p=vv[0]+s[inside,None]*(vv[1]-vv[0])+u[inside,None]*(vv[2]-vv[0])
        aa=max(radius*.22,math.sqrt(np.linalg.norm(np.cross(vv[1]-vv[0],vv[2]-vv[0]))/abs(det))*.65)
        strength=np.zeros(len(p),dtype=np.float32)
        for start,end in perface[t.polygon_index]:
            v=end-start;along=np.clip(((p-start)*v).sum(1)/np.dot(v,v),0,1)
            distance=np.linalg.norm(p-start-along[:,None]*v,axis=1)
            strength=np.maximum(strength,np.clip((radius+aa-distance)/(2*aa),0,1))
        mask=masks[t.material_index];fill=covered[t.material_index]
        patch=mask[y0:y1,x0:x1];patch[inside]=np.maximum(patch[inside],strength);fill[y0:y1,x0:x1]|=inside
    images=[]
    for slot,(mask,fill) in enumerate(zip(masks,covered)):
        # A narrow gutter extends ink at UV boundaries without altering source UVs.
        for _ in range(2):
            weights=np.zeros_like(mask);values=np.zeros_like(mask)
            for axis,shift in ((0,1),(0,-1),(1,1),(1,-1)):
                neighbor=np.roll(fill,shift,axis);weights+=neighbor;values+=np.roll(mask,shift,axis)*neighbor
            grow=(~fill)&(weights>0);mask[grow]=values[grow]/weights[grow];fill|=grow
        image=bpy.data.images.new('INK_'+tag+'_'+str(slot),size,size,alpha=False);image.colorspace_settings.name='Non-Color'
        rgba=np.ones((size,size,4),dtype=np.float32);rgba[:,:,:3]=mask[:,:,None];image.pixels.foreach_set(rgba.reshape(-1))
        image.filepath_raw=str(OUT/'Textures'/(image.name+'.png'));image.file_format='PNG';image.save();image.pack();images.append(image)
    return images
def cel_material(old,mask,uv_name):
    mat=old.copy();mat.name='CEL3_'+old.name;n=mat.node_tree.nodes;l=mat.node_tree.links
    principled=next(x for x in n if x.type=='BSDF_PRINCIPLED');out=next(x for x in n if x.type=='OUTPUT_MATERIAL')
    base=principled.inputs['Base Color'].links[0].from_socket if principled.inputs['Base Color'].is_linked else None
    geo=n.new('ShaderNodeNewGeometry');geo.name='Cel_World_Normal'
    dot=n.new('ShaderNodeVectorMath');dot.operation='DOT_PRODUCT';dot.inputs[1].default_value=Vector((.35,-.55,.76)).normalized();dot.name='Art_Light_Direction';l.new(geo.outputs['Normal'],dot.inputs[0])
    ramp=n.new('ShaderNodeValToRGB');ramp.name='THREE_TONE_Shadow_Mid_Light';ramp.color_ramp.interpolation='CONSTANT';ramp.color_ramp.elements.remove(ramp.color_ramp.elements[1])
    for i,(pos,c) in enumerate([(0,(.28,.34,.45,1)),(.13,(.64,.70,.80,1)),(.56,(1.15,1.10,1.02,1))]):
        e=ramp.color_ramp.elements[0] if i==0 else ramp.color_ramp.elements.new(pos);e.position=pos;e.color=c
    l.new(dot.outputs['Value'],ramp.inputs[0])
    shade=n.new('ShaderNodeMixRGB');shade.blend_type='MULTIPLY';shade.inputs[0].default_value=1;shade.name='Paint_x_Three_Tones'
    if base:l.new(base,shade.inputs[1])
    else:shade.inputs[1].default_value=principled.inputs['Base Color'].default_value
    l.new(ramp.outputs['Color'],shade.inputs[2]);result=shade.outputs[0]
    em=principled.inputs['Emission Color'];strength=principled.inputs['Emission Strength'].default_value
    if strength>0 and (em.is_linked or max(em.default_value[:3])>0):
        add=n.new('ShaderNodeMixRGB');add.blend_type='ADD';add.inputs[0].default_value=strength;l.new(result,add.inputs[1])
        if em.is_linked:l.new(em.links[0].from_socket,add.inputs[2])
        else:add.inputs[2].default_value=em.default_value
        result=add.outputs[0]
    if mask:
        texture=n.new('ShaderNodeTexImage');texture.name='Internal_Structure_Ink';texture.image=mask;texture.interpolation='Linear'
        uv=n.new('ShaderNodeUVMap');uv.uv_map=uv_name;l.new(uv.outputs[0],texture.inputs['Vector'])
        ink=n.new('ShaderNodeMixRGB');ink.name='Fine_Ink_Overlay';ink.inputs[2].default_value=INK;l.new(texture.outputs['Color'],ink.inputs[0]);l.new(result,ink.inputs[1]);result=ink.outputs[0]
    emission=n.new('ShaderNodeEmission');emission.name='Cel_Surface';l.new(result,emission.inputs['Color']);l.new(emission.outputs[0],out.inputs['Surface'])
    mat['style']='3 discrete tones from editable world art-light direction; independent fine line mask'
    return mat
def outline_material():
    mat=bpy.data.materials.new('INK3_Outer_NoShadow');mat.use_nodes=True;n=mat.node_tree.nodes;l=mat.node_tree.links;n.clear()
    out=n.new('ShaderNodeOutputMaterial');em=n.new('ShaderNodeEmission');em.inputs['Color'].default_value=INK
    transparent=n.new('ShaderNodeBsdfTransparent');geometry=n.new('ShaderNodeNewGeometry');path=n.new('ShaderNodeLightPath')
    # The expanded original-winding shell is visible only on back faces.
    front=n.new('ShaderNodeMath');front.operation='SUBTRACT';front.inputs[0].default_value=1;l.new(geometry.outputs['Backfacing'],front.inputs[1])
    gate=n.new('ShaderNodeMath');gate.operation='MAXIMUM';l.new(front.outputs[0],gate.inputs[0]);l.new(path.outputs['Is Shadow Ray'],gate.inputs[1])
    mix=n.new('ShaderNodeMixShader');l.new(gate.outputs[0],mix.inputs[0]);l.new(em.outputs[0],mix.inputs[1]);l.new(transparent.outputs[0],mix.inputs[2]);l.new(mix.outputs[0],out.inputs['Surface'])
    mat.diffuse_color=INK;return mat
INKMAT=outline_material()
def outline(o,width,scale):
    shell=o.copy();shell.data=o.data.copy();shell.name='INK3_Outline_'+o.name
    for col in o.users_collection:col.objects.link(shell)
    shell.data.materials.clear();shell.data.materials.append(INKMAT)
    for f in shell.data.polygons:f.material_index=0
    # Equal-position vertices share expansion normals within their rigid part.
    # UV/hard-normal splits must not open cracks in the contour shell.
    me=shell.data;co=np.empty(len(me.vertices)*3,np.float32);normal=np.empty_like(co)
    me.vertices.foreach_get('co',co);me.vertices.foreach_get('normal',normal)
    co=co.reshape(-1,3);normal=normal.reshape(-1,3)
    groups=np.array([max(v.groups,key=lambda g:g.weight).group if v.groups else -1 for v in me.vertices],dtype=np.int32)
    keys=np.column_stack((np.rint(co*10000).astype(np.int32),groups))
    _,inverse=np.unique(keys,axis=0,return_inverse=True);averaged=np.zeros((inverse.max()+1,3),np.float32)
    np.add.at(averaged,inverse,normal);length=np.linalg.norm(averaged,axis=1)
    averaged/=np.maximum(length[:,None],1e-8)
    me.vertices.foreach_set('co',(co+averaged[inverse]*(width/scale)).reshape(-1));me.update()
    for mod in list(shell.modifiers):
        if mod.type!='ARMATURE':shell.modifiers.remove(mod)
    shell.visible_shadow=False;shell.hide_select=True;shell['ink_layer']='outer';shell['width_world_m']=width
    return shell
def render(key,view):
    scene=bpy.context.scene;cam=scene.camera
    bounds=REPORT['assets'][key]['bounds'];lo=Vector(bounds['min']);hi=Vector(bounds['max']);center=(lo+hi)/2;span=max(hi-lo)
    d={'Hero':Vector((1.5,-2,1.05)),'Front':Vector((0,-1,0)),'Side':Vector((1,0,0)),'Rear':Vector((0,1,0))}[view].normalized()
    cam.location=center+d*span*3;rotation=(center-cam.location).to_track_quat('-Z','Y');cam.rotation_euler=rotation.to_euler();cam.data.type='ORTHO'
    pts=[Vector((x,y,z)) for x in (lo.x,hi.x) for y in (lo.y,hi.y) for z in (lo.z,hi.z)]
    right=rotation@Vector((1,0,0));up=rotation@Vector((0,1,0))
    width=max(p.dot(right) for p in pts)-min(p.dot(right) for p in pts)
    height=max(p.dot(up) for p in pts)-min(p.dot(up) for p in pts)
    cam.data.ortho_scale=max(width,height*scene.render.resolution_x/scene.render.resolution_y)*1.12
    scene.render.filepath=str(OUT/'Previews'/(key+'_'+view+'.png'));(OUT/'Previews').mkdir(exist_ok=True);bpy.ops.render.render(write_still=True)
for key in KEYS:
    scene=bpy.data.scenes['Review_'+key];bpy.context.window.scene=scene
    col=bpy.data.collections['WORK_'+key];col.hide_viewport=False;col.hide_render=False
    bpy.context.view_layer.update()
    body=[o for o in m.meshes(key) if not o.get('ink_layer')]
    points=[o.matrix_world@Vector(v) for o in body for v in o.bound_box]
    span=max(max(p[i] for p in points)-min(p[i] for p in points) for i in range(3))
    baseline={o.name:digest(o.data) for o in body};record={'meshes':[],'span_m':span,'bounds':{'min':[min(p[i] for p in points) for i in range(3)],'max':[max(p[i] for p in points) for i in range(3)]}};REPORT['assets'][key]=record
    if key=='Mech_Lightest':
        for o in body:
            if o.name.endswith('__Cockpit_Jet') or o.name.endswith('__Mech_Legs_Lt.001'):mirror_paint(o)
    for index,o in enumerate(body):
        scale=o.matrix_world.to_scale().length/math.sqrt(3)
        main=('DETAIL' not in o.name and 'ArmorSeam' not in o.name)
        details={'object':o.name,'geometry_before':baseline[o.name]};masks=None
        if main and o.data.uv_layers:
            edges,stats=analyze_ink(o,span,scale);details.update(stats)
            masks=line_masks(o,edges,span*(.00062 if key=='SpiderMech' else .0011)/scale,key+'_'+str(index))
        for slot,old in enumerate(list(o.data.materials)):
            o.data.materials[slot]=cel_material(old,masks[slot] if masks else None,o.data.uv_layers[0].name if masks else '')
        if main:
            shell=outline(o,span*.0025,scale);details['outline']=shell.name
        details['geometry_unchanged']=digest(o.data)==baseline[o.name];assert details['geometry_unchanged']
        o.data.calc_loop_triangles();details['body_triangles']=len(o.data.loop_triangles);record['meshes'].append(details);checkpoint()
    scene.render.engine='CYCLES';scene.cycles.samples=48;scene.cycles.use_denoising=True;scene.cycles.device='GPU'
    scene.render.resolution_x=1400;scene.render.resolution_y=1200;scene.render.resolution_percentage=100
    scene.view_settings.view_transform='Standard';scene.view_settings.look='None';scene.view_settings.exposure=0
    for view in (['Rear','Hero'] if key=='Mech_Lightest' else ['Hero']):render(key,view)
    checkpoint()
bpy.context.window.scene=bpy.data.scenes['Review_Mech_Lightest']
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':area.spaces.active.shading.type='MATERIAL';area.spaces.active.region_3d.view_perspective='CAMERA'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Mechs_InkCel_v3.blend'),check_existing=False)
REPORT['success']=True;checkpoint()
