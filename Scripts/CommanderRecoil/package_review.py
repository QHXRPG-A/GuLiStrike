"""One playable Blender review file with scene switching; no rendered previews."""
import bpy,json
from pathlib import Path
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'ArtSource/CommanderRecoil_20261114'
bpy.ops.wm.open_mainfile(filepath=str(OUT/'WarMachine/WarMachine_Recoil_v1.blend'),load_ui=False)
heavy=bpy.context.scene;heavy.name='03_重防号_左右4发'
for path in (OUT/'Pioneer/Pioneer_Recoil_v1.blend',OUT/'Miner/QuadrupedMiner_Action_v6.blend',
             OUT/'WarMachine/Missile_v1/WarMachine_Missile_v1.blend'):
    with bpy.data.libraries.load(str(path),link=False) as (src,dst):dst.scenes=src.scenes
for scene in bpy.data.scenes:
    if scene.name.startswith('先驱者'):scene.name='04_先驱者_双枪8轮'
    bpy.context.window.scene=scene
    points=[]
    for frame in ((1,7) if scene.name.startswith('05_') else (1,)):
        scene.frame_set(frame);bpy.context.view_layer.update()
        dg=bpy.context.evaluated_depsgraph_get()
        visible=[o.evaluated_get(dg) for o in scene.objects if o.type=='MESH' and 'Studio' not in o.name and 'Laser' not in o.name and o.visible_get(view_layer=bpy.context.view_layer)]
        points.extend(o.matrix_world@Vector(v) for o in visible for v in o.bound_box)
    scene.frame_set(1);bpy.context.view_layer.update()
    low=Vector([min(p[i] for p in points) for i in range(3)]);high=Vector([max(p[i] for p in points) for i in range(3)])
    center=(low+high)/2;span=max(high-low)
    camera=bpy.data.cameras.new('ActionReviewCamera');camera.type='ORTHO'
    camera.clip_start=max(.01,span*.001);camera.clip_end=max(1000.,span*10)
    obj=bpy.data.objects.new('ActionReviewCamera',camera);scene.collection.objects.link(obj)
    obj.location=center+Vector((1.5,-2.2,1.2))*span;obj.rotation_euler=(center-obj.location).to_track_quat('-Z','Y').to_euler();scene.camera=obj
    scene.render.resolution_x=1600;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
    bpy.context.view_layer.update()
    local=[obj.matrix_world.inverted()@v for v in points]
    camera.ortho_scale=max(max(abs(p.x) for p in local)*2,max(abs(p.y) for p in local)*2*1.6)*1.18
    # Numeric frustum verification only; no screenshots, rendering or visual QA.
    projected=[world_to_camera_view(scene,obj,v) for v in points]
    assert all(.03<p.x<.97 and .03<p.y<.97 and camera.clip_start<p.z<camera.clip_end for p in projected),scene.name
    scene['审核视角检查']=json.dumps({'visible_meshes':len(visible),'model_span':span,'camera_depth':[min(p.z for p in projected),max(p.z for p in projected)],'clip':[camera.clip_start,camera.clip_end]})
review=bpy.data.scenes['05_重防号_导弹持续后倾']
bpy.context.window.scene=review;review.frame_set(1);bpy.context.view_layer.update()
bpy.context.window.workspace=bpy.data.workspaces.get('Layout') or bpy.context.window.workspace
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            v=area.spaces.active;v.use_local_camera=False;v.camera=review.camera
            v.clip_start=.01;v.clip_end=10000.;v.region_3d.view_camera_zoom=0
            v.region_3d.view_perspective='CAMERA';v.shading.type='MATERIAL'
            v.region_3d.view_location=Vector((0,0,18));v.region_3d.view_distance=90
notes=bpy.data.texts.new('动作审核说明')
notes.write('右上方Scene切换矿车01行走/02采矿、03重防号普攻、04先驱者、05重防号导弹。鼠标放在视口按空格播放。\n重防号4发后额外2秒，先驱者8轮齐射后额外2秒；首帧中立。\n导弹版：0.10秒后倾至8度、保持1.20秒、0.50秒回正；双枪无独立回缩，无左右扭转；末尾0.60秒中立仅供循环审核。\n重防号与先驱者此文件演示独立后坐层，游戏内会叠加既有悬浮/六足步态。外形沿用正式网格，游戏已审材质保留。\n未由助手读图验收。')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'CommanderActions_Review_v3.blend'),check_existing=False,compress=True)
(OUT/'review-visibility-v3.json').write_text(json.dumps({s.name:json.loads(s['审核视角检查']) for s in bpy.data.scenes},ensure_ascii=False,indent=2),encoding='utf8')
print('REVIEW_PACKAGE_SAVED',[(s.name,s.frame_start,s.frame_end,s.render.fps,s.render.fps_base) for s in bpy.data.scenes],flush=True)
