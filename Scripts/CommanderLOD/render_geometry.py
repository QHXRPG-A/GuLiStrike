"""Real Blender gray-geometry comparisons and vehicle wheel articulation inspection."""
import bpy,math,json,sys
from pathlib import Path
from mathutils import Vector,Matrix
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART
frames=[]
for name in ['DefaultSoldier','WM01','ElectromagneticMiner','ConstructionVehicle','SweeperSummon']:
 bpy.ops.wm.open_mainfile(filepath=str(ART/name/(name+'_3Tier.blend')))
 scene=bpy.context.scene;scene.render.engine='BLENDER_EEVEE'
 scene.render.resolution_x=scene.render.resolution_y=1024;scene.render.resolution_percentage=100
 scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGB'
 scene.view_settings.view_transform='Standard';scene.view_settings.look='None'
 scene.world=bpy.data.worlds.new('CommanderLOD_GrayWorld');scene.world.use_nodes=True
 background=scene.world.node_tree.nodes.new('ShaderNodeBackground');background.inputs[0].default_value=(.055,.055,.055,1)
 world_output=scene.world.node_tree.nodes.new('ShaderNodeOutputWorld');scene.world.node_tree.links.new(background.outputs[0],world_output.inputs[0])
 material=bpy.data.materials.new('CommanderLOD_GrayInspection');material.use_nodes=True
 shader=next(node for node in material.node_tree.nodes if node.type=='BSDF_PRINCIPLED');shader.inputs['Base Color'].default_value=(.32,.34,.33,1)
 shader.inputs['Roughness'].default_value=.7;shader.inputs['Metallic'].default_value=0
 scene.view_layers[0].material_override=material
 for obj in scene.objects:
  if obj.type in ['LIGHT','CAMERA']:bpy.data.objects.remove(obj,do_unlink=True)
 def choose(lod):
  for j in range(3):
   for ob in bpy.data.collections[name+'_LOD'+str(j)].objects:
    ob.hide_set(j!=lod);ob.hide_render=j!=lod
  bpy.context.view_layer.update()
 choose(0)
 meshes=[o for o in bpy.data.collections[name+'_LOD0'].objects if o.type=='MESH']
 points=[ob.matrix_world@Vector(corner) for ob in meshes for corner in ob.bound_box]
 low=Vector([min(p[i] for p in points) for i in range(3)]);high=Vector([max(p[i] for p in points) for i in range(3)])
 center=(low+high)*.5;size=max(high-low)*1.35
 camera_data=bpy.data.cameras.new('CommanderLOD_GrayCamera');camera=bpy.data.objects.new(camera_data.name,camera_data)
 scene.collection.objects.link(camera);scene.camera=camera;camera_data.type='ORTHO';camera_data.ortho_scale=size
 light_data=bpy.data.lights.new('CommanderLOD_GrayKey','AREA');light=bpy.data.objects.new(light_data.name,light_data)
 scene.collection.objects.link(light);light.location=center+Vector((size,-size,size*1.8))
 light.rotation_euler=(center-light.location).to_track_quat('-Z','Y').to_euler();light_data.energy=180*size*size;light_data.shape='DISK';light_data.size=size*.7
 def view(offset):
  camera.location=center+Vector(offset)*size;camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler()
 def render(lod,view_name):
  folder=ART/name/'Review/Blender';folder.mkdir(exist_ok=True)
  file=folder/f'LOD{lod}_{view_name}.png';scene.render.filepath=str(file);bpy.ops.render.render(write_still=True)
  frames.append(dict(name=name,lod=lod,view=view_name,file=str(file)))
 for lod in range(3):
  choose(lod)
  for key,offset in [('GrayHero',(1,-1,.8)),('GrayFront',(2,0,0)),('GrayLeft',(0,-2,0)),('GrayBack',(-2,0,0))]:
   view(offset);render(lod,key)
  if name in ['ElectromagneticMiner','ConstructionVehicle']:
   rigs=[o for o in bpy.data.collections[name+'_LOD'+str(lod)].objects if o.type=='ARMATURE']
   assert rigs
   saved=[(p,p.matrix.copy()) for rig in rigs for p in rig.pose.bones if p.name.startswith('Wheel_')]
   assert len(saved)==6,len(saved)
   scene.render.resolution_x=scene.render.resolution_y=640;view((1,-1,.8))
   for frame in range(8):
    for pose,original in saved:
     pivot=original.translation;pose.matrix=Matrix.Translation(pivot)@Matrix.Rotation(math.tau*frame/8,4,'Y')@Matrix.Translation(-pivot)@original
    bpy.context.view_layer.update();render(lod,f'Mechanism_{frame:03d}')
   for pose,original in saved:pose.matrix=original
   scene.render.resolution_x=scene.render.resolution_y=1024
report=dict(success=True,scope='Actual Blender FBX geometry; vehicle wheel articulation demonstration. No game simulation.',frames=frames)
(ART/'Reports/blender_geometry_preview.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('BLENDER_GEOMETRY_FRAMES',len(frames),flush=True)
