import bpy,bmesh,math,json
from mathutils import Vector,Matrix,Quaternion
import mech_production_common as m
helper=open('D:/UE5.7/test1/Scripts/Blender/style_mech_source_models.py',encoding='utf-8').read().split("bpy.context.window.scene=bpy.data.scenes['Mechs_Production_v1']")[0]
exec(compile(helper,'mech_style_helpers','exec'))
m.visible(['Cockpit_Jet'])
o=m.meshes('Cockpit_Jet')[0]
bm=bmesh.new();bm.from_mesh(o.data);remaining=set(bm.verts);removed=[];doomed=[]
while remaining:
    seed=remaining.pop();stack=[seed];part=[seed]
    while stack:
        v=stack.pop()
        for edge in v.link_edges:
            w=edge.other_vert(v)
            if w in remaining:remaining.remove(w);stack.append(w);part.append(w)
    lo=[min(v.co[i] for v in part) for i in range(3)];hi=[max(v.co[i] for v in part) for i in range(3)]
    if len(part)<20 and max(abs(lo[0]),abs(hi[0]))<34 and hi[1]<0 and lo[2]>10:
        doomed.extend(part);removed.append({'vertices':len(part),'min_cm':lo,'max_cm':hi})
bmesh.ops.delete(bm,geom=doomed,context='VERTS');bm.to_mesh(o.data);bm.free();o.data.update()
o['driver_and_open_cockpit_removed']=True

def plainmat(name,h,rough=.55,metal=.2):
    mat=bpy.data.materials.new(name);mat.use_nodes=True;p=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED');p.inputs['Base Color'].default_value=color(h);p.inputs['Roughness'].default_value=rough;p.inputs['Metallic'].default_value=metal;mat.diffuse_color=color(h);return mat
gold=plainmat('LightMech_ClosedArmor_Gold','B48A3F');edge=plainmat('LightMech_Armor_Seams','2C3737')
sections=[(-113,19,34,26),(-81,27,64,35),(-45,30,88,52),(-17,15,103,75),(18,24,92,67),(52,23,82,64)]
verts=[]
for y,w,top,bottom in sections:
    c=3.0
    verts.extend([(-w+c,y,bottom),(w-c,y,bottom),(w,y,bottom+c),(w,y,top-c),(w-c,y,top),(-w+c,y,top),(-w,y,top-c),(-w,y,bottom+c)])
faces=[tuple(reversed(range(8)))]
for j in range(len(sections)-1):
    for i in range(8):faces.append((j*8+i,j*8+(i+1)%8,(j+1)*8+(i+1)%8,(j+1)*8+i))
faces.append(tuple((len(sections)-1)*8+i for i in range(8)))
mesh=bpy.data.meshes.new('Cockpit_Sealed_Fairing');mesh.from_pydata(verts,[],faces);mesh.update()
cover=bpy.data.objects.new('WORK_Cockpit_Jet__Sealed_Armor_Fairing',mesh);bpy.data.collections['WORK_Cockpit_Jet'].objects.link(cover);cover.matrix_world=o.matrix_world.copy();cover.data.materials.append(gold)
cover['source_basis']='Fits original cockpit rim; replaces pilot and open cabin with continuous closed armor'
bevel(cover,.5)
# Narrow top panel seams sit on the solid underlying fairing.
for idx in (1,2,3,4):
    y,w,z,bottom=sections[idx]
    bpy.ops.mesh.primitive_cube_add(size=1)
    seam=bpy.context.object;seam.name=f'WORK_Cockpit_Jet__ArmorSeam_{idx}'
    for col in list(seam.users_collection):col.objects.unlink(seam)
    bpy.data.collections['WORK_Cockpit_Jet'].objects.link(seam)
    seam.data.transform(Matrix.Diagonal((2*(w-3),.45,.35,1)))
    seam.matrix_world=o.matrix_world@Matrix.Translation((0,y,z+.05));seam.data.materials.append(edge)

mapkeys={'Mech_Legs_Lt':'Mech_Legs_Lt','Cockpit_Jet':'Cockpit_Jet','HalfShoulder_Box':'HalfShoulder_Box','Weapons_Machinegun_lvl1':'Machinegun_lvl1'}
for source in [True,False]:
    prefix='SRC_' if source else 'WORK_';parent=bpy.data.collections['00_SOURCE_READONLY' if source else '10_WORKING_MODELS']
    collection=bpy.data.collections.new(prefix+'Mech_Lightest');parent.children.link(collection)
    for comp in m.SNAP['assemblies']['Mech_Lightest']:
        key=mapkeys[comp['mesh'].split('.')[-1]];mapping={}
        for old in m.objects(key,source):
            new=old.copy();new.name=prefix+'Mech_Lightest__'+old.name.split('__',1)[-1]
            if old.data:new.data=old.data.copy()
            collection.objects.link(new);mapping[old]=new
        t=comp['world_transform'];loc=t['location'];q=t['rotation'];basis=Matrix.Diagonal((1,-1,1,1));rot=Quaternion((q[3],q[0],q[1],q[2])).to_matrix().to_4x4()
        transform=Matrix.Translation((loc[0]*.01,-loc[1]*.01,loc[2]*.01))@(basis@rot@basis)@Matrix.Diagonal((*t['scale'],1))
        for old,new in mapping.items():
            if old.parent in mapping:new.parent=mapping[old.parent]
            else:new.matrix_world=transform@old.matrix_world
            for mod in new.modifiers:
                if mod.type=='ARMATURE' and mod.object in mapping:mod.object=mapping[mod.object]
            new['source_component']=comp['name'];new['attachment_socket']=comp['socket']
    collection['source_blueprint']='/Game/Assets/MechaController/Mech_Constructor_Lt_Med/Blueprints/Mech_Lightest_Blueprint'
(m.OUT/'light_closed_cockpit_report.json').write_text(json.dumps({'removed_interior_components':removed,'cover_sections_cm':sections,'original_assembly_preserved':True,'driver_visible':False,'open_cockpit_visible':False},indent=2),encoding='utf-8')
m.visible(['Mech_Lightest']);m.save()
result={'removed_interior_vertices':len(doomed),'closed_cover':cover.name,'assembly_objects':len(m.objects('Mech_Lightest'))}
