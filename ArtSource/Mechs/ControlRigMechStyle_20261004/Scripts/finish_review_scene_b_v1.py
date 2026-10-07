"""Keep the portable material scene separate from honest native line-art B renders."""
import bpy,json,shutil
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004'); O=R/'Production_B_v1'
bpy.ops.wm.open_mainfile(filepath=str(O/'ControlRigMech_B_v1_Production.blend'))
portable=bpy.data.scenes.get('PORTABLE_SHADER_LODS') or bpy.context.scene
bpy.context.window.scene=portable; portable.name='PORTABLE_SHADER_LODS'
for old in list(bpy.data.scenes):
    if old.name.startswith('REVIEW_B_ReferenceMatched'): bpy.data.scenes.remove(old)
for ob in list(bpy.data.objects):
    if ob.name.startswith('B_Review_'): bpy.data.objects.remove(ob,do_unlink=True)
for c in list(bpy.data.collections):
    if c.name.startswith('REVIEW_GEOMETRY_SAME_AS_PRODUCTION_LODS'): bpy.data.collections.remove(c)
P=O/'Portable'; P.mkdir(exist_ok=True)
setup=json.loads((R/'References_A_v2/reference_setup.json').read_text(encoding='utf-8'))
portable.render.use_freestyle=False
for view,cam in setup['cameras'].items():
    portable.camera.location=cam['location_m']; portable.camera.rotation_euler=cam['rotation_radians']
    portable.camera.data.ortho_scale=cam['ortho_scale_m']
    portable.render.filepath=str(P/f'ControlRigMech_B_v1_Portable_{view}.png')
    bpy.ops.render.render(write_still=True,scene=portable.name)
review=portable.copy(); review.name='REVIEW_B_ReferenceMatched'; review.use_fake_user=True; portable.use_fake_user=True
for c in list(review.collection.children): review.collection.children.unlink(c)
for ob in list(review.collection.objects): review.collection.objects.unlink(ob)
review_col=bpy.data.collections.new('REVIEW_GEOMETRY_SAME_AS_PRODUCTION_LODS'); review.collection.children.link(review_col)
rig=bpy.data.objects['Armature']; review_col.objects.link(rig)
camera=portable.camera.copy(); camera.data=camera.data.copy(); camera.name='B_Review_OrthographicCamera'
review_col.objects.link(camera); review.camera=camera
material=next(o for o in portable.objects if o.name=='ControlRigMech_LOD0_Body').data.materials[0].copy()
material.name='M_ThreeTone_Review_NativeLineArt'; material.node_tree.nodes['InternalLineStrength'].inputs[1].default_value=0
for lod in range(4):
    src=bpy.data.objects[f'ControlRigMech_LOD{lod}_Body']; ob=src.copy(); ob.data=src.data.copy()
    ob.name=f'B_Review_LOD{lod}_Body'; review_col.objects.link(ob)
    ob.hide_render=lod!=0; ob.hide_viewport=lod!=0
    ob.data.materials.clear(); ob.data.materials.append(material)
    ob['same_geometry_as']=src.name; ob['line_render']='native Blender Freestyle, NOT portable shell/mask preview'
review.render.use_freestyle=True
review['presentation_mode']='actual same production body; native Freestyle for reference fidelity'
portable['presentation_mode']='actual independent internal mask and budgeted inverted-hull shell; native Freestyle disabled'
review.render.resolution_x=review.render.resolution_y=2048; review.render.resolution_percentage=100
setup=json.loads((R/'References_A_v2/reference_setup.json').read_text(encoding='utf-8'))
for view,cam in setup['cameras'].items():
    camera.location=cam['location_m']; camera.rotation_euler=cam['rotation_radians']; camera.data.ortho_scale=cam['ortho_scale_m']
    review.render.filepath=str(O/f'ControlRigMech_B_v1_{view}.png')
    bpy.ops.render.render(write_still=True,scene=review.name)
cam=setup['cameras']['Hero']; camera.location=cam['location_m']; camera.rotation_euler=cam['rotation_radians']
camera.data.ortho_scale=cam['ortho_scale_m']
review['review_B']='pending'; bpy.context.window.scene=review
report=json.loads((O/'production_render_report.json').read_text(encoding='utf-8'))
report['main_review_images']='actual production body rendered with native Freestyle; atlas internal mask disabled ONLY in review material; outline shell excluded ONLY from review scene'
report['portable_shader_images']='Portable/; actual shader mask and outline hull; no Freestyle'
report['portable_line_fidelity']='partial: portable fine lines are softer than approved Freestyle reference; requires further refinement before formal UE delivery'
report['geometry_shared_semantics']='review body is a mesh-data copy with exactly the same topology, positions, normals, skin weights and color attribute as corresponding portable LOD body'
report['default_scene']=review.name
(O/'production_render_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(O/'ControlRigMech_B_v1_Production.blend'))
print('B_REVIEW_SCENE_READY',flush=True)
