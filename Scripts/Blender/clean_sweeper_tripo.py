"""Mechanical cleanup of the accepted Tripo draft, preserving its silhouette.

The raw GLB is immutable. Color simplification is material authoring, not a
change to the approved concept images. Cylindrical moving parts are rebuilt
coaxially; the source body and gun are mirrored and retained.
"""
import bpy,bmesh,math,json,sys
from pathlib import Path
from mathutils import Matrix,Vector
import numpy as np
sys.path.insert(0,str(Path(__file__).parent))
import finish_tactical_ship_style as common
ROOT=Path('D:/UE5.7/test1/ArtSource/TacticalStyle_20260916')
OUT=ROOT/'Production_AI';OUT.mkdir(exist_ok=True)
common.OUT=OUT
common.PALETTE=['EC5D2D','F1E9D7','53697A','243544','FFC65C','8195A0']
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(ROOT/'Tripo/Sweeper_Multiview_Raw.glb'))
obj=next(o for o in bpy.context.scene.objects if o.type=='MESH')
obj.data.transform(Matrix.Rotation(math.pi,4,'Z')@obj.matrix_world);obj.matrix_world=Matrix.Identity(4)
image=next(i for i in bpy.data.images if i.name.startswith('Color_'))
pixels=np.array(image.pixels[:],dtype=np.float32).reshape(image.size[1],image.size[0],4)
r,g,b=pixels[...,0],pixels[...,1],pixels[...,2]
classes=np.full(r.shape,2,dtype=np.uint8)
classes[(r>.23)&(r>g*.9)&(r>b*.95)]=1
classes[(r>g*1.45)&(r>b*1.7)&(r>.12)]=0
classes[(r>.5)&(g>r*.6)&(b<r*.5)]=4
# Majority filtering removes generated speckle and hairline scratches. Clean
# structural ink is baked separately from real geometry later in production.
for iteration in range(3):
    votes=[]
    for cls in range(5):
        v=(classes==cls).astype(np.uint8)
        votes.append(sum(np.roll(np.roll(v,dx,1),dy,0) for dx,dy in [(0,0),(1,0),(-1,0),(0,1),(0,-1)]))
    classes=np.argmax(votes,axis=0).astype(np.uint8)
palette=np.array([common.linear(c) for c in common.PALETTE],dtype=np.float32)
paint=bpy.data.images.new('Sweeper_Clean_Paint_Source',width=image.size[0],height=image.size[1],alpha=True)
paint.pixels.foreach_set(palette[classes].ravel());paint.filepath_raw=str(OUT/'Sweeper_Clean_Paint_Source.png');paint.file_format='PNG';paint.save()
mat=obj.data.materials[0];nodes,links=mat.node_tree.nodes,mat.node_tree.links;nodes.clear()
out=nodes.new('ShaderNodeOutputMaterial');em=nodes.new('ShaderNodeEmission');tex=nodes.new('ShaderNodeTexImage');tex.image=paint
uv=nodes.new('ShaderNodeUVMap');uv.uv_map='UVMap';links.new(uv.outputs[0],tex.inputs[0]);links.new(tex.outputs[0],em.inputs[0]);links.new(em.outputs[0],out.inputs[0])
common.single(obj);bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT');bpy.ops.mesh.remove_doubles(threshold=.00005);bpy.ops.mesh.separate(type='LOOSE');bpy.ops.object.mode_set(mode='OBJECT')
parts=[];removed=[];fenders=[]
def plate(name,profile,inner_y,outer_y):
    verts=[(x,y,z) for y in (inner_y,outer_y) for x,z in profile];n=len(profile)
    faces=[tuple(reversed(range(n))),tuple(range(n,2*n))]+[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)]
    me=bpy.data.meshes.new(name);me.from_pydata(verts,[],faces);me.update();o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o)
    o['motion']='Body';o['paint_override']=1;common.normals(o);common.set_slots(o,0);common.single(o)
    mod=o.modifiers.new('Straight armor edge chamfer','BEVEL');mod.width=.005;mod.segments=1;bpy.ops.object.modifier_apply(modifier=mod.name)
    return o
for o in list(bpy.context.scene.objects):
    if o.type!='MESH':continue
    lo,hi=common.bounds([o]);center=(lo+hi)*.5;dim=hi-lo
    wheel=abs(center.y)>.205 and hi.z<-.05 and (center.x>.15 or center.x<-.28)
    aerial=dim.z>.17 and dim.x<.03 and dim.y<.03
    main_shell=dim.x>.8 and dim.z>.3
    gun_receiver=lo.x>-.07 and hi.x<.24 and dim.x>.24 and lo.z>-.03 and hi.z>.10
    barrel=lo.x>.19 and dim.x>.25 and dim.y<.07 and lo.z>0
    fender=.10<abs(center.y)<.23 and dim.x>.15 and dim.y>.10 and hi.z<-.07
    if fender:fenders.append((Vector(center),Vector(dim)))
    if wheel or aerial or main_shell or gun_receiver or barrel or fender:
        removed.append(o.name);bpy.data.objects.remove(o,do_unlink=True);continue
    o['motion']='Gun_Pitch' if hi.x>0 and lo.z>-.035 and hi.z<.13 and dim.y<.22 else 'Body'
    o.name='Sweeper_Tripo_'+o['motion']
    common.single(o)
    if common.tris(o)>100:
        mod=o.modifiers.new('Remove subpixel surface tessellation','DECIMATE');mod.ratio=.76
        bpy.ops.object.modifier_apply(modifier=mod.name)
    parts.append(o)
parts.extend([
    plate('Retopologized side armor',[(-.285,.130),(-.225,.160),(-.175,.152),(.128,-.042),(.157,-.103),(.077,-.154),(-.091,-.171),(-.215,-.099)],.135,.215),
    plate('Retopologized rear deck',[(-.330,-.080),(-.305,.110),(-.174,.160),(-.059,.078),(-.018,-.065)],-.101,.101),
    plate('Retopologized front hood',[(-.025,-.220),(.430,-.220),(.450,-.170),(.350,-.100),(.170,-.068),(.030,-.095)],-.076,.076),
    common.box('Gun receiver clean',(.076,0,.05),(.266,.166,.12),0,.017,'Gun_Pitch'),
])
parts[-1]['paint_override']=2
for center,dim in fenders:
    o=common.box('Retopologized axle armor',center,dim,0,.010);o['paint_override']=5;parts.append(o)
for name,loc,dim,bevel,col in [('Straight gun barrel',(.345,0,.034),(.272,.032,.032),.004,2),('Muzzle brake',(.480,0,.034),(.050,.056,.050),.007,2),('Recessed muzzle',(.506,0,.034),(.002,.026,.023),.002,3)]:
    o=common.box(name,loc,dim,col,bevel,'Gun_Pitch');o['paint_override']=100+col;parts.append(o)
parts=common.mirror_positive(parts)
def lathe(name,center,radius,width,tag):
    # A regular 16-sided tire and stepped ivory hub; every ring is coaxial.
    ring=[(-.50,.64),(-.43,.91),(-.31,1),(.31,1),(.43,.91),(.50,.64),(.51,.46),(.53,.40)]
    verts=[];segments=12
    for y,rad in ring:
        for i in range(segments):
            a=2*math.pi*i/segments;verts.append((center[0]+math.cos(a)*radius*rad,center[1]+y*width,center[2]+math.sin(a)*radius*rad))
    faces=[];colors=[]
    for j in range(len(ring)-1):
        for i in range(segments):
            k=(i+1)%segments;faces.append((j*segments+i,j*segments+k,(j+1)*segments+k,(j+1)*segments+i));colors.append(1 if j==5 else 2)
    faces.extend([tuple(reversed(range(segments))),tuple((len(ring)-1)*segments+i for i in range(segments))]);colors.extend([2,2])
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(verts,[],faces);mesh.update();o=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(o);common.set_slots(o,2)
    for p,col in zip(o.data.polygons,colors):p.material_index=col
    o['motion']=tag;common.normals(o);return o
wheel_pivots={};ground=-.337
for axle,x,radius,side_y,width in [('F',.292,.111,.324,.143),('R',-.389,.125,.335,.153)]:
    for side in (-1,1):
        tag='Wheel_'+axle+('L' if side>0 else 'R');center=(x,side*side_y,ground+radius)
        o=lathe(tag,center,radius,width,tag)
        if side<0:
            # Reverse the ring so both visible outward hubs have identical paint.
            o.data.transform(Matrix.Translation(center)@Matrix.Diagonal((1,-1,1,1))@Matrix.Translation(-Vector(center)));common.normals(o)
        parts.append(o);wheel_pivots[tag]=center
        for dx in (-.054,.054):
            slot=common.box('Wheel hub mechanical notch',(center[0]+dx,center[1]+side*(width*.52+.001),center[2]),(.012,.003,.024),2,.001,tag);slot['paint_override']=102;parts.append(slot)
parts.append(common.cylinder('Centered aerial',(-.208,0,.238),.008,.198,2,'Z',8))
lo,hi=common.bounds(parts);scale=15.10524/max(hi.x-lo.x,hi.y-lo.y);offset=Vector((-(lo.x+hi.x)*.5,0,-lo.z))
for o in parts:
    o.data.transform(Matrix.Scale(scale,4)@Matrix.Translation(offset));o.data.update();common.finish_normals(o)
report={'unit':'Sweeper','display_name':'扫荡者','source_task':'837deaa2-03e7-49c4-a54a-37b12ac219e9','scale':scale,'source_offset':list(offset),'bounds_m':[list(v) for v in common.bounds(parts)],'triangles':sum(common.tris(o) for o in parts),'symmetry_error_m':common.symmetry(parts),'wheel_pivots_m':{k:list((Vector(v)+offset)*scale) for k,v in wheel_pivots.items()},'wheel_radius_m':.111*scale,'sockets_m':{'Rig_GunPitch':list((Vector((.047,0,.034))+offset)*scale),'Muzzle_Gun':list((Vector((.507,0,.034))+offset)*scale)}}
assert report['symmetry_error_m']<.001,report['symmetry_error_m']
for view in ('three_quarter','front','top'):common.preview(parts,'Sweeper',view)
(OUT/'Sweeper_clean_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Sweeper_CleanParts.blend'))
print('SWEEPER_CLEAN_READY',json.dumps(report),flush=True)
