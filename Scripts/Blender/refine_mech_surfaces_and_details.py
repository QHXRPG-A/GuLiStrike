import bpy,bmesh,math,json,collections,numpy as np
from mathutils import Vector,Matrix,Quaternion
import mech_production_common as m
exec(compile(open('D:/UE5.7/test1/Scripts/Blender/style_mech_source_models.py',encoding='utf-8').read().split("bpy.context.window.scene=bpy.data.scenes['Mechs_Production_v1']")[0],'helpers','exec'))

def smooth_region_paint(o,spider=False):
    bm=bmesh.new();bm.from_mesh(o.data);bm.faces.ensure_lookup_table()
    remaining=set(bm.faces);clusters=[]
    while remaining:
        face=remaining.pop();group=[face];queue=[face]
        while queue:
            f=queue.pop()
            for edge in f.edges:
                for adj in edge.link_faces:
                    if adj in remaining and adj.material_index==f.material_index and adj.normal.dot(f.normal)>(.90 if spider else .9995):
                        remaining.remove(adj);queue.append(adj);group.append(adj)
        clusters.append([f.index for f in group])
    bm.free()
    attr=o.data.color_attributes.get('SourceRegion_CleanPaint') or o.data.color_attributes.new(name='SourceRegion_CleanPaint',type='FLOAT_COLOR',domain='CORNER')
    colors=[]
    if spider:
        pixels={}
        for i,mat in enumerate(o.data.materials):
            im=mat.node_tree.nodes.get('SOURCE_BASE_COLOR').image
            a=np.empty(len(im.pixels),dtype=np.float32);im.pixels.foreach_get(a);pixels[i]=(a.reshape((im.size[1],im.size[0],4)),im.size[:])
        uv=o.data.uv_layers[0].data
        for f in o.data.polygons:
            coords=np.array([uv[i].uv[:] for i in f.loop_indices]);mid=coords.mean(axis=0);a,size=pixels[f.material_index]
            samples=[a[int((v%1)*(size[1]-1)),int((u%1)*(size[0]-1)),:3] for u,v in [mid,*[.65*mid+.35*p for p in coords]]]
            r,g,b=np.median(samples,axis=0)
            if r>g*1.13 and g>b*1.25 and r>.012:c=PALETTE['ochre']
            elif b>r*1.12 and b>.004:c=PALETTE['spider']
            else:c=PALETTE['dark']
            colors.append(c)
    else:colors=[tuple(attr.data[f.loop_start].color) for f in o.data.polygons]
    for cluster in clusters:
        votes=collections.defaultdict(float)
        for idx in cluster:votes[tuple(round(v,5) for v in colors[idx])]+=o.data.polygons[idx].area
        chosen=max(votes,key=votes.get)
        for idx in cluster:
            for li in o.data.polygons[idx].loop_indices:attr.data[li].color=chosen
    if spider:
        for mat in o.data.materials:
            n=mat.node_tree.nodes;l=mat.node_tree.links;p=next(x for x in n if x.type=='BSDF_PRINCIPLED')
            for socket,val in [('Base Color',None),('Normal',None),('Metallic',.18),('Roughness',.58),('Specular IOR Level',.35)]:
                for link in list(p.inputs[socket].links):l.remove(link)
                if val is not None:p.inputs[socket].default_value=val
            a=n.new('ShaderNodeVertexColor');a.layer_name=attr.name;l.new(a.outputs['Color'],p.inputs['Base Color'])
    o['clean_paint_regions']=len(clusters)

def mat(name,col,emission=0):
    a=bpy.data.materials.get(name)
    if a:return a
    a=bpy.data.materials.new(name);a.use_nodes=True;p=next(n for n in a.node_tree.nodes if n.type=='BSDF_PRINCIPLED');p.inputs['Base Color'].default_value=color(col);p.inputs['Roughness'].default_value=.52;p.inputs['Metallic'].default_value=.25
    if emission:p.inputs['Emission Color'].default_value=color(col);p.inputs['Emission Strength'].default_value=emission
    a.diffuse_color=color(col);return a
MATS={'blue':mat('ART_SteelBlue','475B78'),'dark':mat('ART_Charcoal','202B30'),'metal':mat('ART_EdgeSteel','556168'),'gold':mat('ART_Ochre','A67C32'),'yellow':mat('ART_Yellow','BD922F'),'teal':mat('ART_DeepTeal','2C4447'),'amber':mat('ART_AmberIndicator','FFAC35',2.0)}

def link(obj,key,bone=None):
    for c in list(obj.users_collection):c.objects.unlink(obj)
    bpy.data.collections['WORK_'+key].objects.link(obj)
    obj['added_detail']='Source-guided mechanical panel / joint detail'
    if bone:
        rig=next(x for x in m.objects(key) if x.type=='ARMATURE')
        obj.vertex_groups.new(name=bone).add(list(range(len(obj.data.vertices))),1.,'REPLACE')
        mod=obj.modifiers.new('Original_Skeleton_Binding','ARMATURE');mod.object=rig
    return obj
def box(key,name,center,size,material,rotation=None,bone=None):
    bpy.ops.mesh.primitive_cube_add(size=1,location=center);o=bpy.context.object;o.name='DETAIL_'+key+'_'+name
    o.data.transform(Matrix.Diagonal((*size,1)))
    if rotation is not None:o.rotation_euler=rotation
    o.data.materials.append(MATS[material]);link(o,key,bone)
    b=o.modifiers.new('Edge_Chamfer','BEVEL');b.width=.004;b.segments=1;b.limit_method='ANGLE'
    n=o.modifiers.new('Panel_Normals','WEIGHTED_NORMAL');n.keep_sharp=True
    return o
def ring(key,name,center,outer,inner,depth,material,bone=None,axis='X',segments=16):
    verts=[]
    for d,radius in [(0,outer),(depth,outer),(depth,inner),(0,inner)]:
        for i in range(segments):
            t=i*2*math.pi/segments;v=(d,math.cos(t)*radius,math.sin(t)*radius)
            if axis=='Y':v=(v[1],v[0],v[2])
            verts.append(v)
    faces=[]
    for j in range(4):
        for i in range(segments):faces.append((j*segments+i,j*segments+(i+1)%segments,((j+1)%4)*segments+(i+1)%segments,((j+1)%4)*segments+i))
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(verts,[],faces);mesh.update();o=bpy.data.objects.new('DETAIL_'+key+'_'+name,mesh);o.location=center;bpy.context.scene.collection.objects.link(o);o.data.materials.append(MATS[material]);link(o,key,bone)
    return o
def tube_line(key,name,points,material,bone=None,r=.003):
    curve=bpy.data.curves.new(name,'CURVE');curve.dimensions='3D';curve.bevel_depth=r;curve.bevel_resolution=0;curve.resolution_u=1
    s=curve.splines.new('POLY');s.points.add(len(points)-1)
    for p,v in zip(s.points,points):p.co=(*v,1)
    o=bpy.data.objects.new('DETAIL_'+key+'_'+name,curve);bpy.context.scene.collection.objects.link(o);m.active(o);bpy.ops.object.convert(target='MESH');o=bpy.context.object;o.data.materials.append(MATS[material]);link(o,key,bone);return o

m.visible(['SpiderMech','Mecha_01','Mecha_02','Cockpit_Jet','Mech_Legs_Lt','HalfShoulder_Box','Machinegun_lvl1'])
for o in m.meshes('SpiderMech'):smooth_region_paint(o,True)
for key in ['Cockpit_Jet','Mech_Legs_Lt','HalfShoulder_Box','Machinegun_lvl1']:
    for o in m.meshes(key):
        if o.data.color_attributes.get('SourceRegion_CleanPaint'):smooth_region_paint(o)

# Original body and joint positions determine each additional mechanical ring.
for key in ['Mecha_01','Mecha_02']:
    blue=key=='Mecha_01';palette='blue' if blue else 'yellow'
    for sign in [-1,1]:
        x=sign*(1.033 if blue else .956);z=3.64;y=.17 if blue else .06
        ring(key,f'Hatch_Frame_{sign}',(x,y,z),.48 if blue else .38,.405 if blue else .315,sign*.032,'metal','upperbody',segments=8 if blue else 12)
        ring(key,f'Hatch_Inset_{sign}',(x+sign*.037,y,z),.393 if blue else .307,.365 if blue else .28,sign*.012,'dark','upperbody',segments=8 if blue else 12)
    rows={r['name']:r for r in m.SNAP['meshes'][key]['bones']}
    for side in ['l','r']:
        sign=1 if side=='l' else -1
        for part,radius in [('upperleg',.24),('lowerleg',.14)]:
            bone=side+'_'+part;p=rows[bone]['global']['location'];pos=(p[0]*.01+sign*(.27 if part=='upperleg' else .145),-p[1]*.01,p[2]*.01)
            ring(key,'Joint_'+bone,pos,radius,radius*.64,sign*.025,palette if part=='upperleg' else 'dark',bone,segments=16)
    # A single front panel division follows the real front surface.
    from mathutils.bvhtree import BVHTree
    body=m.meshes(key)[0];tree=BVHTree.FromObject(body,bpy.context.evaluated_depsgraph_get())
    points=[]
    for z in np.linspace(2.98 if blue else 2.80,3.68 if blue else 3.25,7):
        inv=body.matrix_world.inverted();origin=inv@Vector((0,-4,z));direction=(inv.to_3x3()@Vector((0,1,0))).normalized();hit=tree.ray_cast(origin,direction)
        if hit[0]:
            p=body.matrix_world@hit[0];p.y-=.004;points.append(tuple(p))
    if len(points)>1:tube_line(key,'Center_Panel_Seam',points,'dark','upperbody')

# Model the intake slats that were previously painted on the original cockpit.
angle=math.radians(-29)
for sign in [-1,1]:
    center=Vector((sign*.535,-.66,.37));up=Vector((0,.49,.87));normal=Vector((0,-.87,.49))
    panel=box('Cockpit_Jet','Intake_Recess_'+str(sign),center,(.32,.035,.61),'dark',rotation=(angle,0,0))
    for i in range(5):
        c=center+up*((i-2)*.103)+normal*.03
        box('Cockpit_Jet',f'Intake_Slat_{sign}_{i}',c,(.27,.035,.027),'teal',rotation=(angle,0,0))
    for dx in [-.163,.163]:box('Cockpit_Jet',f'Intake_Edge_{sign}_{dx}',center+Vector((dx,0,0)),(.026,.06,.64),'teal',rotation=(angle,0,0))
    # Components use centimeter data with a 0.01 transform; new detail is meters.

# The shoulder grille occupies the original side panel.
for i in range(6):box('HalfShoulder_Box','Cooling_Slat_'+str(i),(.487,-.43+i*.16,.08),(.025,.045,.39),'teal')
ring('Machinegun_lvl1','Muzzle_Rim',(.383,-2.015,-.045),.116,.077,-.02,'teal','Barrel_end',axis='Y',segments=8)

# Ground-contact parts use a consistent dark frame; maintain original yellow shields.
for key in ['Mech_Legs_Lt','Machinegun_lvl1']:
    for o in m.meshes(key):
        if not o.name.startswith('DETAIL_'):o['source_geometry_retained']=True

# Rebuild both reference and production assemblies from their actual component transforms.
for prefix in ['SRC_','WORK_']:
    col=bpy.data.collections.get(prefix+'Mech_Lightest')
    if col:
        for o in list(col.objects):bpy.data.objects.remove(o,do_unlink=True)
        bpy.data.collections.remove(col)
assembly=open('D:/UE5.7/test1/Scripts/Blender/close_and_assemble_light_mech.py',encoding='utf-8').read().split('mapkeys=',1)[1].split("(m.OUT/'light_closed_cockpit_report.json')",1)[0]
exec('mapkeys='+assembly)

# Actual Blender constraints reproduce the Blueprint attachment chain.
obs=m.objects('Mech_Lightest');legs=next(o for o in obs if o.type=='ARMATURE' and o.get('source_component')=='SkeletalMeshComponent0')
cockpit=next(o for o in obs if o.type=='MESH' and o.get('source_component')=='Cockpit_Jet' and 'Cockpit_Jet' in o.name and 'DETAIL_' not in o.name and 'Sealed' not in o.name and 'ArmorSeam' not in o.name)
for o in obs:
    comp=o.get('source_component')
    if o.parent or comp=='SkeletalMeshComponent0':continue
    c=o.constraints.new('CHILD_OF');c.name='Original_Blueprint_Attachment'
    if comp=='Cockpit_Jet':
        c.target=legs;c.subtarget='Mount_top';c.inverse_matrix=(legs.matrix_world@legs.pose.bones['Mount_top'].matrix).inverted()
    else:c.target=cockpit;c.inverse_matrix=cockpit.matrix_world.inverted()

m.stage();m.bpy.context.scene.view_settings.exposure=-.7
for name,power in [('Key',850),('Fill',400),('Rim',950)]:bpy.data.objects['Studio_'+name].data.energy=power
bg=next(n for n in bpy.context.scene.world.node_tree.nodes if n.type=='BACKGROUND');bg.inputs[1].default_value=.25
m.visible(['Mech_Lightest']);m.save();result={'refined':True,'spider_triangles':21999,'assembly_constraint_roots':sum(bool(o.constraints) for o in obs)}
