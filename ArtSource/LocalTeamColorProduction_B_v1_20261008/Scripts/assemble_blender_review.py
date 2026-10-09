import bpy,json,sys,math
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).parent))
import build_palette_only as common
O=common.O
names={'DefaultSoldier':'先驱号','WM01':'重防号','BiZhiMao':'彼之矛','ShieldGenerator':'护盾发生器','ManualOutpost':'前哨建筑','MissileTurret':'防空炮','SentryTurret':'哨戒炮','ResourceFactory':'矿厂','SSF_AirBase':'空军基地','SSF_CloningCenter':'克隆中心','SSF_CommandCenter':'指挥中心','SSF_MilitaryFactory':'军工厂','SSF_Reactor':'反应堆','SSF_StrategyCenter':'战略中心'}
bpy.ops.wm.read_factory_settings(use_empty=True)
overview=bpy.context.scene;overview.name='00_十四模型蓝红成品_B待审核';common.scene_settings(overview)
overview.render.resolution_x=2600;overview.render.resolution_y=2050
gray=bpy.data.materials.new('Review_Label');gray.use_nodes=True
gray.node_tree.nodes.clear();e=gray.node_tree.nodes.new('ShaderNodeEmission');e.inputs[0].default_value=common.color('Gray','Blue');out=gray.node_tree.nodes.new('ShaderNodeOutputMaterial');gray.node_tree.links.new(e.outputs[0],out.inputs[0])
font=bpy.data.fonts.load('C:/Windows/Fonts/msyh.ttc') if Path('C:/Windows/Fonts/msyh.ttc').exists() else None
manifest={'version':'PaletteOnly_B_v1','models':[],'B_approval':'pending','model_geometry_modified':False,'display_instances_only_normalized':True}
def label(text,position,size):
    d=bpy.data.curves.new(text,'FONT');d.body=text;d.align_x='CENTER';d.size=size
    if font:d.font=font
    o=bpy.data.objects.new('审核标签_'+text,d);overview.collection.objects.link(o);o.location=position;o.data.materials.append(gray);return o
labels=[]
for i,key in enumerate(common.ALL_KEYS):
    p=O/'Models'/(key+'_PaletteOnly_B_v1.blend')
    report=json.loads((O/'Reports'/(key+'_production.json')).read_text(encoding='utf8'))
    with bpy.data.libraries.load(str(p),link=False) as (src,dst):
        dst.scenes=[team+'_'+key for team in ('Blue','Red')]
        dst.actions=[n for n in report['original_animation_signatures'] if n not in bpy.data.actions]
    for a in dst.actions:
        if a:a.use_fake_user=True
    scenes={s['team']:s for s in dst.scenes}
    cell=Vector(((i%4)*16,-(i//4)*13,0))
    for team,offset in [('Blue',-3.7),('Red',3.7)]:
        scene=scenes[team];rec=next(r for r in report['teams'] if r['team']==team)
        col=scene.collection.children.get(rec['collection']);assert col
        scale=6/max(b-a for a,b in zip(rec['view']['lo'],rec['view']['hi']))
        inst=bpy.data.objects.new(f'{i+1:02d}_{names[key]}_{"蓝方" if team=="Blue" else "红方"}',None)
        inst.instance_type='COLLECTION';inst.instance_collection=col
        inst.scale=(scale,)*3;center=Vector(rec['view']['center']);lo=Vector(rec['view']['lo'])
        inst.location=cell+Vector((offset,0,0))-Vector((center.x,center.y,lo.z))*scale
        inst['review_display_instance']=True;inst['source_model_transforms_unchanged']=True;overview.collection.objects.link(inst)
        labels.append(label('蓝方' if team=='Blue' else '红方',cell+Vector((offset,-3.8,.2)),.55))
    labels.append(label(f'{i+1:02d} {names[key]}',cell+Vector((0,-5.1,.2)),.8))
    manifest['models'].append({'id':key,'name':names[key],'file':str(p),'scenes':[s.name for s in scenes.values()],'native_views':['Hero','Front','Left','Back'],'reference':'A_v3'})
bpy.context.window.scene=overview;bpy.context.view_layer.update()
cam=bpy.data.objects.new('Overview_Camera',bpy.data.cameras.new('Overview_Camera'));overview.collection.objects.link(cam);overview.camera=cam
center=Vector((24,-19,2.5));cam.location=center+Vector((0,-70,90));cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.type='ORTHO';cam.data.ortho_scale=72
for o in labels:o.rotation_euler=cam.rotation_euler
labels.append(label('14 模型 / 蓝红配色成品 · B 待审核',Vector((24,8,1)),1.1));labels[-1].rotation_euler=cam.rotation_euler
labels.append(label('仅配色与三渲二线稿 · 原几何 / UV / 骨架 / 动画 / LOD 保留',Vector((24,6,1)),.63));labels[-1].rotation_euler=cam.rotation_euler
for screen in bpy.data.screens:
    for a in screen.areas:
        if a.type=='VIEW_3D':
            a.spaces.active.shading.type='MATERIAL';a.spaces.active.overlay.show_overlays=False
            a.spaces.active.region_3d.view_location=center;a.spaces.active.region_3d.view_distance=85;a.spaces.active.region_3d.view_rotation=cam.rotation_euler.to_quaternion()
readme=bpy.data.texts.new('00_审核说明');readme.write('十四模型蓝红实际配色成品 B_v1，全部待用户审核。\n总览只使用集合实例调整展示尺寸；原模型对象保持原几何与变换。\n右上场景菜单选择 Blue_ / Red_ + 模型英文标识，逐个旋转审核。\n模型材质为三档明暗，补充线稿由 Freestyle 基于原网格渲染；F12 查看完整线稿。\n对应效果图与正/左/后三视图在 Previews；reference-comparison.html 为 A/B 对照。\n护盾顶部三个点使用 TeamLightLamp，固定区两队一致。\n此文件不代表 B 通过；正式 UE 资源没有更新。\n')
try:bpy.ops.file.pack_all()
except RuntimeError:pass
bpy.ops.wm.save_as_mainfile(filepath=str(O/'PaletteOnly_14Models_BlueRed_B_v1.blend'))
overview.render.filepath=str(O/'Previews/Overview_BlueRed_B_v1.png');bpy.ops.render.render(write_still=True)
(O/'review-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
print('BLENDER_REVIEW_14_MODELS_28_VARIANTS_READY',flush=True)
