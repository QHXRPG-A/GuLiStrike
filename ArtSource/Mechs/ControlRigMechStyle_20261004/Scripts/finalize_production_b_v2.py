"""Freeze only the final actual Blender review artifacts, never mutable review decisions."""
import json,hashlib,html
from pathlib import Path
from datetime import datetime,timezone
from PIL import Image
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004'); O=R/'Production_B_v2'
assert not (O/'production_manifest.json').exists(),'Do not overwrite a frozen B version'
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
expected={'A-v1':'93324b32cb12fb9273bdb3d7ad5c60ce59798655f30bb657f53cca7f18db4fb4','A-v2':'ea47e95d2e5ee1f8d2bfe3ccd1fd9b249873e99fe31d93f9c9935b72af1ed9ac'}
for version,digest in expected.items():
    path=R/f'References_{version.replace("-","_")}'/'reference_manifest.json'
    assert sha(path)==digest,version
    ref=json.loads(path.read_text(encoding='utf-8'))
    for item in ref['files']:
        file=R/item['path']; assert file.stat().st_size==item['bytes'] and sha(file)==item['sha256'],item['path']
b1=R/'Production_B_v1/production_manifest.json'
assert sha(b1)=='00fa36bd0bc046db39011d5fbbeeaf7d8442d95989c0ace028ed80bdee462853'
history=json.loads(b1.read_text(encoding='utf-8'))
for item in history['files']:
    file=R/item['path']; assert file.stat().st_size==item['bytes'] and sha(file)==item['sha256'],item['path']
approval=json.loads((R/'Approvals/Approval_A_v2_20261004.json').read_text(encoding='utf-8'))
render=json.loads((O/'production_render_report.json').read_text(encoding='utf-8'))
audit=json.loads((O/'native_geometry_audit.json').read_text(encoding='utf-8'))
contact=json.loads((O/'motion_contact_audit.json').read_text(encoding='utf-8'))
previews=json.loads((O/'AnimationPreviews/animation_preview_report.json').read_text(encoding='utf-8'))
blend=O/'ControlRigMech_B_v2_Production.blend'; blend_hash=sha(blend)
fidelity=json.loads((O/'AnimationPreviews/movie_frame_fidelity_report.json').read_text(encoding='utf-8'))
assert fidelity['passed'] and fidelity['checked_frames']==283 and fidelity['source_blend_sha256']==blend_hash
for clip in fidelity['clips']:
    assert clip['all_frames_match_source_within_lossy_codec_tolerance']
    assert sha(O/f"AnimationPreviews/ControlRigMech_B_v2_{clip['clip']}.mp4")==clip['mp4_sha256']
assert all(x['source_blend_at_render_sha256']==blend_hash for x in previews),'Motion previews must come from final saved Blender version'
assert all(x['geometry_skin_normals_sha256']==audit['lods'][0]['geometry_skin_normal_sha256'] for x in previews)
for view in ('Hero','Front','Left','Back'):
    with Image.open(O/f'ControlRigMech_B_v2_{view}.png') as im: assert im.size==(2048,2048); im.verify()
lods=render['lods']; rows=[]
for lod in lods:
    rows.append(f"| LOD{lod['lod']} | {lod['body_triangles']:,} | {lod['outline_triangles']:,} | {lod['total_triangles']:,} | {lod['caps']['total']:,} | +{lod['difference']['total']:,} | {lod['screen_size']:.2f} |")
standing=max(abs(v['difference_m']) for row in contact['poses'] if row['clip']!='Deploy' or row['time_s']==5 for v in row['feet'].values())
reduction=100*(1-lods[0]['body_triangles']/284700)
readme=f'''# ControlRig 机甲实际 Blender 候选 B-v2

A-v2 已由用户明确通过，依据为“审核通过，blender已开，根据参考图和源模型一比一制作”。[A 放行记录](../Approvals/Approval_A_v2_20261004.json)锁定清单 SHA256 `{expected['A-v2']}`。**当前 B-v2 待审核，预算与可迁移细线仍有差额；未导入正式 UE 目录。**

[可交互参考对照与动作](Review_B_v2.html) · [最终可编辑 Blender](ControlRigMech_B_v2_Production.blend) · [冻结清单](production_manifest.json) · [当前决定](../review_decisions.json)

最终 Blender SHA256：`{blend_hash}`。保持源四足、单主炮、活塞、足爪、软管、检修盖和原单侧天线；没有新增武器或玩法身份。按源部件与已审分色整理了 {audit['authoring_parts']} 个可编辑对象，保留 {audit['editable_mirror_modifiers']} 个经源几何核对的镜像修改器、两个按源剖面制作的 32 分段旋转轮廓、受控减面、源法线转移和足爪表面贴合修改器。主要圆形关节盖保留完整源轮廓。不能把这项工作描述为所有部件均从空白重拓扑。

## 效果、三视图和同机位参考

四张均为原生 2048 × 2048，姿态、正交相机、尺度和配色沿用已审 A-v2。

| 视图 | 审核 A-v2 | 实际 B-v2 | 实际遮罩＋描边壳 |
|---|---|---|---|
| 三分之四 | [参考](../References_A_v2/ControlRigMech_A_v2_Hero.png) | [成品](ControlRigMech_B_v2_Hero.png) | [材质预览](Portable/ControlRigMech_B_v2_Portable_Hero.png) |
| 正面 | [参考](../References_A_v2/ControlRigMech_A_v2_Front.png) | [成品](ControlRigMech_B_v2_Front.png) | [材质预览](Portable/ControlRigMech_B_v2_Portable_Front.png) |
| 左侧 | [参考](../References_A_v2/ControlRigMech_A_v2_Left.png) | [成品](ControlRigMech_B_v2_Left.png) | [材质预览](Portable/ControlRigMech_B_v2_Portable_Left.png) |
| 背面 | [参考](../References_A_v2/ControlRigMech_A_v2_Back.png) | [成品](ControlRigMech_B_v2_Back.png) | [材质预览](Portable/ControlRigMech_B_v2_Portable_Back.png) |

`REVIEW_B_ReferenceMatched` 渲染的是与生产 LOD0 **位置、拓扑、角法线和权重哈希一致**的实际网格，线稿使用 Blender 原生 Freestyle；其展示材质关闭图集内线，场景排除外轮廓壳。`PORTABLE_SHADER_LODS` 使用实际独立 2K 内线遮罩和预算内/带差额的反向法线壳，关闭 Freestyle。两者都是真实 Blender 结果。当前遮罩细线比已审参考偏软，局部窄倒角和关节盖仍有简化痕迹，不能将主展示的线稿精度当成已迁移材质的精度。正式 UE 交付前需继续处理该差异。

三档线性系数 0.42 / 0.74 / 1.0，阈值 0 / 0.12 / 0.55，固定艺术光向 (0.35, -0.55, 0.76)。灰青 `#557B78` 主装甲、铁锈红 `#8E3A2A` 炮口/检修护甲、深青灰 `#2C3735` 骨架/软管、沙米色 `#D5C09C` 金属盖/功能镜片，线色 `#1B2422`。真实装甲与装配线，不渲染三角网格线。

## 四档预算与对照

| 档位 | 本体三角面 | 描边三角面 | 合计 | 合计上限 | 合计差额 | 屏幕尺寸 |
|---|---:|---:|---:|---:|---:|---:|
{chr(10).join(rows)}

LOD0 本体比源 UE 284,700 三角面减少 {reduction:.2f}%。四档本体上限分别为 32,000 / 14,000 / 5,000 / 2,000；本体差额分别为 {', '.join(str(x['difference']['body']) for x in lods)}。LOD2 描边另超出 {lods[2]['difference']['outline']} 三角面。**未达到原预算，不记为预算通过；保留主要机构后按计划提交差额。**各档本体一个材质区段，LOD0–2 描边另一个，LOD3 没有描边，内部细线强度 1 / 1 / 0.6 / 0。

同机位对照：[LOD0](LODComparison/ControlRigMech_B_v2_LOD0_SameCamera.png) / [LOD1](LODComparison/ControlRigMech_B_v2_LOD1_SameCamera.png) / [LOD2](LODComparison/ControlRigMech_B_v2_LOD2_SameCamera.png) / [LOD3](LODComparison/ControlRigMech_B_v2_LOD3_SameCamera.png)。`ScreenIllustration` 仅为 Blender 正交比例示意，不冒充项目三档指挥官 UE 镜头或实际屏幕尺寸触发检查。

## 完整实际动作与结构核对

[部署](AnimationPreviews/ControlRigMech_B_v2_Deploy.mp4) · [待机](AnimationPreviews/ControlRigMech_B_v2_Idle.mp4) · [行走](AnimationPreviews/ControlRigMech_B_v2_Walk.mp4)。预览 1280 × 1280 / 15fps，逐帧从原 30fps 曲线取样，包含精确两端；分别为 76 / 131 / 76 帧，完整覆盖原 5 / 8.666667 / 5 秒，视频多保留一个末端显示帧。[逐帧视频核验](AnimationPreviews/movie_frame_fidelity_report.json)将全部 283 个解码帧与对应实际渲染 PNG 比较，均在 H264 有损编码容差内；这只核验视频重现，不代替全部骨骼姿态或穿插检查。未改动画速度、位移、伸缩或缩放。源 152 骨骼全部名称和父子关系核对一致；恢复了 FBX 默认导入转为对象的 root。金属分件刚性绑定，原软管和连接件保留必要混合权重。三个动作的真实全帧 UE 只读采样保存在 [AnimationSource](AnimationSource/capture_manifest.json)，原包未保存。

构建时三点姿态矩阵与源采样最大元素差 2.3842e-6；这只证明 Blender 中的采样姿态恢复，不代表 UE 导出回读已通过。部署 base 原 87.38→192.8959 cm 位移与 cannon_02 原 0.516426→1 缩放保留。

曾发现减面足爪下偏 2.6 cm，已按各自源足爪表面贴合后重渲染所有预览。[脚底对照](motion_contact_audit.json)记录 9 个关键姿态：末部署/待机/行走脚底相对源差最大 {standing*1000:.3f} mm，展开过程最大 {contact['max_foot_lowest_z_difference_m']*1000:.3f} mm；未改骨骼或抹除源部署变形。完整视频可查看活动件、软管和足爪运动，未声称自动覆盖所有时刻的穿插和接地。

保存回读核对四档无无权重顶点、无非有限数、无退化三角面、无同向重复或反向重合面；最大权重和误差约 1.37e-7。B-v1 的远档整体压面破坏装配、序列被编码成首帧重复，已由助手终检撤回；用户未审核或否定该版。B-v2 保留相同近景网格，远档改为保守角度整理和近景法线转移，保留全部源部件，预算差额明显增大；视频改用逐帧独立条带并解码比对。源尺寸约 9.2264 × 13.2333 × 6.8926 m，当前本体约 {' × '.join(f'{x:.4f}' for x in audit['body_dims_m'])} m。采样源表面距离 P95 {audit['sampled_nearest_source_surface_distance_m']['p95']*1000:.2f} mm、最大 {audit['sampled_nearest_source_surface_distance_m']['max']*1000:.2f} mm，范围限定于 LOD0 顶点采样，不替代整体装配/曲面视觉审核。详见 [原生几何审计](native_geometry_audit.json)。

## Blender 编辑、贴图与交付边界

打开文件默认显示实际成品渲染与可操作 3D 网格。[已打开的 Blender 截图](Blender_Live_B_v2.png)。原生模型为 Blender 5.2.2 LTS / EEVEE。编辑分件时切换 `PORTABLE_SHADER_LODS`，隐藏 `PRODUCTION_LODS_SINGLE_BODY_SECTION`，开启 `EDITABLE_MECHANICAL_PARTS` 的显示；源模型/法线辅助/原镜像右侧备份保持独立。修改分件后须重新生成对应 LOD/图集，展示副本不会自动重建。选择 Armature 后在 Action Editor 可切换三个 `ControlRigMech_Mech_*_Source30fps` 动作；源完整数据和转换脚本已保存。

四张 2048² PNG：[BaseColor](Textures/ControlRigMech_BaseColor_2K.png)、[独立内线](Textures/ControlRigMech_InternalLineMask_2K.png)、[功能遮罩](Textures/ControlRigMech_FunctionalMask_2K.png)、[ORM](Textures/ControlRigMech_ORM_2K.png)，均已打包到 .blend。BaseColor 不烘焙光照。当前 shader 用 `GuLi_PaletteLinear` 角点颜色保证微小岛分色准确，BaseColor 图集保留作可迁移数据；功能遮罩与 ORM 已生成但尚未完整接入展示 shader。RGBA8 未压缩四图合计 64 MiB，不等于 UE 压缩驻留预算或实战帧率。制作详情见 [渲染与预算记录](production_render_report.json)。

B 尚未取得用户通过；预算和可迁移细线仍需审核与处理。按用户计划、[制作技能](../../../../.agents/skills/guli-model-production/SKILL.md)及[美术规范 §2](../../../../Progress/RequirementDocument/GuLiStrike美术规范.md#2-制作与审核流程)的“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”流程，B 放行后再导出回读和正式建立 `/Game/GuLiStrike/Mechs/ControlRigMech`。本轮没有 UE 正式资源、ControlRig 副本、物理资产、挂点或玩法代码改动；原 source 无物理资产、挂点为 0。UE 镜头、ControlRig 副本运行、最终动画兼容、导出回读及实战帧率尚未验证。
'''
(O/'README.md').write_text(readme,encoding='utf-8')
cards=''
for view,label in [('Hero','三分之四'),('Front','正面'),('Left','左侧'),('Back','背面')]:
    cards+=f'''<article><h2>{label}：同姿态、同尺度</h2><div class="compare"><img src="../References_A_v2/ControlRigMech_A_v2_{view}.png" alt="已审 A-v2"><img class="after" src="ControlRigMech_B_v2_{view}.png" alt="实际 B-v2"><span class="label a">A-v2 已审参考</span><span class="label b">B-v2 实际网格</span></div><input aria-label="{label} A/B 对照分界" type="range" min="0" max="100" value="50" oninput="this.previousElementSibling.querySelector('.after').style.clipPath='inset(0 0 0 '+this.value+'%)'"><p><a href="ControlRigMech_B_v2_{view}.png">原生2K成品</a> · <a href="Portable/ControlRigMech_B_v2_Portable_{view}.png">实际遮罩/描边壳</a></p></article>'''
videos=''.join(f'<article><h2>{label}</h2><video controls loop preload="metadata" poster="AnimationPreviews/ControlRigMech_{clip}_Start.png" src="AnimationPreviews/ControlRigMech_B_v2_{clip}.mp4"></video><p>原动作 {duration} 秒；预览15fps，保留精确两端。</p></article>' for clip,label,duration in [('Deploy','部署与收缩','5'),('Idle','待机','8.666667'),('Walk','行走','5')])
budget=''.join(f"<tr><td>LOD{x['lod']}</td><td>{x['body_triangles']:,}</td><td>{x['outline_triangles']:,}</td><td>{x['total_triangles']:,}</td><td>{x['caps']['total']:,}</td><td>+{x['difference']['total']:,}</td></tr>" for x in lods)
lodcards=''.join(f'<article><h2>LOD{i}：实际材质与描边壳</h2><a href="LODComparison/ControlRigMech_B_v2_LOD{i}_SameCamera.png"><img src="LODComparison/ControlRigMech_B_v2_LOD{i}_SameCamera.png" alt="LOD{i}同机位"></a></article>' for i in range(4))
document=f'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ControlRig B-v2 实际 Blender 审核</title><style>body{{margin:0;background:#f7f2e8;color:#2c3735;font:16px/1.65 system-ui,"Microsoft YaHei",sans-serif}}main{{max-width:1440px;margin:auto;padding:28px}}h1{{font-size:28px}}h2{{font-size:20px}}a{{color:#8e3a2a}}.notice{{padding:16px;border-left:5px solid #8e3a2a;background:#e8ddc7}}.grid{{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:24px}}article{{background:white;border:1px solid #d5c09c;padding:12px;border-radius:8px}}img{{width:100%;display:block}}.compare{{position:relative}}.after{{position:absolute;inset:0;clip-path:inset(0 0 0 50%)}}.label{{position:absolute;top:8px;background:#2c3735;color:#f7f2e8;padding:4px 8px;font-size:13px}}.a{{left:8px}}.b{{right:8px}}input{{width:100%;accent-color:#8e3a2a}}video{{width:100%;background:#f7f2e8}}table{{border-collapse:collapse;width:100%;margin:18px 0}}td,th{{border-bottom:1px solid #d5c09c;text-align:right;padding:10px}}td:first-child,th:first-child{{text-align:left}}@media(max-width:800px){{.grid{{grid-template-columns:1fr}}main{{padding:15px}}}}</style><main><h1>ControlRig 四足机甲 · 实际 Blender 候选 B-v2</h1><p>A-v2 已通过；B-v2 待审核。四足、单炮、152骨骼、原部署/待机/行走；按已审四色制作。</p><p><a href="ControlRigMech_B_v2_Production.blend">可编辑 Blender</a> · <a href="README.md">制作与检查说明</a> · <a href="production_manifest.json">版本哈希</a></p><div class="notice">本页主图是真实生产网格的 Blender Freestyle 渲染；实际遮罩＋描边壳细线仍偏软，请同时查看链接。四档预算未达标，远档保留完整结构而预算差额较大，差额见下表。B 未通过，正式 UE 导入未开始。</div><div class="grid">{cards}</div><h2>完整源动作</h2><div class="grid">{videos}</div><h2>本体与描边分别计数</h2><table><tr><th>LOD</th><th>本体</th><th>描边</th><th>合计</th><th>合计上限</th><th>差额</th></tr>{budget}</table><p>LOD0 本体减少 {reduction:.2f}%；未删主要机构来强行达标。模型预算不代表实战帧率；UE三档镜头尚未验证。</p><div class="grid">{lodcards}</div><p>最终 Blender SHA256：<code>{blend_hash}</code>。审核决定见 <a href="../review_decisions.json">当前记录</a>。</p></main></html>'''
(O/'Review_B_v2.html').write_text(document,encoding='utf-8')
files=[blend,O/'README.md',O/'Review_B_v2.html',O/'production_render_report.json',O/'native_geometry_audit.json',O/'motion_contact_audit.json',O/'construction_report.json',O/'regular_caps_report.json',O/'Blender_Live_B_v2.png',R/'Approvals/Approval_A_v2_20261004.json']
files += [O/f'ControlRigMech_B_v2_{v}.png' for v in ('Hero','Front','Left','Back')]
for folder in ('Portable','Textures','LODComparison','AnimationSource','AnimationPreviews'):
    files+=sorted((O/folder).rglob('*'))
files=[f for f in files if f.is_file()]
for script in ('capture_full_source_animations.py','build_production_b_v1.py','refine_production_normals.py','rebuild_regular_joint_caps.py','prepare_production_atlas_lods.py','finish_review_scene_b_v1.py','build_safe_lods_b_v2.py','render_motion_b_v2.py','audit_production_b_v2.py','audit_motion_contact_b_v2.py','render_lod_comparison_b_v2.py','verify_movies_b_v2.py','audit_movie_fidelity_b_v2.py','finalize_production_b_v2.py'):
    files.append(R/'Scripts'/script)
entries=[]
for file in files:
    assert file.suffix.lower() not in ('.uasset','.umap','.uexp','.ubulk','.pak')
    entry={'path':file.relative_to(R).as_posix(),'bytes':file.stat().st_size,'sha256':sha(file)}
    if file.suffix.lower()=='.png':
        with Image.open(file) as im: entry['width'],entry['height']=im.size; im.verify()
    entries.append(entry)
manifest={'version':'B-v2','created_utc':datetime.now(timezone.utc).isoformat(),'art_revision':'1.2','stage':'actual_Blender_candidate_submitted_for_B',
          'A':'approved','approved_A_manifest_sha256':expected['A-v2'],'B':'pending','user_B_approval':None,'UE_formal_import':'not run',
          'source_mesh':'/Game/Assets/ControlRig/Characters/Mech/Meshes/SKM_Mech','source_control_rig':'/Game/Assets/ControlRig/Characters/Mech/Rigs/CR_Mech',
          'formal_target_after_B':'/Game/GuLiStrike/Mechs/ControlRigMech','final_blender_sha256':blend_hash,'final_geometry_audit':audit,
          'lod_budget_result':'over budget, differences and real comparisons submitted; no budget exception accepted yet','lods':lods,
          'main_image_line_method':'native Freestyle on actual production mesh','portable_shader_line_fidelity':'partial; softer internal lines',
          'UE_export_readback':'pending B approval','runtime_fps':'not measured','A_v1_and_A_v2_frozen_files_validated':41,
          'withdrawn_B_v1_frozen_files_validated':len(history['files']),'all_native_movie_frames_checked':fidelity['checked_frames'],
          'revises_version':'B-v1','revision_reason':'Restore structurally intact distant LODs and correct full-motion video encoding.','files':entries}
(O/'production_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
digest=sha(O/'production_manifest.json')
decisions=json.loads((R/'review_decisions.json').read_text(encoding='utf-8'))
decisions['B']={'status':'pending','submitted_version':'B-v2','manifest_path':'Production_B_v2/production_manifest.json','manifest_sha256':digest,
                'blender_sha256':blend_hash,'user_decision':None,'user_message_evidence':None,'budget_result':'over_budget_delta_submitted','portable_line_fidelity':'partial'}
(R/'review_decisions.json').write_text(json.dumps(decisions,ensure_ascii=False,indent=2),encoding='utf-8')
(R/'CURRENT.md').write_text(f'''# 当前成品候选：ControlRig 机甲 B-v2

A-v2 已明确通过，[放行记录](Approvals/Approval_A_v2_20261004.json)锁定版本。当前 [真实 Blender B-v2：效果、三视图、同机位参考、三个完整动作与四档差额](Production_B_v2/README.md)待用户审核。

[可编辑 Blender](Production_B_v2/ControlRigMech_B_v2_Production.blend) · [交互对照](Production_B_v2/Review_B_v2.html)。B 清单 SHA256 `{digest}`，最终 Blender SHA256 `{blend_hash}`。

本体预算、LOD2描边预算、可迁移细线仍有余项；未记为技术全通过。B 尚未放行，导出回读和正式 UE 目录未开始。

旧根 README 属于 A-v1 冻结历史，A-v1/A-v2 合计41个冻结文件核对未变。具体用户决定见 [review_decisions.json](review_decisions.json)。
''',encoding='utf-8')
print('B_V2_FROZEN',digest,'BLENDER',blend_hash,'FILES',len(entries))
