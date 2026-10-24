"""Make a reference-only orthographic sheet from untouched imported geometry."""
import bpy, math, json
from pathlib import Path
from mathutils import Vector, Matrix

ART=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023')
OUT=ART/'Reference_A_v1'; OUT.mkdir(parents=True,exist_ok=True)
source=bpy.data.scenes['MechaDestroyer_SourceReference']
obj=next(o for o in source.objects if o.type=='MESH')
mat=bpy.data.materials.new('Source_Reference_Albedo')
mat.use_nodes=True
bsdf=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
tex=mat.node_tree.nodes.new('ShaderNodeTexImage')
tex.image=bpy.data.images.load(str(ART/'SourceExport/TX_Mecha_D.tga'),check_existing=True)
mat.node_tree.links.new(tex.outputs['Color'],bsdf.inputs['Base Color'])
bsdf.inputs['Roughness'].default_value=.7
scene=bpy.data.scenes.get('MechaDestroyer_Reference_Orthographic') or bpy.data.scenes.new('MechaDestroyer_Reference_Orthographic')
bpy.context.window.scene=scene
scene.render.engine='CYCLES'
scene.cycles.samples=16
scene.cycles.use_denoising=True
scene.render.resolution_x=2400;scene.render.resolution_y=1100;scene.render.resolution_percentage=100
scene.world=bpy.data.worlds.new('MechaDestroyer_ReferenceWorld')
scene.world.use_nodes=True
background=next(n for n in scene.world.node_tree.nodes if n.type=='BACKGROUND')
background.inputs[0].default_value=(.6,.6,.6,1)
background.inputs[1].default_value=.6
scene.view_settings.view_transform='Standard'
scene.view_settings.look='None'
geo=obj.data.copy()
geo.transform(obj.matrix_world)
center=Vector((.1099977,-.340668,0))
geo.transform(Matrix.Translation(-center))
geo.materials.clear();geo.materials.append(mat)
for name,x,angle in [('FRONT',-4.5,0),('SIDE',0,math.pi/2),('BACK',4.5,math.pi)]:
    display=bpy.data.objects.new('ReferenceOnly_'+name,geo)
    scene.collection.objects.link(display);display.location.x=x;display.rotation_euler.z=angle
    txt=bpy.data.curves.new('Label_'+name,'FONT');txt.body=name;txt.align_x='CENTER';txt.size=.28
    label=bpy.data.objects.new('Label_'+name,txt);scene.collection.objects.link(label)
    label.location=(x,-2.5,-.45);label.rotation_euler=(math.pi/2,0,0)
cam_data=bpy.data.cameras.new('SourceReferenceCamera');cam_data.type='ORTHO';cam_data.ortho_scale=14.7
cam=bpy.data.objects.new('SourceReferenceCamera',cam_data);scene.collection.objects.link(cam)
cam.location=(0,-22,2.4);cam.rotation_euler=(Vector((0,0,2.4))-cam.location).to_track_quat('-Z','Y').to_euler();scene.camera=cam
for name,location,power,size in [('Key',(-5,-8,10),1800,8),('Fill',(7,-5,7),1100,8),('Top',(0,5,11),1700,7)]:
    data=bpy.data.lights.new(name,'AREA');data.energy=power;data.shape='DISK';data.size=size
    light=bpy.data.objects.new(name,data);scene.collection.objects.link(light);light.location=location
    light.rotation_euler=(Vector((0,0,2.5))-light.location).to_track_quat('-Z','Y').to_euler()
scene.render.image_settings.file_format='PNG'
scene.render.filepath=str(OUT/'Source_Orthographic.png')
bpy.ops.wm.save_as_mainfile(filepath=str(ART/'MechaDestroyer_SourceReference.blend'))
bpy.ops.render.render(write_still=True)
result={'success':True,'reference_image':scene.render.filepath,'blend':bpy.data.filepath,'source_geometry_modified':False,'model_production_started':False}
(OUT/'source_reference_record.json').write_text(json.dumps(result,indent=2),encoding='utf8')
