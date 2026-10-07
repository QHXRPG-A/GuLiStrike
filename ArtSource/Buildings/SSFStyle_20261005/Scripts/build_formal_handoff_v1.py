"""Compose a formal storage handoff from completed API/readback/render evidence."""
import json, hashlib
from pathlib import Path

R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
D=R/'UE_Delivery_v1'
def read(name):return json.loads((D/name).read_text(encoding='utf8'))
im=read('ue_import.json');validation=read('ue_validation.json');mesh=read('ue_mesh_readback.json')
frozen=read('frozen_source_validation.json');visual=read('visual_qa.json')
for name,data in [('import',im),('reload/render',validation),('mesh readback',mesh),('frozen source',frozen),('visual review',visual)]:
    assert data['success'],name
assert validation['asset_count']==105 and not validation['source_dependencies']
assert len(validation['component_animation_checks'])==198 and len(validation['captures'])==54
assert len(im['assets'])==10 and len(im['animations'])==22 and len(mesh['checks'])==30
release=json.loads((R/'approval_B_import_20261006.json').read_text(encoding='utf8'))
assert hashlib.sha256((R/'Production_B_v1/SSF_Production_B_v1.blend').read_bytes()).hexdigest()==release['source_blend_sha256']
assert hashlib.sha256((R/'Production_B_v1/production_manifest.json').read_bytes()).hexdigest()==release['manifest_sha256']
max_pose=[max(c['all_bones_error_translation_cm_scale_angle_deg'][i] for c in validation['component_animation_checks']) for i in range(3)]
report={'success':True,'date':'2026-10-06','release':release,'target':im['target'],
        'engine':validation['engine'],'asset_count':105,'asset_classes':validation['asset_classes'],'meshes':10,'skeletons':7,'bones':355,
        'animations':22,'physics_assets':6,'textures':38,'master_materials':4,'material_instances':16,'drone_blueprints':1,'private_animation_compression_presets':1,
        'LOD_count':3,'screen_sizes':[1.,.10,.035],
        'max_UE_mesh_roundtrip_error_m':max(c['geometry_error_m'] for c in mesh['checks']),
        'max_UE_skin_weight_error':max(c['weight_error'] for c in mesh['checks']),
        'component_animation_LOD_samples':198,'max_component_pose_error_cm_scale_degrees':max_pose,
        'rendered_previews':54,'original_pack_dependencies':0,
        'gameplay_integrated':False,'maps_saved':[], 'source_package_preserved':True,'frozen_B_source_preserved':True,
        'budget_cap_differences_retained':24,'performance_tested':False,
        'animation_pose_baseline':'original editable RAW resampled at original runtime target rate, followed by local quaternion interpolation; original 1/2 FPS clips retained',
        'limits':['198 actual component samples cover start/middle/end of 22 clips at three LODs against the original runtime sampling baseline; they are not every-frame gameplay validation',
                  'Current B_v1 has 24 disclosed body-cap differences and partial outline hull coverage; original budgets remain unchanged',
                  'Distance previews use isolated unsaved Entry actors; battlefield composition, gameplay and combat FPS are outside this delivery'],
        'assets':im['assets'],'animations_detail':im['animations'],'saved_packages':im['saved_packages'],
        'evidence':{'reload_animations_render':'ue_validation.json','mesh_readback':'ue_mesh_readback.json',
                    'visual':'visual_qa.json','reference_pose':'reference_pose_repair.json','animation_precision':'animation_compression_precision.json','source_preservation':'frozen_source_validation.json'}}
(D/'formal_delivery.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
lines=['# SSF 建筑正式 UE 资源交付 v1','',
       '正式目录：`/Game/GuLiStrike/Buildings/SSFStylized`。',
       '用户已放行当前实际 Blender `SSF_Production_B_v1`，本轮只存放正式副本，暂不接入游戏。','',
       '共105个资源：10个网格（各三档LOD）、7套骨架/355骨骼、6个源物理资产副本、22个原名动画、38张贴图、4个母材质、16个材质实例、1个无人机蓝图副本和1个机械动画压缩设置。原商城包与冻结Blender文件保持。','',
       '## 使用入口','',
       '| 子目录 | 内容 |','|---|---|']
for a in im['assets']:
    count=sum(x['key']==a['key'] for x in im['animations'])
    description=('骨骼网格、独立骨架'+('、物理资产' if a['physics'] else '')+f'、{count}个动画') if a['type']=='SkeletalMesh' else '静态网格'
    if a['key']=='Drone':description+='、TB1_MoveDrone蓝图副本（原行为）'
    lines.append(f"| `{a['key']}` | {description}，配套材质/贴图 |")
lines += ['| `Shared` | 三档明暗/线稿、描边、半透明标识和灯片母材质，共用源遮罩、独立机械动画压缩设置 |','',
          '## 交付检查','',
          f"- 保存后独立重载105个资源，原商城包引用为0。新组允许Engine、ACL及现有Script插件依赖。",
          f"- 30档UE导出回读，最大几何差{report['max_UE_mesh_roundtrip_error_m']:.9g}m，权重差{report['max_UE_skin_weight_error']:.9g}；4套UV、法线、配色/LOD/Team数据保持。",
          f"- 22动画×3LOD×起/中/末姿态共198组实际组件对照；最大位置差{max_pose[0]:.9g}cm、缩放差{max_pose[1]:.9g}、旋转差{max_pose[2]:.9g}°。",
          '- 姿态基线为原可编辑动画按原运行采样率重采样后的局部四元数插值。原有1/2fps动作保持；与原压缩噪声的差异另行记录。',
          '- 原动画帧数、时长、原采样率和4条可编辑变换曲线保持；CommandCenter原`Destoy`/`Edle`名称保持。',
          '- 新组独立ACL设置使用0.001cm误差阈值和100cm虚拟顶点距离，保护建筑活动件的压缩精度；源ACL设置保持。骨架参考姿态按原资产恢复，未重做动画。',
          '- 54张实际UE截图：所有10资产×3LOD，以及六座建筑35m/25°、300m/55°、700m/55°和1500m/55°独立距离预览。总览是资源预览，尚未放入实战场景。',
          '- 固定艺术光向、三档因子0.40/0.72/1.00、独立内部线稿、近中景实际轮廓壳、远档淡出及Base Color/Team Color接口已在UE建立。','',
          '## 保留的边界','',
          '当前版本沿用已披露的24项本体面数差额，描边面数在上限内，部分外轮廓覆盖仍比参考Freestyle弱。此次是具体B_v1版本的正式存储放行，原预算并未全局上调，也不代表实战性能通过。',
          '未修改游戏关卡、兵种/建筑数据表、Gameplay蓝图或玩法代码；无人机蓝图副本随资源存储。源无人机没有指派物理资产，不额外伪造。','',
          '## 证据','',
          '[正式机器清单](formal_delivery.json) · [独立重载/动画/截图](ue_validation.json) · [UE网格回读](ue_mesh_readback.json) · [视觉检查](visual_qa.json) · [源文件保持](frozen_source_validation.json)',
          '', '[六座UE成品总览](Preview/UE_Buildings_LOD0_Contact.png) · [全部LOD比较](Preview/UE_AllAssets_3LOD_Contact.png) · [距离与自动LOD](Preview/UE_Distance_LOD_Contact.png)','']
(D/'README.md').write_text('\n'.join(lines),encoding='utf8')
print(json.dumps({'success':True,'asset_count':105,'max_pose_error':max_pose,'formal_target':im['target']},ensure_ascii=False))
