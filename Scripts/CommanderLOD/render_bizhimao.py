"""Real Blender comparison renders of the compressed distance LOD revision."""
import bpy,json,math
from pathlib import Path
from mathutils import Vector,Matrix
OUT=Path('D:/UE5.7/test1/ArtSource/CommanderLOD_20261005/BiZhiMao');REVIEW=OUT/'Review'
bpy.ops.wm.open_mainfile(filepath=str(OUT/'BiZhiMao_3Tier.blend'))
s=bpy.context.scene;rig=bpy.data.objects['Armature'];root=bpy.data.objects['BiZhiMao_PresentationScale_2x'];root.location=(0,0,0)
rig.data.pose_position='POSE';rig.animation_data.action=bpy.data.actions['BiZhiMao_Idle_Deployed'];s.frame_set(1)
s.render.engine='BLENDER_EEVEE';s.render.image_settings.file_format='PNG';s.render.image_settings.color_mode='RGBA';s.render.resolution_percentage=100
s.render.resolution_x=s.render.resolution_y=2048;s.render.film_transparent=False
camera=s.camera;center=Vector((0,-3.05,6.89));rendered=[]
def choose(lod,before=False,gray=False):
 suffix='_Previous_v1' if before and lod else ''
 keep=[f'ControlRigMech_LOD{lod}_Body'+suffix]+([f'ControlRigMech_B_v4_LOD{lod}_Outline'+suffix] if lod<2 else [])
 for ob in s.objects:
  if ob.type=='MESH' and ('ControlRigMech' in ob.name or ob.name.startswith(('Mech_','SKM_Mech'))):
   hide=ob.name not in keep;ob.hide_render=hide;ob.hide_set(hide)
 if gray:
  for ob in s.objects:
   if ob.name in keep:
    saved[ob.name]=list(ob.data.materials);ob.data.materials.clear();ob.data.materials.append(graymat)
def view(where,orth=False,scale=36):
 camera.location=Vector(where);camera.rotation_euler=(center-camera.location).to_track_quat('-Z','Y').to_euler()
 camera.data.type='ORTHO' if orth else 'PERSP';camera.data.ortho_scale=scale;camera.data.lens=48
def render(name):
 path=REVIEW/name;path.parent.mkdir(parents=True,exist_ok=True);s.render.filepath=str(path);bpy.ops.render.render(write_still=True);rendered.append(name)
graymat=bpy.data.materials.new('LOD_Review_Neutral_Gray');graymat.diffuse_color=(.38,.38,.38,1);graymat.use_nodes=True
shader=next(n for n in graymat.node_tree.nodes if n.type=='BSDF_PRINCIPLED');shader.inputs['Base Color'].default_value=(.38,.38,.38,1);shader.inputs['Roughness'].default_value=.8
saved={}
for lod in range(3):
 for before in [False]:
  choose(lod,before);prefix=f'LOD{lod}_'+('Before' if before else 'After')
  for key,where,orth in [('Hero',(26,-34,24),False),('Front',(0,-50,6.89),True),('Left',(50,-3.05,6.89),True),('Back',(0,50,6.89),True)]:
   view(where,orth);render(prefix+'_'+key+'.png')
  choose(lod,before,True);view((26,-34,24));render(prefix+'_Gray.png')
  for name,mats in saved.items():
   mesh=bpy.data.objects[name].data;mesh.materials.clear()
   for m in mats:mesh.materials.append(m)
  saved.clear()
s.render.resolution_x=s.render.resolution_y=640
view((26,-34,24))
for lod in range(3):
 choose(lod)
 for name in ['Idle','Forward','Backward','Left','Right']:
  rig.animation_data.action=bpy.data.actions['BiZhiMao_Idle_Deployed' if name=='Idle' else 'BiZhiMao_'+name]
  for i in range(8):
   phase=i/8;frame=1+phase*(260 if name=='Idle' else 90);s.frame_set(math.floor(frame),subframe=frame%1)
   render(f'LOD{lod}_{name}/{i:03d}.png')
 # Fixed camera zooms illustrate relative screen size; no claim about a UE gameplay camera.
 rig.animation_data.action=bpy.data.actions['BiZhiMao_Idle_Deployed'];s.frame_set(1)
 s.render.resolution_x=s.render.resolution_y=1024
 for before in [False]:
  choose(lod,before);view((26,-34,24),True,36/[1,.4,.06][lod]);render(f'LOD{lod}_'+('Before' if before else 'After')+'_Distance.png')
 s.render.resolution_x=s.render.resolution_y=640;view((26,-34,24))
# Actual mechanical limits on the reduced meshes, using the approved articulation.
def aim(yaw,pitch):
 rig.animation_data.action=bpy.data.actions['BiZhiMao_Idle_Deployed'];s.frame_set(1);bpy.context.view_layer.update()
 original={p.name:p.matrix.copy() for p in rig.pose.bones};upper=original['turret_base'].translation;gun=original['main_gun'].translation
 yd=Matrix.Translation(upper)@Matrix.Rotation(-math.radians(yaw),4,'Z')@Matrix.Translation(-upper)
 pd=Matrix.Translation(gun)@Matrix.Rotation(-math.radians(pitch),4,'X')@Matrix.Translation(-gun)
 desired={}
 for bone in rig.data.bones:
  is_upper=bone.name=='turret_base' or any(p.name=='turret_base' for p in bone.parent_recursive)
  is_gun=bone.name=='main_gun' or any(p.name=='main_gun' for p in bone.parent_recursive)
  desired[bone.name]=(yd if is_upper else Matrix.Identity(4))@(pd if is_gun else Matrix.Identity(4))@original[bone.name]
 for side in ['l','r']:
  a,b='piston_01_'+side,'piston_02_'+side;old_a=original[a].translation;old_b=original[b].translation;new_a=pd@old_a
  q=(old_b-old_a).rotation_difference(old_b-new_a)
  for n,new in [(a,new_a),(b,old_b)]:
   mat=q.to_matrix().to_4x4()@original[n];mat.translation=new;desired[n]=yd@mat
 rig.animation_data.action=None
 for n in [b.name for b in rig.data.bones]:rig.pose.bones[n].matrix=desired[n]
 bpy.context.view_layer.update()
s.render.resolution_x=s.render.resolution_y=2048;center=Vector((0,-3.05,11.5));view((36,-46,33))
for lod in range(3):
 choose(lod)
 for name,yaw,pitch in [('PitchMin',0,-10),('PitchMax',0,45),('Yaw180',180,0)]:aim(yaw,pitch);render(f'LOD{lod}_After_{name}.png')
(OUT/'Reports/blender_review.json').write_text(json.dumps(dict(version='BiZhiMao_CommanderLOD_3Tier_v1',actual_blender_renders=True,
 images=rendered,body_scale=2,runtime_bones=0,source_rig_used_offline_only=True,ue_validation='not_run; version B pending',
 distance_images='Blender relative screen-size illustration, not UE commander camera measurements',approval='pending'),ensure_ascii=False,indent=2),encoding='utf8')
print('COMPRESSED_LOD_REVIEW_RENDERED',len(rendered),flush=True)
