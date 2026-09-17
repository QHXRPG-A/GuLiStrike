"""Preserve Tripo outputs and render neutral albedo/geometry inspection views."""
import bpy,json,math,sys
from pathlib import Path
from mathutils import Vector,Matrix
ROOT=Path('D:/UE5.7/test1/ArtSource/TacticalStyle_20260916/Tripo')
unit=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'Sweeper'
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(ROOT/(unit+'_Multiview_Raw.glb')))
parts=[o for o in bpy.context.scene.objects if o.type=='MESH']
for o in parts:
    o.data.transform(o.matrix_world);o.matrix_world=Matrix.Identity(4)
lo=Vector([min(v.co[i] for o in parts for v in o.data.vertices) for i in range(3)])
hi=Vector([max(v.co[i] for o in parts for v in o.data.vertices) for i in range(3)])
report={'objects':[{'name':o.name,'vertices':len(o.data.vertices),'triangles':sum(len(p.vertices)-2 for p in o.data.polygons),'uv':[u.name for u in o.data.uv_layers],'materials':[m.name for m in o.data.materials]} for o in parts], 'bounds':[list(lo),list(hi)]}
for img in bpy.data.images:
    if img.source=='FILE' or img.packed_file:
        img.filepath_raw=str(ROOT/(unit+'_'+img.name+'.png'));img.file_format='PNG';img.save()
report['textures']=[{'name':i.name,'size':list(i.size)} for i in bpy.data.images]
scene=bpy.context.scene;scene.render.engine='BLENDER_EEVEE';scene.render.resolution_x=1280;scene.render.resolution_y=1100;scene.render.resolution_percentage=100
scene.view_settings.view_transform='Standard';scene.view_settings.look='None'
scene.world=bpy.data.worlds.new('Neutral preview');scene.world.use_nodes=True;bg=scene.world.node_tree.nodes.get('Background');bg.inputs[0].default_value=(.65,.67,.7,1);bg.inputs[1].default_value=.8
center=(lo+hi)*.5;span=max(hi-lo)
data=bpy.data.cameras.new('PreviewCamera');cam=bpy.data.objects.new('PreviewCamera',data);scene.collection.objects.link(cam);data.type='ORTHO';data.ortho_scale=span*1.5;data.clip_end=10000;scene.camera=cam
for name,pos,energy in [('Key',(1,-2,4),3),('Fill',(-3,2,2),2)]:
    d=bpy.data.lights.new(name,'SUN');o=bpy.data.objects.new(name,d);scene.collection.objects.link(o);o.rotation_euler=Vector(pos).to_track_quat('Z','Y').to_euler();d.energy=energy;d.use_shadow=False
scene.render.image_settings.file_format='PNG'
for mat in bpy.data.materials:
    if not mat.use_nodes:continue
    n,l=mat.node_tree.nodes,mat.node_tree.links
    principled=next((v for v in n if v.type=='BSDF_PRINCIPLED'),None)
    if principled:
        # A neutral albedo proof avoids masking baked texture defects with lights.
        out=next(v for v in n if v.type=='OUTPUT_MATERIAL');em=n.new('ShaderNodeEmission')
        color=principled.inputs['Base Color']
        if color.is_linked:l.new(color.links[0].from_socket,em.inputs[0])
        else:em.inputs[0].default_value=color.default_value
        l.new(em.outputs[0],out.inputs['Surface'])
for view,direction in [('front_quarter',(1,-1.5,.8)),('rear_quarter',(-1,1.5,.8)),('top',(0,0,1))]:
    cam.location=center+Vector(direction)*span;cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
    scene.render.filepath=str(ROOT/(unit+'_raw_'+view+'.png'));bpy.ops.render.render(write_still=True)
(ROOT/(unit+'_raw_report.json')).write_text(json.dumps(report,indent=2),encoding='utf8')
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/(unit+'_Raw_Inspection.blend')))
print('TRIPO_INSPECT_OK',json.dumps(report),flush=True)
