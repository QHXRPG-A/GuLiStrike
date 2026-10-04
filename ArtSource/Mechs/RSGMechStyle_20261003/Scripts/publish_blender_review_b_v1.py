"""Publish and freeze the concrete Blender B-v1 review, without UE import."""
import hashlib
import json
from datetime import datetime,timezone,timedelta
from pathlib import Path

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT=ROOT/'Production_B_v1'
assert not (OUT/'review_manifest.json').exists(),'Review version is already frozen'
def read(name):return json.loads((OUT/name).read_text(encoding='utf-8'))
def link(path,label):return f'[{label}]({path.as_posix()})'
def picture(path,label):return '!'+link(path,label)
lods=read('lod_build_report.json')['LODs']
validation=read('review_technical_validation.json')
movies=read('animation_preview_report.json')['videos']
approval=json.loads((ROOT/'approval_A_v3_20261003.json').read_text(encoding='utf-8'))
assert approval['status']=='approved'
assert len(movies)==7
assert len(validation['movies'])==7
views=('Hero','Front','Left','Back')
names={'Hero':'三分之四','Front':'正面','Left':'左侧','Back':'背面'}
comparison=['# A-v3 与实际 Blender B-v1 同机位对照','',
    'A-v3 已审造型作为比例与结构基准。B-v1 唯一追加主配色为用户红框指定的头球正面凹嵌块天蓝；外围浅黄保留，球后方面板暖白。旧 A-v3 图纸保留原样。',
    '', '四图均为 2048 × 2048、正交相机、同一参考姿态和 13.246429m 视野高度。B 图采用实际减面网格、2K 线稿遮罩和几何描边，不使用 Freestyle。',
    '', '| 视角 | 已审 A-v3 | 实际 B-v1 |','|---|---|---|']
for view in views:
    a=ROOT/f'References_A_v3/RSG_A_v3_{view}_SourceStyle.png'
    b=OUT/f'ReviewImages/RSG_B_v1_{view}.png'
    comparison.append(f'| {names[view]} | {picture(a,"A-v3 "+names[view])} | {picture(b,"B-v1 "+names[view])} |')
(OUT/'ReferenceComparison.md').write_text('\n'.join(comparison)+'\n',encoding='utf-8')
rows=['| LOD | 本体 | 描边 | 合计 | 区段 | 屏幕阈值 |', '|---|---:|---:|---:|---|---:|']
for r in lods:
    rows.append(f'| {r["LOD"]} | {r["body_triangles"]:,} | {r["outline_triangles"]:,} | {r["total_triangles"]:,} | {r["body_sections"]}+{r["outline_sections"]} | {r["screen_size"]:.2f} |')
text=['# RSG 六足机器人 — Blender B-v1 实际成品待审核','',
    '已按通过的 A-v3 在 Blender 完成减面、可编辑分件、镜像、44 骨骼刚性绑定、四档 LOD、三档明暗与结构线。头球红框中的中央凹嵌面板已改天蓝，外围浅黄保留；误改的球后方面板已恢复暖白。',
    '',picture(OUT/'ReviewImages/RSG_B_v1_Hero.png','实际 Blender B-v1'),'',
    link(OUT/'RSGMech_Production_B_v1.blend','Blender 实际源文件')+' · '+link(OUT/'ReferenceComparison.md','与已审参考同机位对照')+' · '+link(OUT/'ReviewImages/RSG_B_v1_HeadDetail.png','头部凹块细节'),'',
    '四张成品图均 2048 × 2048：'+' · '.join(link(OUT/f'ReviewImages/RSG_B_v1_{v}.png',names[v]) for v in views),
    '', '## 七个原动作预览','',
    '真实成品 EEVEE 渲染，1080 × 1080、30fps。落地镜头随竖直位移跟踪，死亡镜头扩大视野；不改变原骨骼动作。视频包含首尾样本，比原动作区间多一帧播放时间。所有 MP4 已原生解码核对尺寸与帧数。','']
for name,label in [('Idle','待机'),('WalkForward','前行'),('WalkBackward','后行'),('Walk_L','左行'),('Walk_R','右行'),('Landing','落地'),('Death','死亡')]:
    text.append('- '+link(OUT/f'AnimationPreviews/A_FPS_Mech_{name}_01.mp4',label))
text+=['','## 四档预算与远景造型','']+rows+['',
    '各档本体只有一个材质区段，LOD0–2 另一个描边区段。LOD0/1/2 保留 392 个源组件；LOD3 保留 217 个主要分件，省略小硬件，保留六足、上下炮组、头球、背环与天线。LOD2/3 用封闭分件简化避免薄甲尖片；LOD1/2 描边严格复制对应本体表面并成对选择。',
    '', 'LOD1 内线强度 0.85；LOD2 头球/背环内线强度 0.45，新简化表面省略细线，保留主要色块和实际描边；LOD3 内线和描边均为 0。以下是 Blender 屏幕占比近似图，实际项目三档指挥官镜头仍在 B 放行后于 UE 验证。','',
    '| LOD | 放大检查 | 相对屏幕占比 |','|---|---|---|']
for lod in range(4):
    text.append(f'| {lod} | {link(OUT/f"LODPreviews/RSG_B_v1_LOD{lod}_Inspection.png","查看形体")} | {link(OUT/f"LODPreviews/RSG_B_v1_LOD{lod}_Screen.png","查看远景")} |')
text+=['','## 制作与绑定记录','',
    '编辑集合保留 208 个主分件、184 个镜像修改器及减面/骨架修改器；源集合只作隐藏对照。全尺寸比例取自原模型，外包尺寸差小于 6mm。恢复真正的 DeformationSystem 根骨骼，44 根骨骼名称和父子关系与原 UE 基线一致；所有成品顶点为单骨骼权重 1。原七动作已保存为 Actions 与默认静音的 NLA 轨道。',
    '', '动画查看：选 Armature，打开 Action Editor，选择 A_FPS_Mech 动作；当前默认无活动动作，展示参考姿态。切换 LOD 时只显示对应 03_LODn_Review 集合中的本体/描边。',
    '', '全 311 帧源姿态检查：最大骨骼位置差 0.021mm，刚性分件距离差 0.0063mm，六足最低点相对源模型最大变化 9.3mm。源动画自身相对零高度平面仍有接地偏差：待机约 -0.008m、落地 -0.172m、四向行走最低 -0.274m、死亡最低 -0.831m。本轮保持原动画，未更改 IK/曲线；这项记录不代表无穿地或游戏内接地验收通过。',
    '', '## 材质、贴图与版本','',
    '固定已审艺术光向，三档亮度乘数 0.42 / 0.74 / 1.0，阈值 0 / 0.12 / 0.55。2K BaseColor 为平色，未烘焙照明；独立 2K LineMask 的 R 为内部结构线、G 为琥珀功能灯遮罩。描边为独立壳材质；成品渲染不使用 Freestyle。LOD1–3 第二 UV 通道用于稳定取配色色块，内线仍从 UV0 独立读取，避免减面时串色。',
    '', link(OUT/'Textures/T_RSGMech_BaseColor.png','BaseColor 2K')+' · '+link(OUT/'Textures/T_RSGMech_LineMask.png','独立线稿/灯遮罩 2K'),
    '', '两图约 1.81MiB PNG，已打包进 .blend。按 RGBA8 保守估计，基础层合计 32MiB、完整 mip 链约 42.67MiB；实际 UE GPU 压缩和实战帧率待引擎交付验证。制作环境 Blender 5.2.2 LTS / UE5.7，导出回读和正式 UE 版本尚未产生。',
    '', link(ROOT/'approval_A_v3_20261003.json','A-v3 用户通过记录')+' · '+link(OUT/'color_amendment_confirmed_20261003.json','红框定位与改色记录')+' · '+link(OUT/'review_technical_validation.json','面数/区段/骨架/纹理/视频核对')+' · '+link(OUT/'motion_geometry_checks.json','全帧刚性与接地对照')+' · '+link(OUT/'review_manifest.json','B-v1 文件哈希清单'),
    '', '## 审核与后续','',
    '当前 B-v1 为实际成品待审核，没有登记用户 B 通过。通过后按原计划先导出回读，再新建 /Game/GuLiStrike/Robots/RSGMech 正式副本，原资源包保留，完成物理资产、七动画副本、等效 UE 着色与三档指挥官镜头验证。',
    '', '依据用户原计划以及 '+link(ROOT.parents[2]/'.agents/skills/guli-model-production/SKILL.md','模型制作技能')+' 与 '+link(ROOT.parents[2]/'Progress/RequirementDocument/GuLiStrike美术规范.md','美术规范')+' 中的“用户审核 B → UE 导入”，本次交付停在 B，不提前写入正式 UE 资源。','']
(ROOT/'README_B_v1.md').write_text('\n'.join(text),encoding='utf-8')
visual={
    'review_version':'B-v1','actual_B_viewed':['Hero','Front','Left','Back','HeadDetail'],
    'same_camera_reference':'A-v3','source_proportions_and_six_leg_assembly':'preserved with budgeted simplification',
    'color_amendment':'user-marked front head recessed panel SkyBlue; PaleYellow surrounding shell; rear hood restored WarmWhite',
    'LODs_viewed':[0,1,2,3],'animations_inspected':'start/middle/end render samples plus all-frame pose/foot/rigid checks',
    'source_contact_deviation_known':True,'UE_actual_commander_camera_checked':False,'B_user_decision':'pending'}
(OUT/'review_visual_checks.json').write_text(json.dumps(visual,ensure_ascii=False,indent=2),encoding='utf-8')
files=[ROOT/'README_B_v1.md',OUT/'RSGMech_Production_B_v1.blend',ROOT/'approval_A_v3_20261003.json',OUT/'ReferenceComparison.md']
files+=list((OUT/'ReviewImages').glob('*.png'))+list((OUT/'LODPreviews').glob('*.png'))
files+=list((OUT/'AnimationPreviews').glob('*.mp4'))+list((OUT/'AnimationPreviews').glob('*.png'))
files+=list((OUT/'Textures').glob('*.png'))+list((OUT/'UserAnnotations').glob('*.png'))
files+=[OUT/name for name in ['geometry_build_report.json','atlas_build_report.json','lod_build_report.json',
    'animation_binding_report.json','animation_preview_report.json','lod_preview_report.json','motion_geometry_checks.json',
    'review_technical_validation.json','review_visual_checks.json','color_amendment_confirmed_20261003.json']]
scripts=['build_production_geometry.py','apply_head_recess_color.py','bake_production_atlas.py','build_production_lods.py',
    'capture_source_animations_blender.py','bind_original_animations.py','render_production_review.py',
    'render_animation_reviews.py','inspect_production_motion.py','render_lod_reviews.py','validate_review_bundle_blender.py',
    'publish_blender_review_b_v1.py']
files +=[ROOT/'Scripts'/name for name in scripts]
manifest={'version':'B-v1','published_at':datetime.now(timezone(timedelta(hours=8))).isoformat(),
    'A':'A-v3 approved','B':'pending user review','approved_reference_untouched':True,
    'formal_UE_assets_saved':False,'artifacts':[]}
for path in sorted(set(files)):
    assert path.is_file(),str(path)
    manifest['artifacts'].append({'path':path.relative_to(ROOT).as_posix(),'bytes':path.stat().st_size,
                                 'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
(OUT/'review_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'published_version':'B-v1','artifacts':len(files),'B':'pending','UE_imported':False}),flush=True)
