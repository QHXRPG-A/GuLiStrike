"""Finalize only after actual UE reload, poses, pixels and stored-mesh readback succeed."""
import json,hashlib,re
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');ROOT=R.parents[2];D=R/'UE_Delivery_Team_v2';O=R/'TeamPalette_B_v2_20261007'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def write(p,data):p.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
im=read(D/'ue_import.json');ue=read(D/'ue_validation.json');back=read(D/'ue_mesh_readback.json');pre=read(D/'preview_inventory.json');visual=read(D/'visual_qa.json');auth=read(R/'approval_B_v2_import_20261007.json')
assert im['success'] and ue['success'] and back['success'] and pre['success'] and visual['assistant_visual_review_performed']
assert ue['asset_count']==48 and len(ue['component_animation_checks'])==378 and len(ue['captures'])==84
assert len(back['checks'])==36 and not ue['unexpected_project_dependencies']
assert sha(Path(auth['source_blend']))==auth['source_blend_sha256']
frozen=read(O/'delivery_manifest.json');assert all(sha(R/e['file'])==e['sha256'] for e in frozen['files'])
candidate=read(O/'candidate_manifest.json');assert sha(Path(candidate['source_blend']))==candidate['source_blend_sha256']
write(D/'frozen_source_validation.json',{'success':True,'tracked_reviewed_files':len(frozen['files']),'B_v2_sha256':auth['source_blend_sha256'],'B_v1_source_unchanged':True,'user_release':str(R/'approval_B_v2_import_20261007.json')})
poses=[r['all_bones_error_translation_cm_scale_angle_deg'] for r in ue['component_animation_checks']]
stats={'UE_mesh_readback_cases':36,'max_geometry_error_m':max(r['geometry_error_m'] for r in back['checks']),
       'max_weight_error':max(r['weight_error'] for r in back['checks']),'animation_LOD_pose_cases':378,
       'max_component_pose_error_cm_scale_degrees':[max(row[i] for row in poses) for i in range(3)],
       'native_UE_captures':84,'new_resource_count':48}
classes={}
for a in ue['asset_inventory']:classes[a['class']]=classes.get(a['class'],0)+1
delivery={'success':True,'date':'2026-10-07','version':'SSF_TeamPalette_B_v2','blender_version':'5.2.2 LTS','UE_version':ue['engine'],
          'source_blend':auth['source_blend'],'source_blend_sha256':auth['source_blend_sha256'],
          'user_release_quote':auth['user_quote'],'target_roots':auth['team_roots'],'new_resource_count':48,'resource_classes':classes,
          'assets':im['assets'],'reused_dependencies':im['shared_dependencies'],'compatible_existing_building_animations':im['animations'],
          'old_formal_release_preserved':True,'Drone_and_22nd_animation_unchanged':True,'gameplay_integrated':False,'maps_saved':[],
          'verification':stats,'budget_note':'Original B_v1 body-cap/outline differences remain recorded; no global cap increase or gameplay/performance acceptance.'}
write(D/'formal_delivery.json',delivery)
readme=f'''# SSF 蓝红建筑正式 UE 资源 · B_v2

已按用户“导入至ue作为正式资源”放行当前实际 `SSF_TeamPalette_B_v2`，保存并独立重载 **48个新增资源**：12个骨骼建筑网格（各三档LOD）、24个材质实例、12张2K基础色图集。

| 阵营 | 正式目录 | 配色 |
|---|---|---|
| 蓝方 | `/Game/GuLiStrike/Buildings/SSFStylized/Blue` | 图1橙 / 蓝灰 / 奶油白 |
| 红方 | `/Game/GuLiStrike/Buildings/SSFStylized/Red` | 图2莓红 / 浅粉 / 橙 |

每队含 AirBase、CloningCenter、CommandCenter、MilitaryFactory、Reactor、StrategyCenter，分别有 `Meshes` / `Materials` / `Textures` 子目录，网格名 `SK_Blue_<Building>` / `SK_Red_<Building>`。

**原正式B_v1与商城包保留，暂不接入游戏。** 两队复用此前正式目录中已验证的6套兼容骨架、6个物理资产、21个建筑动画，以及共用三档/描边/显示母材质和各建筑线稿遮罩。无人机、平台、灯具与无人机第22个动画保持。因此使用两队资源时继续保留原 `SSFStylized` 的共用依赖目录；两队不再依赖原商城包。

## 实际 UE 外观与使用

[蓝红两队实际UE总览](Sheets/Blue_Red_UE_Overview.png) · [蓝方](Sheets/Blue_UE_Overview.png) · [红方](Sheets/Red_UE_Overview.png)

三档明暗、独立内部线稿、近中档真实描边壳、远档细线淡出、半透明标识和 `Base Color` / `Team Color` 接口保持。实际基础配色通过4套UV中的线性RGB传输到UE三档材质，2K基础色图集也随正式资源入库；这不是自动PBR导入。

屏幕尺寸为 `1.0 / 0.10 / 0.035`。全部12模型有原生2048px的三个实际LOD镜头，以及35m/25°、300–700m/55°和1500m/55°自动LOD资源预览，共84张。预览在未保存的临时Entry世界完成，不代表实战地图或实战帧率。

## 核对结果

- 独立进程重载48个资源，引用均落在正式 `SSFStylized` 组内及Engine/Script/ACL等允许依赖，原商城资源引用为0。
- 36档存储网格由UE导出回读，几何最大差 `{stats['max_geometry_error_m']:.9g} m`、权重最大差 `{stats['max_weight_error']:.9g}`；三档面数、4UV中的配色/淡出/队色数据和法线保持。
- 21原正式动画 × 2队 × 3LOD × 起/中/末，共378组实际组件对照；相对已保存B_v1实际组件，最大位置 / 缩放 / 旋转差为 `{stats['max_component_pose_error_cm_scale_degrees']}`，单位cm / 无量纲 / °。动画本身未修改。
- 来源Blender SHA256 `{auth['source_blend_sha256']}`，147个冻结审核文件仍保持，用户具体版本放行见[决定](../approval_B_v2_import_20261007.json)。

原[24项本体差额（含平台/无人机）](../Production_B_v1/Budget_Exceptions.md)及预算内描边壳的局部覆盖差异继续披露。两队建筑分别继承6建筑×3档的本体差额，本轮未重新减面、增加预算或开展玩法/联机/性能测量。

[正式机器清单](formal_delivery.json) · [保存重载/姿态/截图](ue_validation.json) · [存储网格回读](ue_mesh_readback.json) · [助手读图](visual_qa.json) · [原生图片哈希](preview_inventory.json) · [冻结源保持](frozen_source_validation.json)
'''
(D/'README.md').write_text(readme,encoding='utf8')
decisions=read(R/'review_decisions.json')
release={'version':'SSF_TeamPalette_B_v2','status':'released_for_formal_UE_storage','date':'2026-10-07',
         'source_blend_sha256':auth['source_blend_sha256'],'user_quote':auth['user_quote'],
         'authorization_record':'approval_B_v2_import_20261007.json','target_roots':auth['team_roots'],
         'UE_formal_import_performed':True,'new_assets':48,'formal_delivery':'UE_Delivery_Team_v2/formal_delivery.json',
         'gameplay_integration_authorized':False,'original_B_v1_preserved':True}
decisions.update({'current_version':'SSF_TeamPalette_B_v2','B':release,'team_palette_B_v2':release,
                  'formal_UE_current_version':'SSF_TeamPalette_B_v2 with preserved B_v1 shared dependencies',
                  'current_candidate_UE_import_performed':True,'formal_UE_import_authorized':True,
                  'formal_UE_delivery':'UE_Delivery_Team_v2/formal_delivery.json'})
decisions['history'].append(release);write(R/'review_decisions.json',decisions)
old=(R/'README.md').read_text(encoding='utf-8-sig');marker='## 历史参考阶段入口'
assert marker in old
header='''# SSF 建筑制作源目录

当前 **SSF_TeamPalette_B_v2** 已按用户“导入至ue作为正式资源”放行并完成正式入库。蓝方、红方各六座建筑，48个新增正式资源，三档LOD、线稿与三档明暗保持；原B_v1和商城包保留，暂不接入游戏。

- [当前正式资源交付与路径](UE_Delivery_Team_v2/README.md)
- [当前实际UE总览](UE_Delivery_Team_v2/Sheets/Blue_Red_UE_Overview.png)
- [正式机器清单](UE_Delivery_Team_v2/formal_delivery.json)
- [B_v2具体存储放行](approval_B_v2_import_20261007.json)
- [原提交Blender文件](TeamPalette_B_v2_20261007/SSF_TeamPalette_B_v2.blend)
- [此前B_v1正式交付及共用依赖](UE_Delivery_v1/README.md)

蓝方目录 `/Game/GuLiStrike/Buildings/SSFStylized/Blue`，红方目录 `/Game/GuLiStrike/Buildings/SSFStylized/Red`。复用原正式6骨架、6物理资产和21建筑动画，原无人机及第22个动作不改。新资源已独立重载与实际预览，原预算差额继续记录；原提交文件与当时待审记录为冻结历史，最新状态以本入口和决定登记为准。

'''
(R/'README.md').write_text(header+old[old.index(marker):],encoding='utf8')

EXPECTED={
'Progress/RequirementDocument/20261005-SSF建筑美术统一与三档LOD.md':'1228342a133f0c97e0b733a01aefb46dafb86365d189f4e072e8346e2adc0257',
'Progress/DevelopmentDocumentation/20261005-SSF建筑美术统一与三档LOD.md':'7aaef5baa88506e5e04cf9f986352584bf083409b4567e7be9b44791b2623da0',
'Progress/RequirementDocument/GuLiStrike美术规范.md':'ce5a9288d397f1e59bc192670a64288f3f9e25b132597234caef9c3dc274b7d2',
'Progress/DevelopmentDocumentation/GuLiStrike美术规范.md':'e4a10d0a18df74c8120c54a9dbb395d765d2760e9a9ebc46afc0bd26df11dbc6'}
for p,h in EXPECTED.items():assert sha(ROOT/p)==h,('re-read and merge concurrent change',p)
write(D/'progress_snapshot_before.json',EXPECTED)
def metadata(text,values):
    chunks=text.split('---',2);assert len(chunks)==3
    for key,value in values.items():
        chunks[1],count=re.subn(r'^'+re.escape(key)+r':.*$',key+': '+value,chunks[1],flags=re.M);assert count==1
    return '---'+chunks[1]+'---'+chunks[2]
latest=f'''\n\n## 2026-10-07 B_v2 蓝红阵营正式资源入库

用户看到实际Blender `SSF_TeamPalette_B_v2` 后明确“导入至ue作为正式资源”，已登记[本版本存储放行](../../ArtSource/Buildings/SSFStyle_20261005/approval_B_v2_import_20261007.json)，SHA256固定为 `{auth['source_blend_sha256']}`。这是当前可见版本的正式资源放行，暂不接入游戏，不扩大为全局预算或性能验收。

[正式交付](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/README.md)新增48资源：12骨骼网格各3LOD、24材质实例、12张2K基础色图集，分入 `/Game/GuLiStrike/Buildings/SSFStylized/Blue` 与 `/Game/GuLiStrike/Buildings/SSFStylized/Red`。六座建筑均有两队配色，原B_v1保留；复用原正式6兼容骨架、6物理资产、21建筑动画、共用三档/描边/半透明母材质和6线稿遮罩。原无人机、平台、灯具及第22个无人机动画保持。

导出前147冻结文件哈希核对保持；36个B_v2 FBX独立回读后完成导入，36档实际UE存储网格再次导出回读。最大几何差 `{stats['max_geometry_error_m']:.9g} m`、权重差 `{stats['max_weight_error']:.9g}`，实际三档面数、法线和4UV配色/队色/远档淡出传输保持。只保存本轮Blue/Red归属包，未覆盖旧正式资源或商城包。

[独立UE重载](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/ue_validation.json)检查48资产、资源引用、骨架参考姿态、材质接口与21建筑动画的两队/三LOD/起中末共378组实际组件对照，最大位置/缩放/角度差 `{stats['max_component_pose_error_cm_scale_degrees']}`（cm/无量纲/°）。84张实际2K UE图覆盖12模型×3档及35m/25°、300–700m/55°、1500m/55°独立资源镜头；[读图记录](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/visual_qa.json)与[原生图清单](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/preview_inventory.json)分别登记。预览使用未保存Entry世界，未接入游戏、改战场地图、运行PIE或做性能测试。

原24项本体差额（含平台/无人机）和真实描边局部覆盖差异继续记录；两队建筑各18档继承差额，不擅改主要结构或提高预算。上方B_v2提交时待审/未导入是历史事实，以本条具体版本放行与完成交付接续。规范仍v1.3，见[新增归档](../Archive/20261007-SSF建筑蓝红正式资源B_v2入库.md)。
'''
req=ROOT/'Progress/RequirementDocument/20261005-SSF建筑美术统一与三档LOD.md'
text=metadata(req.read_text(encoding='utf-8-sig'),{'summary':'已按用户放行将B_v2蓝红建筑48资源存入GuLiStrike，独立重载、三档LOD、动画姿态和UE画面核对完成，暂不接入游戏。',
    'next_action':'后续按具体任务决定进一步预算处理与游戏接入；当前两队正式资源可复用。',
    'status_note':'当前可见B_v2正式存储放行与交付完成；原B_v1依赖和源包保持，既有预算差额不转为全局规则。'})
req.write_text(text+latest,encoding='utf8')
dev=ROOT/'Progress/DevelopmentDocumentation/20261005-SSF建筑美术统一与三档LOD.md'
text=metadata(dev.read_text(encoding='utf-8-sig'),{'status':'done','verification':'partial',
    'summary':'B_v2两队六座建筑48新增正式资源已保存重载，36LOD回读、378动画姿态和84实际UE图完成，暂不接入游戏。',
    'next_action':'后续单独处理游戏接入或原预算差额；正式Blue/Red资源已可复用。',
    'status_note':'用户已放行当前B_v2正式存储，技术和实际视觉核对完成；原几何预算差额保持披露，不宣称实战性能通过。'})
old_line=next(line for line in text.splitlines() if line.startswith('当前为新增实际配色 **SSF_TeamPalette_B_v2'))
new_line='当前 **SSF_TeamPalette_B_v2已获正式存储放行并完成入库**：[正式交付与路径](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/README.md)。蓝红两队各六座、48新增资源；原B_v1保留并共享兼容骨架/物理/21建筑动画，暂不接入游戏。'
text=text.replace(old_line,new_line,1);dev.write_text(text+latest,encoding='utf8')
norm=ROOT/'Progress/RequirementDocument/GuLiStrike美术规范.md';text=norm.read_text(encoding='utf-8-sig')
text=text.replace('已打开、待具体成品审核，保留B_v1的几何、骨架、22原Action、线稿、三档明暗和三档LOD','已按用户“导入至ue作为正式资源”获得本版本存储放行并完成Blue/Red正式入库，保留B_v1的几何、骨架、22原Action、线稿、三档明暗和三档LOD',1)
text=text.replace('B_v1正式资源继续保留，未据旧放行更新B_v2到UE。','B_v1正式资源继续保留；B_v2新增48资源，复用已验证的骨架、物理资产与21建筑动画，独立重载和实际UE画面检查完成，见[本次交付](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/README.md)。暂不接入游戏，原预算差额保持。',1)
norm.write_text(text,encoding='utf8')
ledger=ROOT/'Progress/DevelopmentDocumentation/GuLiStrike美术规范.md';ledger.write_text(ledger.read_text(encoding='utf-8-sig')+latest,encoding='utf8')
archive=ROOT/'Progress/Archive/20261007-SSF建筑蓝红正式资源B_v2入库.md';assert not archive.exists()
archive.write_text(f'''---
schema: guli-progress/v1
id: ARC-20261007-002
work_id: ''
kind: archive
role: root
title: SSF建筑蓝红正式资源B_v2入库
areas: [art, assets, rendering]
categories: [art]
status: recorded
verification: partial
created: '2026-10-07'
updated: '2026-10-07'
summary: 用户放行当前B_v2后将蓝红两队48资源正式入库，独立重载、36LOD回读、378动画姿态和84UE图完成，不接入游戏。
next_action: 后续单独决定原预算差额处理或游戏接入；两队正式资源当前可复用。
relations:
  work_items: [WORK-20261005-003, WORK-20260917-001]
status_note: 具体可见B_v2的正式存储已完成，继承原几何/描边差异；技术和视觉检查不宣称预算全部达标或实战性能通过。
---

# 2026-10-07：SSF 蓝红两队正式资源入库

用户针对上一轮已展示实际Blender B_v2明确“导入至ue作为正式资源”，[放行记录](../../ArtSource/Buildings/SSFStyle_20261005/approval_B_v2_import_20261007.json)固定来源SHA256 `{auth['source_blend_sha256']}` 和147冻结审核文件清单。旧B_v1正式资源与原商城包保留，暂不接入游戏。

## 变更清单与资源清单

| 最终项目内文件夹 | 来源 | 内容与用途 | 依赖、示例与迁移边界 |
|---|---|---|---|
| `/Game/GuLiStrike/Buildings/SSFStylized/Blue` | 已审`SSF_TeamPalette_B_v2`蓝方六模型、三LOD FBX及图1调色2K图集 | 6骨骼网格、12材质实例、6图集，正式蓝方建筑 | 原正式6兼容骨架/物理资产/21建筑动画，共用母材质与线稿遮罩；不接玩法，无示例地图或外部Actor迁移 |
| `/Game/GuLiStrike/Buildings/SSFStylized/Red` | 同版本红方六模型、三LOD FBX及图2调色2K图集 | 6骨骼网格、12材质实例、6图集，正式红方建筑 | 同上；两队资源原商城依赖为0 |
| 既有`/Game/GuLiStrike/Buildings/SSFStylized/<Building>`与`Shared` | 前一轮已验证B_v1正式交付 | 继续提供6骨架、6物理资产、21建筑动作、3共用母材质及6线稿遮罩等原正式依赖 | 原105资源保持；无人机/平台/灯具和第22个动作不改，不移动或覆盖既有包 |
| `ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2` | 已审Blender、UE实际导出与渲染 | 36FBX、Expected基线、实际UE回读、84截图、图板及验证/交付记录 | 骨骼FBX回读需要带渲染环境；源blend与原提交保持，不作为玩法Map |

所有两队建筑各保留三档网格、半透明标识和近中档真实描边壳；线性配色通过4UV正确传入原正式三档着色，内部遮罩及Base Color/Team Color接口保留。新组共48资源。

## 导出、导入与实际验收

36个实际B_v2 FBX在Blender独立回读后导入；保存后由UE导出36个实际LOD回读，最大几何差 `{stats['max_geometry_error_m']:.9g} m`、权重差 `{stats['max_weight_error']:.9g}`，三档实际面数、UV颜色/队色/远档淡出、法线和原骨骼契约保持。

独立UE进程重载48资产，原商城引用为0。21不变建筑动画×2队×3LOD×起/中/末共378组实际组件对照，最大位置/缩放/旋转误差 `{stats['max_component_pose_error_cm_scale_degrees']}`，单位cm/无量纲/°，基线为已保存B_v1实际组件使用同一原正式动画。没有重建或改写动作，原低采样率、名称与压缩设置继续沿用。

84张原生2K截图覆盖12模型三档以及35m/25°、300–700m/55°、1500m/55°镜头；助手已查看实际UE图板。预览演员只存在于独立进程未保存Entry世界，未改战场地图、保存Gameplay引用或运行PIE。

[交付](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/README.md) · [机器清单](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/formal_delivery.json) · [保存重载/实际姿态/截图](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/ue_validation.json) · [UE存储网格回读](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/ue_mesh_readback.json) · [视觉记录](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/visual_qa.json)

## 执行记录与遗留边界

最初无渲染导入进程在首个骨骼网格FBX导出回读时触发UE MeshObject断言；已改为带渲染环境的独立进程完成全部导入和回读，旧正式包未保存改写。首次验证脚本的AnimSequence直接属性访问已改为支持的get_editor_property接口，随后独立检查通过，尝试日志保留在本交付目录。

原24项本体差额（包含平台/无人机）和描边壳局部覆盖弱于参考的差异继续记录；两队建筑各18档继承原差额，不自行增预算或进一步减面。规范v1.3保持；资源预算与编辑器预览不替代实战帧率、玩法和联机验收。既有规范/台账长文档可另按资产及审核历史拆分，本轮不自动拆分冷归档。
''',encoding='utf8')
write(D/'completion_summary.json',{'success':True,'stats':stats,'classes':classes,'archive_id':'ARC-20261007-002','gameplay_integrated':False})
print('SSF_TEAM_V2_FORMAL_HANDOFF_COMPLETE',json.dumps(stats),flush=True)
