"""Record the actual color candidate, review evidence, and project provenance."""
import hashlib
import json
import re
import shutil
from pathlib import Path
from PIL import Image

ROOT = Path('D:/UE5.7/test1')
R = ROOT / 'ArtSource/Buildings/SSFStyle_20261005'
O = R / 'TeamPalette_B_v2_20261007'
VERSION = 'SSF_TeamPalette_B_v2'
DATE = '2026-10-07'
QUOTE = '蓝色方所有建筑改成图1配色，红色方把所有建筑改成图二配色，先放Blender给我审核'
NAMES = {'AirBase':'空军基地','CloningCenter':'克隆中心','CommandCenter':'指挥中心','MilitaryFactory':'军工厂','Reactor':'反应堆','StrategyCenter':'战略中心'}

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()

def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf8')

manifest = read_json(O/'candidate_manifest.json')
validation = read_json(O/'native_validation.json')
previews = read_json(O/'preview_inventory.json')
opened = read_json(O/'interactive_open_status.json')
assert manifest['success'] and validation['success'] and previews['success'] and opened['success']
assert previews['native_capture_count'] == 75
assert sha(O/'SSF_TeamPalette_B_v2.blend') == manifest['candidate_blend_sha256']
assert sha(Path(manifest['source_blend'])) == manifest['source_blend_sha256']

# Prevent replacing text that another task edited while this work was running.
EXPECTED = {
 'Progress/RequirementDocument/20261005-SSF建筑美术统一与三档LOD.md':'313bf2d7ff615650b1460b50156aaf5e3aea84fc2f8e90c89254f8c25f88f234',
 'Progress/DevelopmentDocumentation/20261005-SSF建筑美术统一与三档LOD.md':'97b8dac65bfa8e7f2c7fb2f08add77ce07da84a248899e3b1561a744438371fe',
 'Progress/RequirementDocument/GuLiStrike美术规范.md':'fc19daf1a7995dcb89b0a04c5327ee842b676cc99d5cadf527256e67fcc23709',
 'Progress/DevelopmentDocumentation/GuLiStrike美术规范.md':'9e18b4ef9cf589f12b9b9488f2026d4a4f957db4ca2034d35669532f03064a18',
}
for relative, expected in EXPECTED.items():
    if sha(ROOT/relative) != expected:
        raise RuntimeError('Re-read and merge concurrent changes before writing: '+relative)
archive = ROOT/'Progress/Archive/20261007-SSF建筑蓝红阵营配色Blender审核B_v2.md'
assert not archive.exists(), 'This increment is already recorded; do not overwrite it.'
write_json(O/'progress_snapshot_before.json', {'date':DATE, 'files':EXPECTED})

inputs = O/'Inputs'
inputs.mkdir(exist_ok=True)
references = []
for team, label, filename in [
    ('Blue','图1','codex-clipboard-1916ab2b-24ad-4cf2-87aa-dcb93bda8563.png'),
    ('Red','图2','codex-clipboard-f2bee0e8-1fa1-4029-baba-e13e80b8cffe.png'),
]:
    original = Path('C:/Users/a/AppData/Local/Temp')/filename
    preserved = inputs/(team+'_Palette_Reference.png')
    shutil.copy2(original, preserved)
    assert sha(original) == sha(preserved)
    with Image.open(preserved) as image:
        dimensions = list(image.size)
    references.append({'team':team, 'label':label, 'original_path':str(original),
                       'preserved_file':'Inputs/'+preserved.name, 'sha256':sha(preserved), 'dimensions':dimensions})
request = {'date':DATE, 'user_quote':QUOTE, 'version':VERSION, 'references':references,
           'source_version':'SSF_Production_B_v1', 'source_blend_sha256':manifest['source_blend_sha256'],
           'candidate_blend_sha256':manifest['candidate_blend_sha256'],
           'palette_anchors':{'Blue':'original AirBase palette','Red':'original CommandCenter palette'},
           'interpretation':'Apply each reference palette to the six buildings; keep existing functional color regions, geometry, rigs, UVs, weights and actions. Preview both teams in Blender.',
           'candidate_B_approval':False, 'candidate_UE_import_authorized':False,
           'UE_changed_this_task':False, 'other_assets_recolored':False}
write_json(O/'user_reference_instruction.json', request)

contacts = [f'Sheets/QA_{team}_Views_{p}.png' for team in ['Blue','Red'] for p in range(1,4)]
contacts += [f'Sheets/QA_{team}_LODs_{p}.png' for team in ['Blue','Red'] for p in range(1,3)]
visual = {
    'date':DATE, 'version':VERSION, 'candidate_blend_sha256':manifest['candidate_blend_sha256'],
    'assistant_visual_review_performed':True, 'user_B_approval':False,
    'method':'Viewed the actual Blender render contacts through view_image. Sheets only arrange native pixels and add labels; no AI replacement or retouching.',
    'inspected':contacts+['Sheets/Blue_Buildings_Overview.png','Sheets/Red_Buildings_Overview.png',
                        'Renders/Blue_Red_Assembly_Compare.png','Sheets/Blue_CommandCenter_Views.png',
                        'Sheets/Red_Reactor_Views.png','Sheets/Blue_StrategyCenter_LODs.png','Sheets/Red_MilitaryFactory_LODs.png'],
    'coverage':{'building_variants':12,'Hero_Front_Left_Back_views':48,'same_camera_LOD_samples':36},
    'findings':[
        'Blue variants consistently use orange, bluegrey and cream; Red variants use berry, pale pink and orange. Blue AirBase and Red CommandCenter keep the original reference palettes.',
        'All six kinds retain their individual silhouette, entrances, attachments and functional asymmetry. Color placement is consistent between each variant\'s Hero/Front/Left/Back.',
        'Discrete tone regions remain visible. Existing internal panel masks are retained on LOD0/1; LOD2 suppresses fine lines. Real outline shells remain on LOD0/1.',
        'The visible LOD simplification is the already-delivered B_v1 geometry. The recolor introduced no new silhouette changes; source/candidate geometry, normals, UVs, weights and reference bones were independently checked.',
        'Existing source-derived internal masks have some irregular/UV texture marks, and outline coverage remains limited to the previous shell budget. This task did not retopologize or repair those inherited differences.',
    ],
    'verification_boundary':'Color and retained appearance checked in Blender, not a new full animation movie review, UE validation, gameplay integration or performance measurement. Existing 24 body budget differences remain recorded with B_v1.',
    'ready_for_user_Blender_review':True,
}
write_json(O/'visual_qa.json', visual)

review = f'''# SSF 建筑蓝红阵营配色 · 实际 Blender B_v2

当前版本 **{VERSION}** 已在 Blender 5.2.2 LTS 打开，**待用户审核**。本轮按用户“{QUOTE}”制作六座建筑各一份蓝方、一份红方，共十二个实际模型。配色来自附图对应的原版 AirBase / CommandCenter 材质色值，保留各建筑既有功能分区。

[打开 Blender 文件](SSF_TeamPalette_B_v2.blend) · [两队配色总览](Sheets/Blue_Red_Buildings_Overview.png) · [实际尺寸组合](Renders/Blue_Red_Assembly_Compare.png)

## Blender 查看入口

默认场景 `Review_Blue_Red_Buildings`：**左侧蓝方、右侧红方**，按原建筑尺寸摆放，使用真实渲染视口。切换顶部 Scene 选择器可进入 `Blue_Buildings_Assembly` / `Red_Buildings_Assembly`，或 `Blue_AirBase` / `Red_AirBase` 等单建筑场景近看。

各单建筑场景默认显示 LOD0；`<Team>_<Asset>_LOD0/1/2` collection 可在 Outliner 分别启用以检查对应档位。图板提供已实际渲染的同机位对照，无需改变已打开场景。历史制作场景仍保留792个可编辑分件及原修改器；新配色的三档成品网格也可编辑。

## 两套基础配色

| 阵营 | 主色与配色 | 阴影 / 结构颜色 |
|---|---|---|
| 蓝方 · 图1 | 橙 `#EE9D58`、蓝灰 `#274E61`、奶油白 `#FEE4D9` | 原空军基地少量框架 `#1A182F`；军工厂/战略中心框架使用蓝灰 |
| 红方 · 图2 | 莓红 `#A34053`、浅粉 `#E3B6B1`、橙 `#EE9D58` | 原指挥中心少量框架 `#662249`；军工厂/战略中心框架使用莓红 |

原独立内部线稿、近中档真实描边壳、三档明暗因子 `0.40 / 0.72 / 1.0`、阈值 `0.38 / 0.68`、固定光向及 `Base Color` / `Team Color` 接口保留。十二张新2K基础色图集已打包入当前 blend；原线稿遮罩、UV、ORM与半透明标识继续使用现有版本。

## 逐建筑审核图板

| 建筑 | 蓝方效果 / 三视图 | 红方效果 / 三视图 | 蓝方 LOD | 红方 LOD |
|---|---|---|---|---|
'''
for key, name in NAMES.items():
    review += f'| {name} · {key} | [图板](Sheets/Blue_{key}_Views.png) | [图板](Sheets/Red_{key}_Views.png) | [三档](Sheets/Blue_{key}_LODs.png) | [三档](Sheets/Red_{key}_LODs.png) |\n'
review += f'''
75张原生 Blender 图片包括48张2K效果/正交图、24张2K LOD1/2补图、两张4K单队组合、一张4096×2600两队对照。图板只排版原生渲染。

## 保持与检查

- 独立打开保存后的候选 blend，60个本体/描边网格与来源的几何、法线、UV、权重摘要一致；36档调色检查及36档三档/线稿节点检查通过。十二套骨架继续沿用各自原骨名、层级和参考姿态。
- 22个原 Action 留存在文件中；六类建筑共21个动作由两队各自骨架继续兼容使用，无人机原动作保留。本轮未重制、删减或重新烘焙动画；既有完整视频见[原B_v1图板和动画](../review_B_v1.html)。
- 助手已查看两队全套48个核心视图及36个同机位LOD样本；色块、结构和视图关系一致。助手视觉检查与用户审核分别记录。
- 本轮沿用B_v1几何，原[24项本体预算差额](../Production_B_v1/Budget_Exceptions.md)和部分真实壳覆盖弱于参考的差异继续登记，原面数上限保持。源UV线稿中的部分不规则纹理没有在此次调色中修复。
- Floor、Lamp、Light、Drone颜色保持；本轮没有导入UE或接入玩法。`/Game/GuLiStrike/Buildings/SSFStylized` 仍是已完成存储交付的B_v1。

## 版本与决定

来源 blend SHA256：`{manifest['source_blend_sha256']}`。当前候选 SHA256：`{manifest['candidate_blend_sha256']}`。当前仅有调色与Blender预览指令，**B_v2待用户针对实际成品决定**；先前B_v1正式存储放行不覆盖本候选。

本次停在用户指定的Blender审核阶段。[制作技能](../../../.agents/skills/guli-model-production/SKILL.md)与[美术规范](../../../Progress/RequirementDocument/GuLiStrike美术规范.md)的对应流程是“参考图 → 用户审核 A → Blender 一比一还原 → 用户审核 B → UE 导入”。当前已完成可审产物，用户决定后才推进本版正式UE更新。

[用户附图与指令](user_reference_instruction.json) · [候选清单](candidate_manifest.json) · [独立回读](native_validation.json) · [助手视觉记录](visual_qa.json) · [全部原生图片](preview_inventory.json) · [Blender已打开](interactive_open_status.json)
'''
# This review lives four levels below the project root.
review = review.replace('(../../../.agents/', '(../../../../.agents/').replace('(../../../Progress/', '(../../../../Progress/')
(O/'Review_B_v2.md').write_text(review, encoding='utf8')

decisions = read_json(R/'review_decisions.json')
decisions['B_v1_release'] = decisions['B']
new_B = {'version':VERSION, 'date':DATE, 'status':'pending_user_decision',
         'candidate_blend_sha256':manifest['candidate_blend_sha256'], 'user_quote':QUOTE,
         'reference_selection':'TeamPalette_B_v2_20261007/user_reference_instruction.json',
         'review':'TeamPalette_B_v2_20261007/Review_B_v2.md', 'UE_formal_import_performed':False,
         'formal_UE_import_authorized':False, 'gameplay_integrated':False}
decisions.update({'current_version':VERSION,'date':DATE,'user_palette_steering':QUOTE,
                  'B':new_B,'team_palette_B_v2':new_B,'formal_UE_current_version':'SSF_Production_B_v1',
                  'current_candidate_UE_import_performed':False,'formal_UE_import_authorized':False})
decisions['history'].append(new_B)
decisions['steering_history'].append({'date':DATE,'user_quote':QUOTE,'result':VERSION,
                                    'meaning':'Authorized two-team recolor and actual Blender review; not candidate B approval or UE update.'})
write_json(R/'review_decisions.json', decisions)

readme = (R/'README.md').read_text(encoding='utf-8-sig')
marker = '当前审核版本为 **SSF_Reference_A_v7**'
assert marker in readme
readme = '''# SSF 建筑制作源目录

当前新增审核版本为 **SSF_TeamPalette_B_v2**：六座建筑各制作蓝方、红方配色，实际Blender已打开，待用户审核。蓝方沿用附图空军基地橙/蓝灰/奶油白，红方沿用附图指挥中心莓红/浅粉/橙；几何、骨架、原动作、线稿、三档明暗及三档LOD保持。

- [本轮实际成品与审核入口](TeamPalette_B_v2_20261007/Review_B_v2.md)
- [Blender审核文件](TeamPalette_B_v2_20261007/SSF_TeamPalette_B_v2.blend)
- [两队总览](TeamPalette_B_v2_20261007/Sheets/Blue_Red_Buildings_Overview.png)
- [具体版本决定](review_decisions.json)
- [此前B_v1正式UE交付](UE_Delivery_v1/README.md)

UE正式目录 `/Game/GuLiStrike/Buildings/SSFStylized` 已于2026-10-06完成B_v1的105资源存储交付，本轮未更新UE或接入游戏。B_v2用户审核通过后再处理正式更新，原B_v1预算差额和壳覆盖差异继续记录；来源与历史文件保持。

## 历史参考阶段入口

以下按发生时的阶段状态保留，当前候选状态以上方B_v2及决定登记为准。

''' + readme[readme.index(marker):]
(R/'README.md').write_text(readme, encoding='utf8')

def metadata(text, updates):
    parts = text.split('---', 2)
    assert len(parts) == 3 and not parts[0].strip()
    front = parts[1]
    for key, value in updates.items():
        front, count = re.subn(r'^'+re.escape(key)+r':.*$', key+': '+value, front, flags=re.M)
        assert count == 1, key
    return '---'+front+'---'+parts[2]

latest = '''\n\n## 2026-10-07 蓝红阵营配色实际 Blender B_v2

用户最新附两张实际效果图，明确“蓝色方所有建筑改成图1配色，红色方把所有建筑改成图二配色，先放Blender给我审核”。此指令替代六座建筑各独立色系的旧方向：蓝方橙`#EE9D58`/蓝灰`#274E61`/奶油白`#FEE4D9`，红方莓红`#A34053`/浅粉`#E3B6B1`/橙`#EE9D58`；六座各制作两队副本，保持原功能分区。蓝方AirBase和红方CommandCenter直接沿用附图对应原调色，少量框架与线条保持原参考颜色；军工厂和战略中心的框架在新队色中使用蓝灰/莓红，不新增大面积近黑色块。Floor、Lamp、Light、Drone不在此次调色范围。

[实际审核B_v2](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/Review_B_v2.md)与已打开[Blender](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/SSF_TeamPalette_B_v2.blend)提供12个模型、36档本体、24档真实描边壳、12套兼容原骨架、12张打包2K基础色图集及75张原生渲染。默认场景`Review_Blue_Red_Buildings`左蓝右红；单模型场景与48张效果/三视图、36档同机位LOD样本可近看。原22个Action、线稿遮罩、三档明暗和原B_v1的全部可编辑分件保留。

[独立回读](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/native_validation.json)核对60个网格的几何/法线/UV/权重、36档配色和风格节点；[助手读图](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/visual_qa.json)覆盖12模型的48核心视图及36个LOD样本，两个原色锚点保持。来源B_v1 SHA256为`6f386c2e4782ce6ab6e81ae0f2380a6345fa05cdc6fc3dcf9d260c13ed3a806e`，当前B_v2 SHA256为`5cee0988de1a4201cd9eb60de9cbd30ccddd108090b99b3e86f50c4bf2d584b6`；[用户指令/附图](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/user_reference_instruction.json)保存原图和哈希。

当前B_v2待用户针对实际成品审核；B_v1的正式存储放行与105资源交付继续有效，不能推导B_v2已通过。**本轮未更新UE、不接入玩法**，原24项本体预算差额及真实壳局部覆盖差异继续登记，未重新减面或提高预算。此为资产队色修订，规范仍v1.3；[本轮增量归档](../Archive/20261007-SSF建筑蓝红阵营配色Blender审核B_v2.md)记录版本、验证和下一步。
'''

req_path = ROOT/'Progress/RequirementDocument/20261005-SSF建筑美术统一与三档LOD.md'
req = metadata(req_path.read_text(encoding='utf-8-sig'), {
    'updated':"'2026-10-07'", 'summary':'六座SSF建筑新增蓝方图1及红方图2配色，实际Blender B_v2与完整视图已交付待审；既有B_v1正式存储保持，不接入游戏。',
    'next_action':'用户在Blender审核具体SSF_TeamPalette_B_v2的两队配色，决定后再更新正式UE资源。',
    'status_note':'本轮调色候选待B审核；B_v1的105正式资源与先前存储放行保留，原预算差额继续登记。'})
req_path.write_text(req+latest, encoding='utf8')

dev_path = ROOT/'Progress/DevelopmentDocumentation/20261005-SSF建筑美术统一与三档LOD.md'
dev = metadata(dev_path.read_text(encoding='utf-8-sig'), {
    'status':'verification', 'verification':'partial', 'updated':"'2026-10-07'",
    'summary':'蓝红两队六座建筑配色B_v2已在实际Blender打开，原生视图和独立回读完成，待用户审核；本轮UE未更新。',
    'next_action':'用户审核Blender场景Review_Blue_Red_Buildings左蓝右红的B_v2，确认后再执行正式资源更新。',
    'status_note':'B_v2的几何保持、配色及线稿三档检查已完成；技术与助手读图不代替用户B决定，旧B_v1正式资源保持。'})
old = '当前为实际成品 **B_v1已放行并完成正式资源存储**，原A_v7依据用户“开始制作，严格一比一按照参考图和原模型制作”放行，清单SHA256固定。'
assert old in dev
dev = dev.replace(old, '当前为新增实际配色 **SSF_TeamPalette_B_v2已打开、待用户审核**：[本轮审核入口](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/Review_B_v2.md)。六座各有蓝红两队，左蓝右红；本轮UE未更新。此前B_v1已放行并完成105资源正式存储，原A_v7制作放行和固定清单仍保留。', 1)
dev_path.write_text(dev+latest, encoding='utf8')

norm_path = ROOT/'Progress/RequirementDocument/GuLiStrike美术规范.md'
norm = metadata(norm_path.read_text(encoding='utf-8-sig'), {'updated':"'2026-10-07'"})
norm = norm.replace('SSF六座建筑按用户2026-10-05要求各用不同色系', '**SSF截至2026-10-06的历史设计与交付事实：** 六座建筑按用户2026-10-05要求各用不同色系', 1)
needle = '\n## 4.'
new_rule = '\n\n**SSF当前队色修订（2026-10-07）：** 用户最新指定蓝色方全部六座建筑沿用附图1空军基地的橙/蓝灰/奶油白，红色方沿用附图2指挥中心的莓红/浅粉/橙，并先放Blender审核；此要求替代六座各用不同色系的后续制作方向。[实际B_v2](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/Review_B_v2.md)已打开、待具体成品审核，保留B_v1的几何、骨架、22原Action、线稿、三档明暗和三档LOD；平台、灯具和无人机不改色。B_v1正式资源继续保留，未据旧放行更新B_v2到UE。这是本批资产修订，不修改全局v1.3或其他资产已审配色。\n'
assert needle in norm
norm = norm.replace(needle,new_rule+needle,1)
norm_path.write_text(norm,encoding='utf8')

ledger_path = ROOT/'Progress/DevelopmentDocumentation/GuLiStrike美术规范.md'
ledger = metadata(ledger_path.read_text(encoding='utf-8-sig'), {'updated':"'2026-10-07'"})
ledger_path.write_text(ledger+latest,encoding='utf8')

archive_text = f'''---
schema: guli-progress/v1
id: ARC-20261007-001
work_id: ''
kind: archive
role: root
title: SSF建筑蓝红阵营配色Blender审核B_v2
areas: [art, assets, rendering]
categories: [art]
status: recorded
verification: partial
created: '2026-10-07'
updated: '2026-10-07'
summary: 十二个蓝红建筑配色副本已在实际Blender打开，75张原生渲染和独立回读完成；B_v2待用户审核，UE保持B_v1。
next_action: 用户审核SSF_TeamPalette_B_v2两队配色，版本通过后再处理正式UE更新。
relations:
  work_items: [WORK-20261005-003, WORK-20260917-001]
status_note: 本次是Blender配色交付和审核记录，非UE更新或玩法接入；既有预算差额保留，不将技术检查记为用户通过。
---

# 2026-10-07：SSF 六座建筑蓝红阵营配色候选

用户明确“{QUOTE}”，以两张实际建筑截图指定两队配色。已使用此前B_v1生产模型建立`{VERSION}`，没有以生成图片代替模型。来源SHA256 `{manifest['source_blend_sha256']}`，候选SHA256 `{manifest['candidate_blend_sha256']}`；原附件及哈希见[指令记录](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/user_reference_instruction.json)。

## 变更清单

| 文件 / 资产 | 变更 |
|---|---|
| `ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/SSF_TeamPalette_B_v2.blend` | 蓝红各六座，36本体LOD、24描边网格、12原兼容骨架；保留历史分件源 |
| `TeamPalette_B_v2_20261007/Textures` | 十二张新2K基础色图集，打包入候选blend |
| `TeamPalette_B_v2_20261007/Renders`、`Sheets` | 75张实际模型渲染，完整效果/正/左/背及三LOD图板 |
| `TeamPalette_B_v2_20261007/Review_B_v2.md`、JSON记录 | 查看入口、指令来源、候选哈希、独立检查、视觉证据、未审状态 |
| 当前需求、开发、规范、台账及索引 | 登记新队色方向和待审版本，保留B_v1已完成交付事实 |

## 配色与实现

蓝方使用图1橙`#EE9D58`、蓝灰`#274E61`、奶油白`#FEE4D9`；红方使用图2莓红`#A34053`、浅粉`#E3B6B1`、橙`#EE9D58`。从对应原材质读取色值，保留各建筑功能分区；蓝方空军基地、红方指挥中心是原色锚点。其他机械框架使用原参考少量暗色，军工厂/战略中心框架映射为蓝灰/莓红。线稿遮罩、真实近中描边壳、固定光向、三档因子及阈值保持；没有进行新减面。

默认Blender场景`Review_Blue_Red_Buildings`按实际尺寸左蓝右红；各单建筑场景可近看。实际交互窗口已加载候选文件并启用渲染视口，见[打开记录](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/interactive_open_status.json)。

## 资源清单

| 最终项目内文件夹 | 来源 | 内容与用途 | 依赖与边界 |
|---|---|---|---|
| `ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/Inputs` | 用户Temp目录两张原PNG附件 | 原样保留蓝红配色参考与哈希 | 只作参考，非生产贴图；无外部Actor/Object或示例地图迁移 |
| `ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007` | 项目内冻结`Production_B_v1` | 两队实际Blender、图集、渲染与审核记录 | 原遮罩/ORM已在blend中保留；无新增UE导入 |
| `/Game/GuLiStrike/Buildings/SSFStylized` | 上一轮已完成的B_v1正式交付 | 现有105正式资源保持 | 本轮未重导入、替换或接入游戏；Floor/Lamp/Light/Drone未调色 |

## 验证与审核

[独立Blender回读](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/native_validation.json)核对60网格的几何/UV/法线/权重摘要、36档配色及36档三阶着色和线稿节点、12套参考骨架与22个保留Action。保存后来源与候选哈希一致，Blue AirBase/Red CommandCenter原色保持。

[原生渲染清单](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/preview_inventory.json)保存75图的尺寸和哈希；[助手视觉检查](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/visual_qa.json)查看48核心视图与36LOD样本、两队总览及组合图，视图色块与结构保持一致。没有重新做22动作完整视频、FBX回读、UE检查或实战性能测量，因为当前指令为先审核Blender配色，几何和原动作未改。

**用户对B_v2尚未给出通过决定。** 原B_v1正式存储放行继续归属于其具体版本，当前UE保持旧交付。当前[审核入口](../../ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/Review_B_v2.md)与[开发](../DevelopmentDocumentation/20261005-SSF建筑美术统一与三档LOD.md)已同步为待审核。项目规范仍v1.3。

## 遗留边界

原24项本体预算差额、预算内描边壳局部覆盖较弱和源UV线稿少量不规则保持记录，未宣称预算全部达到原目标。下一步是用户审实际B_v2配色，决定后再推进正式UE更新；不修改玩法或运行PIE。

既有美术规范需求与台账超过建议文档预算，可另按资产/审核历史拆分；本轮仅登记当前修订，不自动改写冷归档或拆分历史。
'''
archive.write_text(archive_text, encoding='utf8')
write_json(O/'progress_update_record.json', {'date':DATE,'archive_id':'ARC-20261007-001',
           'updated_files':{p:sha(ROOT/p) for p in EXPECTED}, 'archive':str(archive),
           'candidate_B_approved':False, 'formal_UE_updated':False})
print('SSF_TEAM_B2_RECORDED', manifest['candidate_blend_sha256'], flush=True)
