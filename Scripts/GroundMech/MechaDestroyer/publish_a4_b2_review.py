"""Refresh the working review entry without declaring B approval."""
from pathlib import Path
import json, hashlib, shutil

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2'
source=Path('C:/Users/a/AppData/Local/Temp/codex-clipboard-08c405e9-8abd-427c-a32f-3cc41e0ef8a7.png')
destination=OUT/'References/User_B2_ChestParts.png'
if source.is_file():shutil.copy2(source,destination)
notes=json.loads((OUT/'revision_notes.json').read_text(encoding='utf8'))
feedback='这两块，参考图上是两个部件，现在只是两条线'
if feedback not in notes['user_feedback']:notes['user_feedback'].append(feedback)
if destination.is_file() and not any('User_B2_ChestParts.png' in x['path'] for x in notes['files']):
    notes['files'].append({'source':str(source),'path':'References/User_B2_ChestParts.png','sha256':hashlib.sha256(destination.read_bytes()).hexdigest()})
notes['latest_changes']=[
    'Independent closed chest armor parts with side walls, bevels and seated mounting backs',
    'Continuous volumetric chest shell supporting all attached armor',
    'Single shoulder housing with octagonal side section and recessed bearing',
    'Corrected elbow height, cuff volume and upper-arm side-plate seating',
    'Thick thigh/calf profiles and fitted rear armor, knee cap mounting brackets',
    'Hip blue side armor recessed into the hip, with the sand tab behind it',
    'Instep footprint rebuilt from both front and side outlines',
    'Gun spin axes through actual barrel centerlines; original names and hierarchy kept',
    'Three tone materials and camera-only contour shells'
]
notes['status']='in_progress';notes['approval_B']='changes_requested';notes['completion_claim']=False
notes['remaining_work']=['Continue silhouette, assembly and component-facet fidelity review against A_v4',
                         'User B approval has not been given',
                         'B2 animation retarget and formal UE integration are not complete']
heel_file=OUT/'heel_connection_revision.json'
heel=json.loads(heel_file.read_text(encoding='utf8')) if heel_file.exists() else None
if heel:
    heel_feedback='脚下这两部分目前是分开的，需要处理下'
    if heel_feedback not in notes['user_feedback']:notes['user_feedback'].append(heel_feedback)
    heel_reference=OUT/'References/User_B2_HeelGap.png'
    if heel_reference.exists() and not any('User_B2_HeelGap.png' in x['path'] for x in notes['files']):
        notes['files'].append({'source':'C:/Users/a/AppData/Local/Temp/codex-clipboard-b0b5a81a-d9f4-442d-be68-1c26f7b27c4e.png',
                               'path':'References/User_B2_HeelGap.png','sha256':hashlib.sha256(heel_reference.read_bytes()).hexdigest()})
    notes['latest_changes'].insert(0,'Extended both solid foot bridges into the heels; shared red/blue geometry and existing foot bones retained')
    notes['latest_revision']='heel_connection_20261023'
    notes['latest_revision_report']='heel_connection_revision.json'
    notes['latest_revision_visual_QA']='No assistant output-image inspection; current AGENTS.md boundary followed. User review pending.'
    notes['visual_comparison_authorization']='Earlier user request asked for finding differences. Current heel repair follows the later AGENTS.md instruction: structural checks only, output visual review by user.'
for filename,feedback,reference,source_path,change in [
    ('back_assembly_revision.json',None,None,None,'Recessed the central back saddle below the cooling fins, placed narrow side walls outside the blue covers, rebuilt three back panels and added rear pod faces from A4'),
    ('integrated_heel_revision.json','这两块东西很丑（用户附图标注）','User_B2_HeelBlocks.png','C:/Users/a/AppData/Local/Temp/codex-clipboard-bb3aab6a-80fe-4574-b61c-4c84d5d65c2d.png','Replaced paired rear heel cuboids with one compact chamfered wedge per foot; seated the instep tail inside it'),
    ('tapered_toes_revision.json','这里也是，方方正正的不好看','User_B2_SquareToes.png','C:/Users/a/AppData/Local/Temp/codex-clipboard-1837367f-acf9-4d5e-9ecc-742c09b0c94e.png','Retained paired front toes but shortened and tapered their tips, sloped the front caps and seated the sand panels on the new surfaces'),
]:
    path=OUT/filename
    if not path.exists():continue
    revision=json.loads(path.read_text(encoding='utf8'))
    notes['latest_revision']=revision['revision'];notes['latest_revision_report']=filename
    notes['latest_changes'].insert(0,change)
    if feedback and feedback not in notes['user_feedback']:notes['user_feedback'].append(feedback)
    if reference and not any(reference in x['path'] for x in notes['files']):
        ref=OUT/'References'/reference
        notes['files'].append({'source':source_path,'path':'References/'+reference,'sha256':hashlib.sha256(ref.read_bytes()).hexdigest()})
annotation_path=OUT/'annotation_revision.json'
annotation=json.loads(annotation_path.read_text(encoding='utf8')) if annotation_path.exists() else None
if annotation:
    notes['latest_revision']=annotation['revision'];notes['latest_revision_report']=annotation_path.name
    notes['latest_changes']=[
        'Both machine guns are 1.5x their previous axial length; width and caliber retained, both team muzzle sockets moved forward',
        'Gun modules have dedicated editable collections, recessed mounting couplings and a clear housing/cuff boundary',
        'Gun and upper-arm rectangular panels replaced with fitted chamfered plaques',
        'Rear arm frames tucked toward the elbow, lower gun cradles tightened to the housing',
        'Hip/knee/shin side guards rebuilt as fitted thick beveled parts; inner kneecap and bracket shortened',
        'Upper boot, lower armor ledge and compact heel separated by recessed mechanisms; physical connections retained'
    ]
    notes['latest_revision_visual_QA']='No assistant output-image inspection; user review pending.'
    notes['visual_comparison_authorization']='User annotated inputs and approved references read as design instructions. Current prohibition on assistant output-image testing remains in force.'
    for item in annotation['user_feedback']:
        if item['text'] not in notes['user_feedback']:notes['user_feedback'].append(item['text'])
        if not any(x['path']==item['path'] for x in notes['files']):notes['files'].append(item)
gun_detail_path=OUT/'gun_detail_revision.json'
gun_detail=json.loads(gun_detail_path.read_text(encoding='utf8')) if gun_detail_path.exists() else None
if gun_detail:
    notes['latest_revision']=gun_detail['revision'];notes['latest_revision_report']=gun_detail_path.name
    if gun_detail['user_request'] not in notes['user_feedback']:notes['user_feedback'].append(gun_detail['user_request'])
    notes['latest_changes']=[
        'Added a faceted octagonal armor bezel seated around the circular muzzle socket',
        'Added a separate raised rear top cover and a small seated mechanical latch',
        'Raised blue side cheek armor frames shorter fitted cream front plaques',
        'Muzzle rebuilt with three axial steps, recessed ring necks and a chamfered recessed bore',
        'Amber core seated at the bore recess floor; muzzle sockets remain unchanged',
        'Lower gun cradle rebuilt with broad open trapezoidal rails from A_v4; tucked rear arm frame unchanged'
    ]
    notes['preserved_latest_constraints']=['1.5x axial length','Independent weapon modules','Existing palette and three tones','Bone names, drivers, actions and muzzle sockets','All non-weapon geometry']
(OUT/'revision_notes.json').write_text(json.dumps(notes,ensure_ascii=False,indent=2),encoding='utf8')
approval=json.loads((OUT/'approval_A.json').read_text(encoding='utf8'))
approval['approval_B']='changes_requested';approval['B_revision']='B_v2 in progress'
approval['B_feedback']=notes['user_feedback']
if gun_detail:approval['latest_B_refinement_request']=gun_detail['user_request']
if annotation:
    approval['A4_user_amendment']={'date':'2026-10-23','source':'Six annotated user inputs',
        'instructions':[x['text'] for x in annotation['user_feedback']],
        'gun_length_factor':1.5,'base_reference_images_unchanged':True,'does_not_grant_B_approval':True}
(OUT/'approval_A.json').write_text(json.dumps(approval,ensure_ascii=False,indent=2),encoding='utf8')
g=json.loads((OUT/'geometry_report.json').read_text(encoding='utf8'))
blend=OUT/'GroundMech_A4B2_Editable_B_v2.blend'
readme=f'''# 地面机甲 A_v4 — Blender B_v2 修订中

A_v4 为基础，叠加用户本轮双枪加长50%等六处明确修订。B_v1 已被用户指出“不一致”，当前 B_v2 继续修订，**未通过 B 审核，未宣称三视图无差异**。

当前源文件：[GroundMech_A4B2_Editable_B_v2.blend](GroundMech_A4B2_Editable_B_v2.blend)。历史 B_v1 和原 MechaDestroyer 资源保留。

## 最新：按参考补充枪体细节

用户要求“枪的细节再多一点，跟参考图对齐”。以A_v4整机与上半身参考中的武器结构为依据，新增前端八角装甲护圈、独立凸起顶盖与连接扣、侧面队色护甲；乳白片缩为前部切角面板并叠装在侧甲上。枪口增加三段台阶、内收环槽、端面倒角和内凹枪膛，橙色内芯落在枪膛底座上；下托架改为有宽折面的镂空梯形框。

左右枪和两队实例同步。保留1.5倍总长、原枪口直径和挂点、炮壳／旋转枪管的既有骨骼归属；此前收拢的后臂架及非枪体几何保持。见[枪体细节修改记录](gun_detail_revision.json)，含改前完整备份与28处实体连接核对。当前Blender定位可编辑枪体近景；外观由用户审核。

![当前枪体细节](Previews/07_WeaponAssembly.png)

## 此前六张标注修订

- 双枪沿枪轴从1.345 m加长至2.0175 m（1.5倍），口径保持；枪口插槽向前0.6725 m，双侧和红蓝实例同步。
- 枪体进入独立可编辑 `Weapon_Module_R/L` 集合，后方增加内收的安装接头，外壳与肘部护套分开；壳体仍绑定 `GunArm`，旋转枪管仍绑定 `Gun`。
- 枪侧及上臂矩形块改为贴合外壳斜面的切角实体片；后侧支架收向肘部，下部枪架贴近枪体。
- 髋、膝、胫部侧甲按参考轮廓重建厚度、斜角和包覆面；内侧膝甲与支座收短。
- 脚背上层靴甲与下层队色甲之间留出至少0.06 m的高度分层，深色支承连接脚踝、脚掌、双趾和后跟；后跟与前趾的既有收尖轮廓保留。

见[本轮实体修改与检查](annotation_revision.json)。修改前保留含未保存状态的完整备份；骨骼、驱动器及其他网格摘要未变。外观由用户在 Blender 审核。

![本轮枪体与安装结构](Previews/07_WeaponAssembly.png)

![本轮腿部与脚部装配](Previews/08_LegAssembly.png)

## 此前脚部轮廓修订

用户先指出后跟断开，随后指出后跟两块方块难看、前脚趾也过于方正。此前改为每脚一个切角楔形后跟，宽度0.94→0.74 m，向后延伸0.64→0.48 m；当时连接块与后跟搭接0.26 m，现由上述内收深色支承接续连接。前脚掌双趾前端宽度0.55→0.32 m、前伸1.65→1.55 m，前端斜切并收尖，米色嵌板贴合新斜面。

[一体后跟记录](integrated_heel_revision.json)与[楔形脚趾记录](tapered_toes_revision.json)包含改前备份、闭合几何、刚性权重及装配共同内部点。后跟和连接块仍绑定 `Foot_R/L`，脚趾及嵌板仍绑定 `FootEnd_R/L`；红蓝共用网格同步。原[断开修复](heel_connection_revision.json)保留历史，其后跟分件已被本版替代。没有助手输出读图测试，尚未取得 B 放行。

![当前一体楔形后跟](Previews/05_HeelAssembly.png)

![当前收尖双趾](Previews/06_ToeAssembly.png)

## 背部装配修订

中央支撑收至散热片下方，侧壳移至队色背甲外侧；按 A_v4 背面轮廓重建三段深灰背板和两块队色护甲，并补上顶部圆筒后端的圆环与橙色中心。数据与备份见[背部装配记录](back_assembly_revision.json)。参考还原度仍待用户审核。

## 本次胸甲修订

用户指出胸前两块在参考中是独立部件、当前只像两条线。已改为 `GMA4B2_Chest_IndependentArmor_R/L` 两个独立封闭网格，具有侧壁和宽倒角；名义厚度0.18 m，前面高出安装底座0.117 m，背面压入底座，仍刚性绑定 `cockpit`。

![实际 Blender 胸甲装配](Previews/04_ChestAssembly.png)

## Blender 入口

- `GroundMech_A4B2_Construction`：实际可编辑蓝方分件、修改器、骨架与挂点；胸甲可以分别选择。
- `GroundMech_A4B2_01_TeamReview`：同几何的蓝／红实例。
- `GroundMech_A4B2_02_ThreeViews`：正／侧／背两队六视图。
- `GroundMech_A4B2_03_SingleInspect`：整机三分之四视角。
- `GroundMech_A4B2_04_ChestAssembly`：胸甲近景，仍为实际三维模型渲染。
- `GroundMech_A4B2_05_HeelAssembly`：一体切角后跟近景。
- `GroundMech_A4B2_06_ToeAssembly`：收尖双趾与嵌板近景。
- `GroundMech_A4B2_07_WeaponAssembly`：本轮加长枪体、面板、独立安装接头。
- `GroundMech_A4B2_08_LegAssembly`：本轮腿甲、内侧膝甲和脚部装配。

![实际 Blender 三视图](Previews/02_BlueRed_ThreeViews.png)

[对照页](review.html)并列展示批准参考和本次模型；[反馈与变更记录](revision_notes.json)保留用户原话、附件哈希及本版本剩余事项。

## 数据与边界

当前主体 {g['body_parts']} 分件、{g['body_triangles']:,} 三角面；轮廓壳 {g['outline_triangles']:,} 三角面；骨架 {g['bones']} 骨骼。高度 {g['bounds_m']['dimensions'][2]:.4f} m。数据随本工作版本继续修订，以 [geometry_report](geometry_report.json) 为准。

当前闭合网格与UV统计见 geometry_report，受影响部件的绑定与装配检查见上述各次修订记录。[历史控制检查](blender_data_checks.json)记录之前阶段的三档节点、双枪后坐、瞄准和插槽核对，不作为当前整机全动作验收。源文件 SHA256：`{hashlib.sha256(blend.read_bytes()).hexdigest()}`。

这些技术检查不等于外观通过。B_v2 的完整动画适配、正式 UE 资源提升和 Ground 全玩法运行仍未完成；原包动画和蓝图未被本次模型修订覆盖。B_v1 的待机预览不是 B_v2 全动作兼容证明。
'''
(OUT/'REVIEW.md').write_text(readme,encoding='utf8')
html='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>地面机甲 A_v4 / B_v2</title>
<style>body{font:16px/1.6 system-ui;background:#f3f1ec;color:#25333d;margin:28px}h1{font-size:26px}.status{padding:14px;background:#fff3dc;border-left:4px solid #b07b36}.grid{display:grid;grid-template-columns:1fr 1fr;gap:20px}figure{margin:14px 0;padding:14px;background:white;border-radius:12px}img{width:100%;display:block}figcaption{font-weight:600;margin-bottom:10px}a{color:#276c8b}@media(max-width:1100px){.grid{grid-template-columns:1fr}}</style>
<h1>地面机甲 A_v4 / Blender B_v2</h1><p class="status">B_v2 修改中。A 已确认，B_v1 被指出不一致；B_v2 未通过美术审核，未宣称三视图无差异。</p>
<p>最新枪体细化：八角前护圈、凸起顶盖、叠装侧甲、三段枪口与内凹枪膛、折角下托架；1.5倍总长、挂点、骨骼和非枪体几何保持。<a href="gun_detail_revision.json">枪体修改记录</a>。</p>
<div class="grid"><figure><figcaption>已审参考：武器结构</figcaption><img src="References/Approved_UpperBody_Detail.png"></figure><figure><figcaption>本轮实际模型：枪体细节</figcaption><img src="Previews/07_WeaponAssembly.png"></figure></div>
<p>最新六处标注：双枪加长50%并独立分件；矩形面板改为贴合切角形；后臂支架收拢；腿侧护甲与内膝件重做厚度和位置；脚部通过内收支承明确分层。<a href="annotation_revision.json">本轮修改记录</a>。枪口同步移动，骨骼和既有驱动器保持。</p>
<div class="grid"><figure><figcaption>本轮枪体与安装结构</figcaption><img src="Previews/07_WeaponAssembly.png"></figure><figure><figcaption>本轮腿部与脚部装配</figcaption><img src="Previews/08_LegAssembly.png"></figure></div>
<p>胸前两块已改为独立实体装甲，保留侧壁、倒角和贴合的安装底座。当前 Blender 文件：<a href="GroundMech_A4B2_Editable_B_v2.blend">可编辑 B_v2</a>。</p>
<p>此前脚部轮廓修订保留：一体切角楔形后跟，前双趾缩短、收尖并斜切，米色板贴合表面；本轮连接结构已按最新标注分层。<a href="integrated_heel_revision.json">历史后跟记录</a>、<a href="tapered_toes_revision.json">脚趾记录</a>。</p>
<div class="grid"><figure><figcaption>当前一体楔形后跟</figcaption><img src="Previews/05_HeelAssembly.png"></figure><figure><figcaption>当前收尖双趾</figcaption><img src="Previews/06_ToeAssembly.png"></figure></div>
<div class="grid"><figure><figcaption>批准参考：上半身</figcaption><img src="References/Approved_UpperBody_Detail.png"></figure><figure><figcaption>本次实际模型：胸甲装配</figcaption><img src="Previews/04_ChestAssembly.png"></figure></div>
<div class="grid"><figure><figcaption>批准参考：A_v4 三视图</figcaption><img src="References/Approved_A4_ThreeViews.png"></figure><figure><figcaption>本次实际 Blender 三视图</figcaption><img src="Previews/02_BlueRed_ThreeViews.png"></figure></div>
<div class="grid"><figure><figcaption>批准参考：A_v4 整机</figcaption><img src="References/Approved_A4_Concept.png"></figure><figure><figcaption>本次实际模型</figcaption><img src="Previews/01_BlueRed_Hero.png"></figure></div>
<p>全部“实际模型”图片由本版本 Blender 场景生成。参考图未被用作模型输出。技术检查与用户视觉审核分开记录。</p></html>'''
(OUT/'review.html').write_text(html,encoding='utf8')
if annotation:
    audit_path=OUT/'fidelity_completion_audit.json'
    audit=json.loads(audit_path.read_text(encoding='utf8')) if audit_path.exists() else {}
    if 'goal_blocker' in audit:
        audit['historical_blocker']=audit.pop('goal_blocker')
    audit['date']='2026-10-23';audit['complete']=False
    audit['working_state']='Six annotated corrections implemented; user visual review pending'
    audit['blocker_resolution']='Six new annotated inputs supplied actionable feedback. Construction resumed without asking again for an output-image-inspection exception.'
    audit['model_sha256']=hashlib.sha256(blend.read_bytes()).hexdigest()
    audit['reference_basis']='Approved A_v4 plus the six explicit user amendments, including 1.5x gun length'
    audit['next_decision']='User visual review of the current Blender model and eight actual-model previews; no assistant output-image testing is authorized or performed.'
    manifest=json.loads((OUT/'render_manifest.json').read_text(encoding='utf8'))
    same=manifest['success'] and manifest['blend_sha256']==audit['model_sha256']
    same=same and all(hashlib.sha256(Path(o['path']).read_bytes()).hexdigest()==o['sha256'] for o in manifest['outputs'])
    audit['requirements']=[
        {'requirement':'Use approved A_v4 and current annotated user amendments','state':'implemented_as_design_inputs','evidence':['approval_A.json','revision_notes.json']},
        {'requirement':'Editable real Blender model','state':'proven','evidence':f"{g['body_parts']} body parts, {g['bones']} bones; revision and saved-file hash recorded in annotation_revision.json"},
        {'requirement':'Current actual-model previews','state':'proven' if same else 'pending_refresh','evidence':f"render_manifest.json: {len(manifest['outputs'])} outputs; same saved model and all output hashes verified={same}"},
        {'requirement':'Six new annotated changes','state':'geometry_implemented_visual_acceptance_pending','evidence':'annotation_revision.json: 1.5x gun length, fitted modules and panels, tucked frame, revised leg guards, articulated foot layers; 40 structural contacts pass'},
        {'requirement':'Blender review entry','state':'proven','evidence':'Construction scene with editable weapon collections; added 07_WeaponAssembly and 08_LegAssembly scenes'},
        {'requirement':'Visual fidelity and user B approval','state':'unproven','evidence':'No assistant output-image inspection performed; approval_B remains changes_requested'}
    ]
    audit['completion_boundary']='The six requested geometry corrections are implemented; current visual acceptance, B2 full animation retarget and formal UE Ground integration are separate outstanding work.'
    if gun_detail:
        audit['working_state']='Gun details refined from the approved references after the six annotated corrections; user visual review pending'
        audit['reference_basis']='Approved A_v4 plus the six explicit amendments and the subsequent gun-detail refinement request'
        audit['requirements'].append({'requirement':gun_detail['user_request'],'state':'geometry_implemented_visual_acceptance_pending',
            'evidence':'gun_detail_revision.json: front bezel, top receiver, layered side armor, stepped recessed muzzle and open lower cradle; gun length, bones and sockets unchanged'})
        audit['completion_boundary']='Requested gun details and prior six corrections are implemented. Current visual acceptance, B2 full animation retarget and formal UE Ground integration remain separate outstanding work.'
    audit_path.write_text(json.dumps(audit,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'success':True,'review':str(OUT/'REVIEW.md'),'approval_B':'changes_requested','body_triangles':g['body_triangles']},ensure_ascii=False))
