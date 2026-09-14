import bpy
import math
import json
from pathlib import Path
from mathutils import Vector, Matrix

ROOT = Path(__file__).resolve().parents[1]

def activate(objects):
    if bpy.context.object and bpy.context.object.mode != 'OBJECT':
        bpy.ops.object.mode_set(mode='OBJECT')
    bpy.ops.object.select_all(action='DESELECT')
    for ob in objects:
        ob.hide_set(False)
        ob.select_set(True)
    if objects:
        bpy.context.view_layer.objects.active = objects[0]

def bounds(objects):
    coords = [ob.matrix_world @ Vector(c) for ob in objects if ob.type == 'MESH' for c in ob.bound_box]
    return (Vector([min(v[i] for v in coords) for i in range(3)]),
            Vector([max(v[i] for v in coords) for i in range(3)]))

def new_scene(name):
    scene = bpy.data.scenes.new(name)
    bpy.context.window.scene = scene
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 1.0
    return scene

def load_asset(name, path, target_span=None, axis=None):
    scene = new_scene(name)
    bpy.ops.import_scene.gltf(filepath=str(path), import_pack_images=True, merge_vertices=True)
    objects = [ob for ob in scene.objects if ob.type == 'MESH']
    for idx, ob in enumerate(objects):
        world = ob.matrix_world.copy()
        ob.parent = None
        ob.matrix_world = world
        activate([ob])
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        ob.name = name + ('_Mesh' if len(objects)==1 else f'_Part_{idx:02d}')
    for ob in list(scene.objects):
        if ob.type != 'MESH':
            bpy.data.objects.remove(ob, do_unlink=True)
    bpy.context.view_layer.update()
    lo, hi = bounds(objects)
    span = (hi-lo)[axis] if axis is not None else max(hi-lo)
    scale = target_span/span if target_span else 1.0
    center = Vector(((lo.x+hi.x)/2,(lo.y+hi.y)/2,lo.z))
    for ob in objects:
        for v in ob.data.vertices:
            v.co = (v.co-center)*scale
        ob.data.update()
    bpy.context.view_layer.update()
    return scene, objects

def material(name,color,metallic=0.0,roughness=.5,emission=0):
    mat=bpy.data.materials.new(name)
    mat.diffuse_color=(*color,1)
    mat.use_nodes=True
    bsdf=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
    bsdf.inputs['Base Color'].default_value=(*color,1)
    bsdf.inputs['Metallic'].default_value=metallic
    bsdf.inputs['Roughness'].default_value=roughness
    if emission:
        bsdf.inputs['Emission Color'].default_value=(*color,1)
        bsdf.inputs['Emission Strength'].default_value=emission
    return mat

def aim(ob,target):
    ob.rotation_euler=(Vector(target)-ob.location).to_track_quat('-Z','Y').to_euler()

def setup_studio(scene,objects,res=(1400,1100),samples=48):
    bpy.context.window.scene=scene
    lo,hi=bounds(objects)
    size=max(hi-lo)
    center=(lo+hi)/2
    stage=bpy.data.collections.new('STUDIO_ExcludedFromExports')
    scene.collection.children.link(stage)
    world=bpy.data.worlds.new(scene.name+'_World')
    world.use_nodes=True
    background=next(n for n in world.node_tree.nodes if n.type=='BACKGROUND')
    world_output=next(n for n in world.node_tree.nodes if n.type=='OUTPUT_WORLD')
    background.inputs['Color'].default_value=(.16,.19,.25,1)
    background.inputs['Strength'].default_value=.65
    wn=world.node_tree.nodes
    wl=world.node_tree.links
    bgcam=wn.new('ShaderNodeBackground')
    bgcam.inputs['Color'].default_value=(.014,.021,.033,1)
    bgcam.inputs['Strength'].default_value=1
    ray=wn.new('ShaderNodeLightPath')
    mix=wn.new('ShaderNodeMixShader')
    wl.new(ray.outputs['Is Camera Ray'],mix.inputs[0])
    wl.new(background.outputs[0],mix.inputs[1])
    wl.new(bgcam.outputs[0],mix.inputs[2])
    wl.new(mix.outputs[0],world_output.inputs['Surface'])
    scene.world=world
    for label,mult,power,light_size,color in [
        ('Key',(1.5,-2.2,3),2100,2.4,(.90,.95,1)),
        ('Fill',(-2,-.5,1.5),1200,2.2,(.55,.74,1)),
        ('Rim',(.3,2.0,2.5),2500,1.9,(1,.79,.56))]:
        dat=bpy.data.lights.new(label,'AREA')
        dat.energy=power*(size/5)**2
        dat.shape='DISK'
        dat.size=light_size*size/5
        dat.color=color
        ob=bpy.data.objects.new(label,dat)
        stage.objects.link(ob)
        ob.location=center+Vector(mult)*size
        aim(ob,center)
    camdat=bpy.data.cameras.new('PresentationCamera')
    camera=bpy.data.objects.new('PresentationCamera',camdat)
    stage.objects.link(camera)
    scene.camera=camera
    camera.data.type='ORTHO'
    camera.data.ortho_scale=size*1.32
    camera.data.lens=50
    camera.data.clip_end=1000
    camera.location=center+Vector((1.3,-1.9,1.15))*size
    aim(camera,center)
    scene.render.engine='CYCLES'
    scene.cycles.samples=samples
    scene.cycles.use_denoising=True
    if 'cycles' in bpy.context.preferences.addons:
        pref=bpy.context.preferences.addons['cycles'].preferences
        try:
            pref.compute_device_type='OPTIX'
            pref.get_devices()
            for d in pref.devices:
                d.use=d.type=='OPTIX'
            scene.cycles.device='GPU'
        except Exception:
            scene.cycles.device='CPU'
    scene.render.resolution_x,scene.render.resolution_y=res
    scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG'
    scene.render.image_settings.color_mode='RGBA'
    scene.render.film_transparent=False
    scene.view_settings.view_transform='AgX'
    scene.view_settings.look='AgX - Medium High Contrast'
    scene.view_settings.exposure=.9
    return camera,center,size,stage

def render(scene,path,camera=None,direction=None,target=None,scale=None):
    bpy.context.window.scene=scene
    camera=camera or scene.camera
    if direction is not None:
        camera.location=Vector(target)+Vector(direction)*10
        aim(camera,target)
    if scale is not None:
        camera.data.ortho_scale=scale
    else:
        fit_camera(scene,camera,[ob for ob in scene.objects if ob.type=='MESH' and not ob.hide_render])
    scene.render.filepath=str(path)
    Path(path).parent.mkdir(parents=True,exist_ok=True)
    bpy.ops.render.render(write_still=True,scene=scene.name)

def fit_camera(scene,camera,objects,padding=1.15):
    bpy.context.view_layer.update()
    dg=bpy.context.evaluated_depsgraph_get()
    inv=camera.matrix_world.inverted()
    points=[]
    for ob in objects:
        ev=ob.evaluated_get(dg)
        points.extend(inv @ (ev.matrix_world @ Vector(c)) for c in ev.bound_box)
    lo=Vector([min(p[i] for p in points) for i in range(3)])
    hi=Vector([max(p[i] for p in points) for i in range(3)])
    aspect=scene.render.resolution_x/scene.render.resolution_y
    camera.data.ortho_scale=max(hi.x-lo.x,(hi.y-lo.y)*aspect)*padding
    correction=camera.matrix_world.to_3x3() @ Vector(((lo.x+hi.x)/2,(lo.y+hi.y)/2,0))
    camera.location+=correction
    bpy.context.view_layer.update()

def describe(objects):
    lo,hi=bounds(objects)
    out={'dimensions_m':list(hi-lo),'min':list(lo),'max':list(hi),'objects':[]}
    for ob in objects:
        if ob.type!='MESH':
            continue
        ob.data.calc_loop_triangles()
        out['objects'].append({'name':ob.name,'verts':len(ob.data.vertices),'triangles':len(ob.data.loop_triangles),
                               'materials':[s.material.name if s.material else None for s in ob.material_slots],
                               'uv_layers':[x.name for x in ob.data.uv_layers],
                               'modifiers':[m.type for m in ob.modifiers]})
    return out

def pack_save(scene,path):
    bpy.context.window.scene=scene
    bpy.ops.file.pack_all()
    bpy.data.libraries.write(str(path),{scene},fake_user=True,compress=True)
