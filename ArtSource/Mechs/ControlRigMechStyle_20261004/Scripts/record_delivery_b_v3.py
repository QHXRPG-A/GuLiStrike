"""Update current progress and add one immutable B3 delivery archive."""
from pathlib import Path
import json,hashlib
P=Path('D:/UE5.7/test1');R=P/'ArtSource/Mechs/ControlRigMechStyle_20261004';O=R/'Production_B_v3'
manifest=O/'production_manifest.json';m=json.loads(manifest.read_text(encoding='utf-8'))
digest=hashlib.sha256(manifest.read_bytes()).hexdigest();blendhash=m['final_blender_sha256']
assert m['version']=='B-v3' and m['B']=='pending'
archive=P/'Progress/Archive/20261004-ControlRig机甲B_v3实际线稿与三档明暗.md'
assert not archive.exists(),'Do not overwrite an archived delivery'
req=P/'Progress/RequirementDocument/20261004-ControlRig机甲美术统一.md'
text=req.read_text(encoding='utf-8')
text=text.replace('next_action: 用户审核实际Blender候选B-v2及预算差额，B放行后再导出回读和正式UE交付。','next_action: 用户审核实际Blender候选B-v3的实时线稿、三档明暗和预算差额，B通过后再导出回读与正式UE交付。')
text=text.replace('status_note: 用户明确审核通过 A-v2 并要求在已打开的 Blender 按图和源模型一比一制作；B-v2待审核，预算与可迁移细线有差额，正式 UE 导入仍须 B 放行。','status_note: A-v2已通过；用户指出B-v2实际线稿不足，B-v3已接入真实结构线与轮廓并在当前Blender展示；B待审核，预算超标，UE未导入。')
old='当前 [真实 Blender B-v2](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v2/README.md)已交付2K效果/三视图、同机位参考、3个完整动作和保守四LOD差额；B-v1因助手终检问题撤回。B-v2未获用户通过，预算和可迁移细线未达目标，不提前正式导入。'
new='当前 [实际 Blender B-v3](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v3/README.md)已接入可旋转模型上的结构线、外轮廓和三档明暗，关闭Freestyle，并交付2K四图、同机位参考、线条开关对照与完整三个动作。用户指出B-v2实际线稿不足后修订为本版；旧冻结文件保留。B-v3待审核，四档预算及LOD0–2描边均超标，按计划提交差额，不提前导入UE。'
assert old in text;text=text.replace(old,new);req.write_text(text,encoding='utf-8')
dev=P/'Progress/DevelopmentDocumentation/20261004-ControlRig机甲美术统一.md'
text=dev.read_text(encoding='utf-8')
text=text.replace('summary: A-v2已获用户通过，实际Blender候选B-v2保留结构和152骨骼/三动作，交付2K三视图、同机位对照、完整视频和保守四LOD；预算及可迁移细线仍有差额。','summary: A-v2已通过，B-v3按用户反馈把结构线、轮廓及三档明暗接入实际模型，已显示于当前Blender；2K四图与真实完整三动作重新交付，B待审核且预算超标。')
text=text.replace('next_action: 用户审核实际B-v2并反馈预算/造型/线稿；未通过或要求修改时继续制作，B放行后才导出回读和正式UE交付。','next_action: 用户审核实际B-v3线稿、三档明暗和预算差额；继续处理修改，B通过后再导出回读和正式UE交付。')
text=text.replace('status_note: A-v2已通过，B-v1由助手终检撤回，当前B-v2待用户审核；四档预算不达标、可迁移细线偏软，UE正式副本和导出回读未开始。','status_note: A-v2已通过；B-v2按用户实时线稿反馈进入修订，当前B-v3待审核；四档本体和LOD0–2描边超标，UE未导入。')
text=text.replace('| B 实际成品 | B-v2 | **待审核** | 实际模型、四图和完整三动作已提交；四档预算和可迁移细线有差额，B-v1由助手终检撤回 |','| B 实际成品历史 | B-v2 | **用户要求线稿修订** | “所以线稿没搞？”与“三渲二，线稿，三档明暗”；旧冻结文件保留，未获B通过 |\n| B 当前实际成品 | B-v3 | **待审核** | 真实结构线、外壳与三档材质已在当前Blender显示，四图及三个完整动作重渲染；预算仍超标 |')
text=text.replace('为当前候选；[Blender 源]','为当时提交候选；[Blender 源]')
rows='\n'.join(f"| LOD{x['lod']} | {x['body_triangles']:,} | {x['outline_triangles']:,} | {x['total_triangles']:,} | +{x['total_delta']:,} |" for x in m['lods'])
section=f'''

## 2026-10-04 B-v3 实际线稿与三档明暗

用户连续指出“所以线稿没搞？”并明确“三渲二，线稿，三档明暗”。B-v2主展示使用Freestyle，实际shader内线未完成到同样效果；此前直接回答“做了”不准确。本版把选定装甲/装配边缘的距离字段与独立原生2K遮罩接入真实生产材质，并制作随原骨骼运动的反向轮廓壳。所有实际图、完整动作和材质预览均关闭Freestyle；不再用主展示图代替真实shader完成证据。

[B-v3完整交付与检查范围](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v3/README.md) · [实际线条开关/参考/动作对照](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v3/Review_B_v3.html) · [当前Blender截图](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v3/Blender_Live_B_v3.png)。当前文件默认 `STYLE_REALTIME_B_v3`，两个可旋转3D材质窗口；B-v3文件内旧Freestyle展示场景已移除。三档线性系数0.42/0.74/1、原四色分区与功能机构保留。

| 档位 | 本体三角面 | 描边三角面 | 合计 | 合计超出 |
|---|---:|---:|---:|---:|
{rows}

四档本体保持B-v2的全部顶点位置、三角表面、UV0、组名/权重、152骨骼名/父级/参考矩阵，保存回读严格核对。全部有效单位角法线，旧B-v2 LOD0–3的0/48/52/57个零角法线按表面重算；有效源法线在三角扇区重编码后最大方向差约0.789°、99百分位低于0.007°，未声称位级一致。当前LOD0–2描边更完整，均超预算；不删主要结构凑预算。逐角距离UV也会增加GPU顶点拆分，不能只看三角面或将Blender效果冒充UE性能通过。

全部283个实际动画帧按新shader/壳重渲染，1280²/15fps，完整部署/待机/行走及两端；逐帧解码与PNG对比通过，动作位移/伸缩/缩放保持。4张2K图纸沿A-v2同姿态/同尺度/正交相机，新增实际内线/外壳开关对照和四LOD同机位图。独立内线遮罩、BaseColor、功能遮罩、ORM已打包；颜色仍使用逐角颜色，功能/ORM未完整接入，UE等效材质须保留三个UV通道及颜色。

B-v3清单SHA256 `{digest}`；Blender SHA256 `{blendhash}`。A-v1/A-v2共41个冻结文件、B-v1共352个、B-v2共355个文件全量哈希核对未变。B-v2历史记为用户要求实时线稿修订，当前B-v3待审核，未把重申风格当作B通过。预算、UE导出回读/ControlRig副本/指挥官镜头及实战帧率尚未通过或执行；无UE源包、玩法身份、C++和战斗引用改动。全局规范仍v1.2，具体记录见[本轮归档](../Archive/20261004-ControlRig机甲B_v3实际线稿与三档明暗.md)。
'''
assert '## 2026-10-04 B-v3 实际线稿与三档明暗' not in text;text+=section;dev.write_text(text,encoding='utf-8')
archive.write_text(f'''---
schema: guli-progress/v1
id: ARC-20261004-011
work_id: WORK-20261004-002
kind: archive
role: root
title: ControlRig 机甲 B-v3 实际线稿与三档明暗
areas: [art, assets, rendering]
categories: [art]
status: recorded
verification: partial
created: '2026-10-04'
updated: '2026-10-04'
summary: 按用户实时线稿反馈修订B-v3，实际材质接入结构线/轮廓与三档明暗，当前Blender双3D预览可旋转；四图、真实三动作和预算差额已交付，B待审核。
next_action: 用户审核B-v3视觉与预算差额；B具体版本通过后才导出回读及正式UE交付。
relations:
  work_items: [WORK-20261004-002]
status_note: A-v2通过仍有效，B-v3待审核；全部本体及LOD0–2描边预算未满足，UE与实战帧率未验证。
art_revision: '1.2'
---

# 2026-10-04：ControlRig B-v3 实际线稿与三档明暗

用户指出“所以线稿没搞？”并再次明确“三渲二，线稿，三档明暗”。B-v2的主展示使用Freestyle，实际shader内线不足；此前直接回答“做了”不准确。当前[B-v3实际交付](../../ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v3/README.md)已将机械结构边缘距离/独立2K内线遮罩、反向骨骼壳、三档阶梯材质应用于真实模型，所有新图和动作关闭Freestyle。当前Blender显示两个可旋转3D材质窗口，原生截图与实际线条开关对照留档。

| 档位 | 本体三角面 | 描边三角面 | 合计 | 合计超出 |
|---|---:|---:|---:|---:|
{rows}

LOD0合计70,117，对40,000超出30,117；LOD1总65,006，对17,000超出48,006；LOD2总51,896，对6,000超出45,896；LOD3总38,428，对2,000超出36,428。四档本体和0–2描边均超标，主要结构没有被删减以凑数，实际同机位对比已提交。额外逐角UV字段还会增加GPU顶点拆分；不写为批量性能达标。

四档实际本体位置、三角表面、UV0、组名/权重及152骨骼名/父级/参考矩阵严格回读一致。旧B-v2 LOD1–3的48/52/57个零角法线重算，自定义法线扇区重编码后的有效源最大方向差约0.789°，99百分位低于0.007°，输出有效单位法线。原部署位移/伸缩/缩放保留，3个完整动作全部283帧按新材质重渲染，逐帧视频解码比对通过；不声称覆盖全部运动穿插。功能遮罩/ORM尚未完整接入，UE等效shader需要三UV与逐角颜色。

B-v3清单SHA256 `{digest}`；Blender SHA256 `{blendhash}`。A冻结41文件、B-v1冻结352文件、B-v2冻结355文件全部原哈希不变。审核决定将B-v2记为按用户反馈修订，B-v3待审核，A-v2原放行有效；没有补写用户B通过。规范v1.2、UE源包、C++、玩法/战斗引用均未改。导出回读、正式UE副本、ControlRig副本、UE三档指挥官镜头和实战帧率未执行。

## 资源清单

| 项目内文件夹 | 内容与来源 | 依赖及边界 |
|---|---|---|
| `ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v3` | 真实Blender、2K四图、线条开关、四LOD比较、打包四图、完整三个动作及检查报告 | 继承已审A-v2及B-v2实际本体；新UV/实际shader/壳，不从外部ZIP另迁移；未导入UE |
| `.../AnimationSource` | 原UE三个动画/152骨骼完整30fps采样的逐字节副本 | 原B-v2只读采样保留位移/缩放，不覆盖源资源 |
| `.../Scripts` | 新B-v3实际线材质/壳、保存回读、动作渲染、解码比对与冻结脚本 | 冻结B-v1/B-v2脚本未修改；新脚本另存 |

按用户计划及[制作技能](../../.agents/skills/guli-model-production/SKILL.md)的“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”，待B-v3具体版本决定再推进正式交付。当前事实与检查范围见[实施文档](../DevelopmentDocumentation/20261004-ControlRig机甲美术统一.md#2026-10-04-b-v3-实际线稿与三档明暗)。
''',encoding='utf-8')
ledger=P/'Progress/DevelopmentDocumentation/GuLiStrike美术规范.md';text=ledger.read_text(encoding='utf-8')
marker='## 2026-10-04 ControlRig A 放行与 Blender B-v2 候选'
assert marker in text and text.count(marker)==1
prefix=text.split(marker)[0]
# Replace only this task's mutable recent ledger entry with concise history and current facts.
tail='''## 2026-10-04 ControlRig A 放行与 Blender B-v2 候选

用户通过A-v2后提交B-v2：152骨骼、三动作，预算超标；Freestyle主展示未代表实际内线完成。283帧视频回读通过，未导入UE，详见[当时归档](../Archive/20261004-ControlRig机甲A放行与BlenderB_v2候选.md)。

## 2026-10-04 ControlRig B-v3 实际线稿

B-v3真实结构线、轮廓和三档明暗已在当前Blender材质预览显示，关闭Freestyle；四图与三个完整动作重交付。LOD0本体57,323/描边12,794，预算仍超标。B待审核、UE未导入、规范v1.2；见[当前记录](20261004-ControlRig机甲美术统一.md#2026-10-04-b-v3-实际线稿与三档明暗)与[归档](../Archive/20261004-ControlRig机甲B_v3实际线稿与三档明暗.md)。
'''
assert len((prefix+tail).encode('utf-8'))<=30*1024,'Keep this ledger within its split threshold'
ledger.write_text(prefix+tail,encoding='utf-8')
print('B3 progress recorded',archive.name,'LEDGER_BYTES',ledger.stat().st_size)
