"""Ship-matched mechanical vehicles, with exact bilateral assembly and rigid rigs.

Source FBX is retained by export_tactical_style_sources.py. This owns only the
TacticalStyle production directory; never changes the interactive Blender file.
"""
import bpy, bmesh, math, json, sys
from pathlib import Path
from mathutils import Vector, Matrix
import numpy as np

ROOT=Path('D:/UE5.7/test1/ArtSource/TacticalStyle_20260916')
OUT=ROOT/'Production'; OUT.mkdir(exist_ok=True)
PALETTE=['D5E5E4','337F9A','486679','1C3348','EAAA4E','8AF2EB']
def linear(code):
    rgb=[int(code[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb)+(1,)
def single(o):
    if bpy.context.object and bpy.context.object.mode!='OBJECT': bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
def bounds(parts):
    vs=[o.matrix_world@v.co for o in parts for v in o.data.vertices]
    return Vector([min(v[i] for v in vs) for i in range(3)]),Vector([max(v[i] for v in vs) for i in range(3)])
def tris(o): return sum(len(p.vertices)-2 for p in o.data.polygons)
def cel(i):
    name='ShipMatch_'+str(i);m=bpy.data.materials.get(name)
    if m:return m
    m=bpy.data.materials.new(name);m.diffuse_color=linear(PALETTE[i]);m.use_nodes=True
    n,l=m.node_tree.nodes,m.node_tree.links;n.clear()
    out=n.new('ShaderNodeOutputMaterial');em=n.new('ShaderNodeEmission');l.new(em.outputs[0],out.inputs[0])
    if i==5:em.inputs[0].default_value=m.diffuse_color;return m
    diffuse=n.new('ShaderNodeBsdfDiffuse');diffuse.inputs[0].default_value=(1,1,1,1)
    rgb=n.new('ShaderNodeShaderToRGB');l.new(diffuse.outputs[0],rgb.inputs[0])
    ramp=n.new('ShaderNodeValToRGB');ramp.color_ramp.interpolation='CONSTANT';ramp.color_ramp.elements.remove(ramp.color_ramp.elements[1])
    for j,(pos,color) in enumerate([(0,(.38,.47,.59,1)),(.26,(.70,.78,.87,1)),(.56,(1,1,1,1))]):
        e=ramp.color_ramp.elements[0] if j==0 else ramp.color_ramp.elements.new(pos);e.position=pos;e.color=color
    l.new(rgb.outputs[0],ramp.inputs[0]);mix=n.new('ShaderNodeMixRGB');mix.blend_type='MULTIPLY';mix.inputs[0].default_value=1;mix.inputs[1].default_value=m.diffuse_color
    l.new(ramp.outputs[0],mix.inputs[2]);l.new(mix.outputs[0],em.inputs[0]);return m
def set_slots(o,paint=0):
    o.data.materials.clear()
    for i in range(6):o.data.materials.append(cel(i))
    for p in o.data.polygons:p.material_index=paint;p.use_smooth=False
def normals(o):
    bm=bmesh.new();bm.from_mesh(o.data)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.00003)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free();o.data.update()
def finish_normals(o):
    single(o)
    for p in o.data.polygons:p.use_smooth=True
    o.data.set_sharp_from_angle(angle=math.radians(42))
    mod=o.modifiers.new('Area weighted panel normals','WEIGHTED_NORMAL');mod.keep_sharp=True;mod.weight=75
    bpy.ops.object.modifier_apply(modifier=mod.name)
def simplify_plate(o):
    """Remove embossed dents on the named solid armor shells, preserving extent."""
    bm=bmesh.new();bm.from_mesh(o.data)
    result=bmesh.ops.convex_hull(bm,input=list(bm.verts),use_existing_faces=False)
    unused=list(set(result.get('geom_interior',[])+result.get('geom_unused',[])))
    if unused:bmesh.ops.delete(bm,geom=unused,context='VERTS')
    bmesh.ops.dissolve_limit(bm,angle_limit=math.radians(7),use_dissolve_boundaries=False,verts=list(bm.verts),edges=list(bm.edges))
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free();o.data.update()
def mirror_positive(parts):
    result=[]
    for o in parts:
        lo,hi=bounds([o])
        if hi.y<.0001:bpy.data.objects.remove(o,do_unlink=True);continue
        if lo.y<-.0001:
            bm=bmesh.new();bm.from_mesh(o.data)
            bmesh.ops.bisect_plane(bm,geom=list(bm.verts)+list(bm.edges)+list(bm.faces),dist=.00001,plane_co=(0,0,0),plane_no=(0,1,0),clear_inner=True,clear_outer=False)
            bm.to_mesh(o.data);bm.free()
        if not o.data.polygons:bpy.data.objects.remove(o,do_unlink=True);continue
        normals(o);result.append(o)
        other=o.copy();other.data=o.data.copy();bpy.context.collection.objects.link(other)
        other.name=o.name+'_Mirror';other.data.transform(Matrix.Diagonal((1,-1,1,1)))
        bm=bmesh.new();bm.from_mesh(other.data);bmesh.ops.reverse_faces(bm,faces=list(bm.faces));bm.to_mesh(other.data);bm.free();other.data.update()
        motion=o.get('motion','Body')
        other['motion']=motion.replace('_FL','_FR').replace('_RL','_RR')
        result.append(other)
    return result
def box(name,loc,dim,color=0,bevel=.04,motion='Body',rot=0):
    bpy.ops.mesh.primitive_cube_add(size=1,location=loc);o=bpy.context.object;o.name=name;o.dimensions=dim;o.rotation_euler.y=rot
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
    if bevel:
        mod=o.modifiers.new('Consistent armor chamfer','BEVEL');mod.width=bevel;mod.segments=1
        bpy.ops.object.modifier_apply(modifier=mod.name)
    set_slots(o,color);o['motion']=motion;return o
def cylinder(name,loc,radius,length,color=2,axis='X',vertices=12,motion='Body'):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices,radius=radius,depth=length,location=loc)
    o=bpy.context.object;o.name=name
    if axis=='X':o.rotation_euler.y=math.pi/2
    elif axis=='Y':o.rotation_euler.x=math.pi/2
    bpy.ops.object.transform_apply(location=True,rotation=True,scale=True);set_slots(o,color);o['motion']=motion;return o
def repaint(o):
    set_slots(o);name=o.name;lo,hi=bounds([o]);center=(lo+hi)*.5
    for p in o.data.polygons:
        c=p.center;n=p.normal;idx=0
        if o.get('motion','').startswith('Wheel'):
            radial=math.hypot(c.x-center.x,c.z-center.z)/(hi.z-lo.z)*2
            idx=2 if abs(n.y)<.75 else 1 if radial<.5 else 0
        elif 'Fan_' in name:
            radial=math.hypot((c.x-center.x)/((hi.x-lo.x)*.5),(c.y-center.y)/((hi.y-lo.y)*.5))
            idx=0 if radial<.72 and n.z>.2 else 1 if n.z>.2 else 2
        elif 'Cannon' in name:idx=2 if c.x>.25 else 0
        elif 'Thruster' in name:idx=2
        elif 'Back_Flat' in name:idx=1
        elif 'Headlight' in name:idx=4 if n.x>.5 else 0
        elif 'Body' in name or 'StaticMeshComponent0' in name:
            idx=1 if abs(c.y)>1.2 else 0
            if n.z<-.5:idx=2
        elif 'Back_Base' in name:idx=0
        elif 'Cockpit_Aero' in name:idx=1
        p.material_index=idx
def model(unit):
    bpy.ops.wm.open_mainfile(filepath=str(ROOT/(unit+'_assembly.blend')))
    parts=[o for o in bpy.data.objects if o.type=='MESH']
    for o in list(parts):
        if 'Antenna' in o.name:
            parts.remove(o);bpy.data.objects.remove(o,do_unlink=True)
    parts=mirror_positive(parts)
    if unit=='WarMachine':
        for o in parts:
            if any(s in o.name for s in ('Back_Base_Cobra','Cockpit_Aero_Blunt','Cockpit_Nose_Sting')):simplify_plate(o)
    for o in parts:repaint(o)
    sockets={}
    if unit=='Sweeper':
        pivot=Vector((.9,0,2.28));muzzle=Vector((3.02,0,2.28))
        parts.extend([box('Gun pedestal',(.7,0,1.75),(.8,.65,.42),1,.05),cylinder('Pitch axle',pivot,.25,.68,2,'Y'),
          box('Gun receiver',(1.08,0,2.28),(1.22,.6,.64),0,.09,'Gun_Pitch'),
          box('Gun dorsal panel',(1.05,0,2.62),(.83,.43,.035),1,.012,'Gun_Pitch'),
          cylinder('Single machine gun barrel',(2.1,0,2.28),.115,1.26,2,'X',12,'Gun_Pitch'),
          cylinder('Muzzle brake',(2.92,0,2.28),.19,.25,0,'X',8,'Gun_Pitch'),
          cylinder('Muzzle bore',(3.052,0,2.28),.115,.018,3,'X',8,'Gun_Pitch'),
          box('Gun amber identification',(1.47,0,2.60),(.13,.49,.025),4,.004,'Gun_Pitch'),
          cylinder('Centered short aerial',(-1.2,0,3.44),.045,.9,2,'Z',6)])
        sockets={'Rig_GunPitch':list(pivot),'Muzzle_Gun':list(muzzle)}
        for side in (-1,1):
            parts.append(box('Rear safety band '+str(side),(-.77,side*1.29,2.86),(.17,.28,.06),4,.015))
    else:
        angle=-math.pi/4;axis=Vector((math.sqrt(.5),0,math.sqrt(.5)))
        for side in (-1,1):
            center=Vector((-1.55,side*.79,3.5));length=1.65
            parts.append(box('Rear missile pod '+str(side),center,(length,.68,.74),0,.07,rot=angle))
            parts.append(box('Pod blue collar '+str(side),center-axis*.45,(.24,.715,.775),1,.04,rot=angle))
            parts.append(box('Pod mount '+str(side),(-1.72,side*.79,2.94),(.8,.43,.57),2,.03))
            front=center+axis*(length*.5+.005)
            parts.append(box('Pod recessed mouth '+str(side),front,(.025,.53,.57),3,0,rot=angle))
            # Four visible tubes, with two authored gameplay launch origins overall.
            across=Vector((0,1,0));up=Vector((-math.sqrt(.5),0,math.sqrt(.5)))
            for a in (-1,1):
                for b in (-1,1):
                    q=front+across*(a*.143)+up*(b*.155)+axis*.018
                    tube=cylinder('Missile nose',q,.094,.035,4,'Z',8)
                    # Existing cylinder is world vertical; rotate about its own centre.
                    tube.data.transform(Matrix.Translation(q)@Matrix.Rotation(math.pi/4,4,'Y')@Matrix.Translation(-q));parts.append(tube)
            sockets['Muzzle_Missile_'+('L' if side>0 else 'R')]=list(front+axis*.12)
        sockets['Muzzle_Cannon_L']=[1.84,1.65,3.09];sockets['Muzzle_Cannon_R']=[1.84,-1.65,3.09]
    # Normalize only after weapons and supports are included; no nonuniform scaling.
    lo,hi=bounds(parts);longest=max(hi.x-lo.x,hi.y-lo.y)
    target=15.10524 if unit=='Sweeper' else 57.61493
    scale=target/longest;offset=Vector((-(lo.x+hi.x)*.5,0,-lo.z))
    for o in parts:
        o.data.transform(Matrix.Scale(scale,4)@Matrix.Translation(offset));o.data.update()
        finish_normals(o)
    sockets={k:list((Vector(v)+offset)*scale) for k,v in sockets.items()}
    wheel_pivots={}
    for o in parts:
        tag=o.get('motion','Body')
        if tag.startswith('Wheel'):
            a,b=bounds([o]);wheel_pivots[tag]=list((a+b)*.5)
    wheel_radius=(bounds([next(o for o in parts if o.get('motion')=='Wheel_FL')])[1].z-bounds([next(o for o in parts if o.get('motion')=='Wheel_FL')])[0].z)*.5 if unit=='Sweeper' else 0
    return parts,dict(unit=unit,display_name='扫荡者' if unit=='Sweeper' else '战争机器',scale=scale,sockets_m=sockets,wheel_pivots_m=wheel_pivots,wheel_radius_m=wheel_radius,palette=PALETTE)
def preview(parts,unit,view='three_quarter'):
    scene=bpy.context.scene
    for o in list(scene.objects):
        if o.type in {'LIGHT','CAMERA'}:bpy.data.objects.remove(o,do_unlink=True)
    scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1400;scene.render.resolution_y=1100;scene.render.resolution_percentage=100
    scene.view_settings.view_transform='Standard';scene.view_settings.look='None'
    if not scene.world:scene.world=bpy.data.worlds.new('Review World')
    scene.world.use_nodes=True;bg=next(n for n in scene.world.node_tree.nodes if n.type=='BACKGROUND');bg.inputs[0].default_value=(.36,.43,.50,1);bg.inputs[1].default_value=.5
    lo,hi=bounds(parts);center=(lo+hi)*.5;span=max(hi-lo)
    direction={'three_quarter':(1.2,-1.55,.92),'front':(1,0,.15),'top':(0,0,1),'side':(0,-1,.1)}[view]
    data=bpy.data.cameras.new('Review camera');cam=bpy.data.objects.new('Review camera',data);scene.collection.objects.link(cam);cam.location=center+Vector(direction)*span;cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler();data.type='ORTHO';data.ortho_scale=span*1.3;data.clip_end=10000;scene.camera=cam
    for name,loc,energy in [('Key',(1,-2,4),2.5),('Fill',(-3,2,2),1.4)]:
        data=bpy.data.lights.new(name,'SUN');o=bpy.data.objects.new(name,data);scene.collection.objects.link(o);o.rotation_euler=Vector(loc).to_track_quat('Z','Y').to_euler();data.energy=energy;data.use_shadow=False
    scene.render.image_settings.file_format='PNG';scene.render.filepath=str(OUT/(unit+'_'+view+'.png'));bpy.ops.render.render(write_still=True)
def symmetry(parts):
    from mathutils.kdtree import KDTree
    vs=[v.co for o in parts for v in o.data.vertices];kd=KDTree(len(vs))
    for i,v in enumerate(vs):kd.insert(v,i)
    kd.balance();return max(kd.find(Vector((v.x,-v.y,v.z)))[2] for v in vs)
if __name__=='__main__':
    for unit in ('Sweeper','WarMachine'):
        parts,report=model(unit)
        report['triangles']=sum(tris(o) for o in parts);report['bounds_m']=[list(v) for v in bounds(parts)];report['symmetry_error_m']=symmetry(parts)
        assert report['symmetry_error_m']<.002,report['symmetry_error_m']
        assert report['triangles']<=(4000 if unit=='Sweeper' else 6000)
        for view in ('three_quarter','front','top'):preview(parts,unit,view)
        (OUT/(unit+'_production.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
        bpy.ops.wm.save_as_mainfile(filepath=str(OUT/(unit+'_Editable.blend')))
        print('TACTICAL_SHIP_STYLE_READY',json.dumps(report,ensure_ascii=False),flush=True)
