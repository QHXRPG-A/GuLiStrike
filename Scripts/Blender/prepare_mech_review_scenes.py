import bpy,math,json
from mathutils import Vector
import mech_production_common as m
main=bpy.data.scenes['Mechs_Production_v1'];bpy.context.window.scene=main;m.stage()
keys=['SpiderMech','Mecha_01','Mecha_02','Mech_Lightest','FireWeapon_01','MissileWeapon_01','Machinegun_lvl1','Missile_01']
created=[]
for key in keys:
    name='Review_'+key
    old=bpy.data.scenes.get(name)
    if old and old.get('work_id')=='WORK-20260919-004':bpy.data.scenes.remove(old)
    m.visible([key]);deps=bpy.context.evaluated_depsgraph_get()
    pts=[o.matrix_world@Vector(v) for o in m.meshes(key) for v in o.evaluated_get(deps).bound_box]
    lo=Vector([min(p[i] for p in pts) for i in range(3)]);hi=Vector([max(p[i] for p in pts) for i in range(3)])
    center=(lo+hi)/2;span=max(hi-lo)
    scene=bpy.data.scenes.new(name);scene['work_id']='WORK-20260919-004';scene['asset_key']=key
    scene.collection.children.link(bpy.data.collections['WORK_'+key]);scene.world=main.world
    studio=bpy.data.collections.new(name+'_Studio');scene.collection.children.link(studio)
    for source in bpy.data.collections['90_STUDIO'].objects:
        obj=source.copy();obj.name=name+'_'+source.name
        if source.type in {'CAMERA','LIGHT'}:obj.data=source.data.copy()
        studio.objects.link(obj)
        if source.type=='CAMERA':
            scene.camera=obj;direction=Vector((1.5,-2,1.05)).normalized();obj.location=center+direction*span*3;obj.rotation_euler=(center-obj.location).to_track_quat('-Z','Y').to_euler();obj.data.type='ORTHO';obj.data.ortho_scale=span*1.28
        elif source.name=='Studio_Floor':obj.location.z=lo.z-.025
    scene.render.engine='CYCLES';scene.cycles.samples=24;scene.cycles.use_denoising=True;scene.cycles.device='GPU'
    scene.render.resolution_x=1100;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
    scene.view_settings.view_transform='AgX';scene.view_settings.look='AgX - Medium High Contrast';scene.view_settings.exposure=-.7
    scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1
    created.append(name)
# Each review scene links only its own model, so all model subcollections can be enabled.
for col in bpy.data.collections['10_WORKING_MODELS'].children:
    enabled=col.name.startswith('WORK_');col.hide_viewport=not enabled;col.hide_render=not enabled
bpy.data.collections['10_WORKING_MODELS'].hide_viewport=True;bpy.data.collections['10_WORKING_MODELS'].hide_render=True
bpy.data.collections['00_SOURCE_READONLY'].hide_viewport=True;bpy.data.collections['00_SOURCE_READONLY'].hide_render=True
bpy.context.window.scene=bpy.data.scenes['Review_SpiderMech']
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            space=area.spaces.active;space.region_3d.view_perspective='CAMERA';space.overlay.show_overlays=False;space.shading.type='MATERIAL';space.shading.use_scene_world=False
bpy.ops.object.select_all(action='DESELECT')
bpy.ops.file.make_paths_relative()
m.save()
(m.OUT/'review_scenes.json').write_text(json.dumps({'scenes':created,'active':'Review_SpiderMech'},indent=2),encoding='utf-8')
result={'saved':bpy.data.filepath,'review_scenes':created}
