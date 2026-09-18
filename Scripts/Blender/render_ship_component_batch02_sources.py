"""Render untouched existing rigged sources for Batch 02 reference approval A.

Only inspection copies receive neutral display material. No production textures,
line masks, geometry, or approved deliverables are changed here.
"""
import bpy
import hashlib
import json
from pathlib import Path
from mathutils import Vector

ROOT=Path('D:/UE5.7/test1/ArtSource/Ships/ShipComponentStyle_20260917/Batch02')
OUT=ROOT/'Source/Previews'
OUT.mkdir(parents=True,exist_ok=True)
KEYS=('Autocannon','Triple_Barrel_Turret','Single_Barrel_Turret')
report={'purpose':'Unchanged original rigged meshes for design-A evidence only','parts':{}}
VIEWS={'hero':(1.1,-1.7,1.65),'front':(0,-1,0),'right':(1,0,0),'rear':(0,1,0),'top':(0,0,1)}

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
for key in KEYS:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene=bpy.context.scene;scene.name=key+'_Original_Inspection'
    path=ROOT/'Source/ExistingAuthoring'/('SC_'+key+'.blend')
    with bpy.data.libraries.load(str(path),link=False) as (available,loaded):
        loaded.objects=[n for n in available.objects if n in ('SKM_SC_'+key,'Armature')]
    for obj in loaded.objects:scene.collection.objects.link(obj)
    body=next(o for o in loaded.objects if o.type=='MESH')
    arm=next(o for o in loaded.objects if o.type=='ARMATURE')
    arm.animation_data_clear()
    for bone in arm.pose.bones:bone.matrix_basis.identity()
    scene.frame_set(0)
    body.data.materials.clear()
    mat=bpy.data.materials.new('Inspection_Only_Neutral');mat.diffuse_color=(.53,.58,.61,1)
    body.data.materials.append(mat)
    for polygon in body.data.polygons:polygon.material_index=0
    scene.render.engine='BLENDER_WORKBENCH'
    scene.render.resolution_x=1440;scene.render.resolution_y=1080;scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG'
    scene.display.shading.light='STUDIO';scene.display.shading.studiolight_rotate_z=.6
    scene.display.shading.color_type='MATERIAL';scene.display.shading.show_shadows=True
    scene.display.shading.show_cavity=True;scene.display.shading.cavity_type='BOTH'
    scene.display.shading.show_object_outline=True;scene.display.shading.background_type='WORLD'
    scene.world=bpy.data.worlds.new('InspectionBackground');scene.world.color=(.16,.20,.23)
    scene.view_settings.view_transform='Standard'
    camera=bpy.data.objects.new('OriginalReferenceCamera',bpy.data.cameras.new('OriginalReferenceCamera'))
    scene.collection.objects.link(camera);scene.camera=camera;camera.data.type='ORTHO'
    points=[body.matrix_world@v.co for v in body.data.vertices]
    lo=Vector([min(v[i] for v in points) for i in range(3)])
    hi=Vector([max(v[i] for v in points) for i in range(3)])
    center=(lo+hi)*.5;span=max(hi-lo)
    camera.data.clip_start=.01;camera.data.clip_end=span*30
    views={}
    hero_camera=None
    for view,direction in VIEWS.items():
        direction=Vector(direction).normalized()
        camera.location=center+direction*span*3
        camera.rotation_euler=(-direction).to_track_quat('-Z','Y').to_euler()
        bpy.context.view_layer.update()
        projected=[camera.matrix_world.inverted()@p for p in points]
        width=max(p.x for p in projected)-min(p.x for p in projected)
        height=max(p.y for p in projected)-min(p.y for p in projected)
        screen_center=Vector(((max(p.x for p in projected)+min(p.x for p in projected))*.5,
                              (max(p.y for p in projected)+min(p.y for p in projected))*.5,0))
        camera.location+=camera.rotation_euler.to_quaternion()@screen_center
        camera.data.ortho_scale=max(width,height*4/3)*1.18
        if view=='hero':hero_camera=(camera.location.copy(),camera.rotation_euler.copy(),camera.data.ortho_scale)
        image=OUT/(key+'_original_'+view+'_v1.png')
        scene.render.filepath=str(image);bpy.ops.render.render(write_still=True)
        views[view]={'path':image.relative_to(ROOT).as_posix(),'sha256':sha(image)}
    camera.location,camera.rotation_euler,camera.data.ortho_scale=hero_camera
    body.data.calc_loop_triangles()
    report['parts'][key]={'authoring_source':path.relative_to(ROOT).as_posix(),'authoring_sha256':sha(path),
        'dimensions_m':list(hi-lo),'vertices':len(body.data.vertices),'triangles':len(body.data.loop_triangles),
        'bones':[b.name for b in arm.data.bones],'views':views}
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'Source'/(key+'_Original_Inspection.blend')),compress=True)
(ROOT/'Source/prototype_render_manifest_v1.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('BATCH02_ORIGINAL_REFERENCE_RENDERS_COMPLETE',flush=True)
