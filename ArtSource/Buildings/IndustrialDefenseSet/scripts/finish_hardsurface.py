"""Compile native V3 hard-surface assemblies, bake palette atlases, bind and export.
Input: hardsurface_work/ThreeModels_Construction.blend. Editable parts stay separate.
"""
import bpy,bmesh,math,json,sys
import numpy as np
from pathlib import Path
from mathutils import Vector,Matrix
sys.path.insert(0,str(Path(__file__).parent))
from hardsurface_common import OUT,WORK,ROOT,apply_parts,join_parts,uv_unwrap
from blender_common import activate,describe,bounds,setup_studio,render,aim,pack_save,material,new_scene
import finish_assets as finish
finish.OUT=OUT
SIZE=4096
NAMES=['RedOreRefinery','ShieldGenerator','HeavyDefenseCannon']
REPORT={'revision':'V3 - rebuilt planar hard-surface geometry',
 'reference':'/Game/GuLiStrike/Buildings/ResourceProcessingFactory/Blueprints/BP_ResourceProcessingFactory',
 'assets':{},'validation':{}}

def img(role,name,space='Non-Color',color=(0,0,0,1)):
    im=bpy.data.images.new('T_'+name+'_'+role,width=SIZE,height=SIZE,alpha=False,float_buffer=False)
    im.colorspace_settings.name=space;im.generated_color=color
    return im

def save_image(im,folder):
    im.filepath_raw=str(folder/(im.name+'.png'));im.file_format='PNG';im.save();im.pack()

def bake_palette(name,objects):
    source=bpy.context.window.scene
    scene=bpy.data.scenes.new('BAKE_'+name);bpy.context.window.scene=scene
    scene.render.engine='CYCLES';scene.cycles.samples=1;scene.cycles.device='GPU'
    pref=bpy.context.preferences.addons['cycles'].preferences
    pref.compute_device_type='OPTIX';pref.get_devices()
    for d in pref.devices:d.use=d.type=='OPTIX'
    copies={};proxies=[];nodes=[]
    for ob in objects:
        mesh=ob.data.copy()
        for i,original in enumerate(ob.data.materials):
            if original.name not in copies:
                mat=original.copy();mat.name='BAKE_'+original.name
                ns=mat.node_tree.nodes;ls=mat.node_tree.links
                bs=next(n for n in ns if n.type=='BSDF_PRINCIPLED')
                base=tuple(bs.inputs['Base Color'].default_value)
                emission=tuple(bs.inputs['Emission Color'].default_value)
                strength=bs.inputs['Emission Strength'].default_value
                values={'BaseColor':base,
                        'ORM':(1,bs.inputs['Roughness'].default_value,bs.inputs['Metallic'].default_value,1),
                        'Emissive':tuple(emission[j]*strength for j in range(3))+(1,)}
                out=next(n for n in ns if n.type=='OUTPUT_MATERIAL')
                em=ns.new('ShaderNodeEmission');em.inputs['Strength'].default_value=1
                ls.new(em.outputs[0],out.inputs['Surface'])
                tex=ns.new('ShaderNodeTexImage');ns.active=tex;tex.select=True
                nodes.append((mat,tex,em,values));copies[original.name]=mat
            mesh.materials[i]=copies[original.name]
        proxy=bpy.data.objects.new('BAKE_'+ob.name,mesh);scene.collection.objects.link(proxy);proxies.append(proxy)
    folder=OUT/name/'Textures';folder.mkdir(parents=True,exist_ok=True);maps={}
    for role in ['BaseColor','ORM','Emissive']:
        im=img(role,name,'sRGB' if role!='ORM' else 'Non-Color')
        for mat,tex,em,values in nodes:
            tex.image=im;mat.node_tree.nodes.active=tex;em.inputs['Color'].default_value=values[role]
        activate(proxies)
        bpy.ops.object.bake(type='EMIT',margin=6,margin_type='EXTEND',use_clear=True)
        save_image(im,folder);maps[role]=im
        print('HARD_SURFACE_BAKED '+name+' '+role,flush=True)
    for role in ['NormalGL','NormalDX']:
        im=img(role,name,color=(.5,.5,1,1));save_image(im,folder);maps[role]=im
    p=np.empty(len(maps['ORM'].pixels),np.float32);maps['ORM'].pixels.foreach_get(p);p=p.reshape(-1,4)
    for ch,role in [(1,'Roughness'),(2,'Metallic')]:
        im=img(role,name);a=np.ones_like(p);a[:,:3]=p[:,ch,None]
        im.pixels.foreach_set(a.ravel());im.update();save_image(im,folder);maps[role]=im
        del a
    del p
    bpy.context.window.scene=source
    for ob in proxies:
        mesh=ob.data;bpy.data.objects.remove(ob,do_unlink=True);bpy.data.meshes.remove(mesh)
    for mat in copies.values():bpy.data.materials.remove(mat)
    bpy.data.scenes.remove(scene)
    return maps

def portable_material(name,maps):
    mat=bpy.data.materials.new('M_'+name+'_HardSurface');mat.use_nodes=True
    ns=mat.node_tree.nodes;ls=mat.node_tree.links
    bs=next(n for n in ns if n.type=='BSDF_PRINCIPLED')
    for role,y in [('BaseColor',300),('ORM',0),('NormalGL',-300),('Emissive',-600)]:
        n=ns.new('ShaderNodeTexImage');n.image=maps[role];n.label=role;n.location=(-650,y)
        if role=='BaseColor':ls.new(n.outputs['Color'],bs.inputs['Base Color'])
        elif role=='ORM':
            sep=ns.new('ShaderNodeSeparateColor');sep.mode='RGB';sep.location=(-370,0)
            ls.new(n.outputs['Color'],sep.inputs[0]);ls.new(sep.outputs['Green'],bs.inputs['Roughness']);ls.new(sep.outputs['Blue'],bs.inputs['Metallic'])
        elif role=='NormalGL':
            normal=ns.new('ShaderNodeNormalMap');normal.location=(-340,-300)
            ls.new(n.outputs['Color'],normal.inputs['Color']);ls.new(normal.outputs[0],bs.inputs['Normal'])
        else:
            ls.new(n.outputs['Color'],bs.inputs['Emission Color']);bs.inputs['Emission Strength'].default_value=1
    mat['Design']='Plain industrial colors; rectangular panels and repeated geometric markings; physical chamfers'
    mat['NativeConstruction']='Construction/'+name+'_EditableParts.blend'
    return mat

def geometry_check(objects):
    result=[]
    for ob in objects:
        bm=bmesh.new();bm.from_mesh(ob.data)
        stats={'object':ob.name,'boundary_edges':sum(e.is_boundary for e in bm.edges),
               'nonmanifold_edges':sum(not e.is_manifold for e in bm.edges),
               'zero_area_faces':sum(f.calc_area()<1e-12 for f in bm.faces)}
        bm.free()
        uv=ob.data.uv_layers.active
        stats['uv_loops']=len(uv.data) if uv else 0
        stats['uv_outside_atlas']=sum(any(v < -1e-5 or v > 1.00001 for v in loop.uv) for loop in uv.data) if uv else 1
        stats['passed']=all(stats[k]==0 for k in ['boundary_edges','nonmanifold_edges','zero_area_faces','uv_outside_atlas']) and bool(uv)
        result.append(stats)
    return {'objects':result,'passed':all(x['passed'] for x in result)}

def controls(scene,rig):
    collection=bpy.data.collections.new('Rig_ControlShapes_NoExport');scene.collection.children.link(collection)
    for label,bone,radius,axis in [('CTRL_BaseYaw','base_yaw',1.9,'Z'),('CTRL_BarrelPitch','barrel_pitch',.9,'Y')]:
        points=[]
        for i in range(64):
            a=math.tau*i/64
            points.append((radius*math.cos(a),radius*math.sin(a),0) if axis=='Z' else (radius*math.cos(a),0,radius*math.sin(a)))
        mesh=bpy.data.meshes.new(label);mesh.from_pydata(points,[(i,(i+1)%64) for i in range(64)],[]);mesh.update()
        ob=bpy.data.objects.new(label,mesh);collection.objects.link(ob);ob.hide_render=True;ob.hide_set(True);ob.display_type='WIRE'
        rig.pose.bones[bone].custom_shape=ob;rig.pose.bones[bone].use_custom_shape_bone_size=False

def gallery(completed):
    scene=new_scene('00_AssetGallery');objects=[]
    for (source,meshes,rig),offset,scale,angle in zip(completed,[-5.7,0,5.7],[.40,.72,.64],[-math.pi/6,-.65,-2.5]):
        lo,hi=bounds(meshes);center=Vector(((lo.x+hi.x)/2,(lo.y+hi.y)/2,lo.z))
        transform=Matrix.Translation((offset,0,0)) @ Matrix.Rotation(angle,4,'Z') @ Matrix.Scale(scale,4) @ Matrix.Translation(-center)
        for original in meshes:
            ob=original.copy();ob.name='Gallery_'+original.name;ob.parent=None;ob.animation_data_clear()
            for mod in list(ob.modifiers):ob.modifiers.remove(mod)
            scene.collection.objects.link(ob);ob.matrix_world=transform;objects.append(ob)
    camera,center,size,stage=setup_studio(scene,objects,res=(2400,1143),samples=64)
    target=Vector((0,0,1.45));camera.location=(0,-24,15);aim(camera,target);bpy.context.view_layer.update()
    right=camera.matrix_world.to_3x3() @ Vector((1,0,0));up=camera.matrix_world.to_3x3() @ Vector((0,1,0))
    toward=camera.matrix_world.to_3x3() @ Vector((0,0,1))
    paper=material('Gallery_Type',(.70,.82,.91),0,.7,1);accent=material('Gallery_Accent',(.10,.66,.67),0,.7,1)
    def label(body,x,y,size,mat):
        data=bpy.data.curves.new('Label','FONT');data.body=body;data.align_x='CENTER';data.align_y='CENTER';data.size=size
        ob=bpy.data.objects.new('Label_'+body,data);stage.objects.link(ob);ob.rotation_euler=camera.rotation_euler
        ob.location=target+right*x+up*y+toward*3;data.materials.append(mat)
    label('GULISTRIKE  /  INDUSTRIAL DEFENSE',0,3.38,.35,paper)
    label('HARD SURFACE REBUILD  /  V3     |     FLAT PLANES  +  PRECISE EDGES',0,2.98,.145,accent)
    for x,title,subtitle in [(-5.7,'01  RED ORE REFINERY','STATIC MESH  /  4K PBR'),(0,'02  SHIELD GENERATOR','STATIC MESH  /  4K PBR'),
                              (5.7,'03  HEAVY DEFENSE CANNON','RIGGED  /  YAW + ELEVATION')]:
        label(title,x,-2.68,.23,paper);label(subtitle,x,-3.08,.14,accent)
    scene['Note']='Presentation scales only. Switch scenes to edit original-size models.'
    activate(objects);render(scene,OUT/'Preview_All.png',scale=18.5)
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.region_3d.view_perspective='CAMERA'
                area.spaces.active.shading.type='MATERIAL';area.spaces.active.overlay.show_overlays=False
    return scene

def main():
    completed=[]
    for name in NAMES:
        scene=bpy.data.scenes[name];bpy.context.window.scene=scene
        parts=[o for o in scene.objects if o.type=='MESH' and 'RigidPart' in o]
        part_count=len(parts);apply_parts(parts)
        if name=='HeavyDefenseCannon':
            buckets={key:[p for p in parts if p['RigidPart']==key] for key in ['root','base_yaw','barrel_pitch']}
            groups={key:join_parts(buckets[key],label) for key,label in [
                ('root','Cannon_FixedFoundation'),('base_yaw','Cannon_RotatingBase'),('barrel_pitch','Cannon_PitchAssembly')]}
            objects=list(groups.values())
        else:objects=[join_parts(parts,'SM_'+name)]
        uv_unwrap(objects)
        check=geometry_check(objects);REPORT['validation'][name+'_geometry']=check
        if not check['passed']:raise RuntimeError('Geometry or UV validation failed: '+json.dumps(check))
        maps=bake_palette(name,objects);mat=portable_material(name,maps)
        for ob in objects:
            ob.data.materials.clear();ob.data.materials.append(mat)
            for poly in ob.data.polygons:poly.material_index=0
            ob['GeometryRevision']='V3 hard-surface rebuild';ob['EditableSource']='Construction/'+name+'_EditableParts.blend'
        rig=None
        if name=='HeavyDefenseCannon':
            rig=finish.make_rig(scene,groups);REPORT['validation']['rig']=finish.validate_rig(rig,groups)
            if not REPORT['validation']['rig']['passed']:raise RuntimeError('Rigid rig verification failed')
        REPORT['assets'][name]={**describe(objects),'construction_parts':part_count,'material':mat.name,'texture_resolution':[SIZE,SIZE],
            'textures':{role:'Textures/'+im.name+'.png' for role,im in maps.items()}}
        scene.cycles.samples=64;scene.render.resolution_x=1800;scene.render.resolution_y=1400
        finish.export_asset(scene,objects,name,rig)
        render(scene,OUT/name/(name+'_Preview.png'))
        if rig:
            REPORT['assets'][name].update({'bones':['root','base_yaw','barrel_pitch'],'pitch_limits_deg':[-10,40],'yaw':'continuous 360 degrees'})
            finish.pose(rig,35,35);render(scene,OUT/name/(name+'_Aimed.png'));finish.pose(rig)
            finish.create_demo(scene,rig);finish.export_asset(scene,objects,name,rig,True);controls(scene,rig)
        activate(([rig] if rig else [])+objects)
        filename='HeavyDefenseCannon_Rigged.blend' if rig else name+'.blend'
        pack_save(scene,OUT/name/filename)
        completed.append((scene,objects,rig))
        print('HARD_SURFACE_EXPORTED '+name,flush=True)
    for mesh in list(bpy.data.meshes):
        if mesh.users==0:bpy.data.meshes.remove(mesh)
    for mat in list(bpy.data.materials):
        if mat.users==0:bpy.data.materials.remove(mat)
    for scene in list(bpy.data.scenes):
        if scene not in [x[0] for x in completed]:bpy.data.scenes.remove(scene)
    gallery(completed)
    REPORT['validation']['passed']=all(v['passed'] for v in REPORT['validation'].values())
    (OUT/'Validation_Report.json').write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
    bpy.ops.file.pack_all();bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'GuLiStrike_ThreeModels_HardSurface.blend'),compress=True)
    print('HARD_SURFACE_DELIVERY_SAVED',flush=True)

if __name__=='__main__':main()
