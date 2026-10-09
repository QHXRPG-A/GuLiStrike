"""Build the reference-only A_v3 gallery and per-model paint contracts. No image editing."""
import hashlib
import html
import json
import struct
from pathlib import Path

OUT=Path(__file__).resolve().parents[1]
ROOT=OUT.parents[1]
catalogue=json.loads((OUT/'Internal/catalogue.json').read_text(encoding='utf8'))
asset_report=json.loads((ROOT/'ArtSource/LocalTeamColorReview_20261008/asset-authoring.json').read_text(encoding='utf8'))
assets={m['name']:m for m in asset_report['models']}
def dump(path,data):
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def image_info(path):
    raw=path.read_bytes()
    assert raw[:8]==b'\x89PNG\r\n\x1a\n',path
    width,height=struct.unpack('>II',raw[16:24])
    return {'path':path.relative_to(OUT).as_posix(),'width':width,'height':height,'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()}
models=catalogue['models']
for m in models:
    m['boards']={team:image_info(OUT/'Boards'/f"{m['id']}_{team}.png") for team in ('blue','red')}
    m['source_mesh']=assets[m['id']]['mesh']
    m['source_kind']=assets[m['id']]['kind']
    m['source_references']=[{'path':p,'sha256':digest(ROOT/p)} for p in m['source']]
    m['reference_version']=catalogue['version']
    m['approval_A']={'status':'pending','user_decision':None,'approved_variant_hashes':None}
    m['construction_B']={'status':'not_started','approved':False}
    m['runtime_enabled']=False
    m['palette']=catalogue['palette']
    m['enemy_non_blue']='#A34053'
    m['enemy_non_blue_secondary']='#662249'
    m['paint_region_mode']='existing_parts_or_region_masks_after_A_approval'
    m['paint_contract']={
        'fixed':{'cream':'#FEE4D9','sand':'#D5C09C','mechanisms':'#2C3735'},
        'team':{'blue':{'primary':'#6AA4BE','secondary':'#274E61'},'red':{'primary':'#A34053','secondary':'#662249'}},
        'team_light':{'count':3,'blue':'#6AA4BE','red':'#A34053','fixed_cyan':False} if m['id']=='ShieldGenerator' else None,
        'shading':'Three cel-shading levels are separate from the two team base paint colors.',
        'fixed_function_policy':'Only existing function locations and palette colors; shield top lights are team-controlled.'
    }
    m['geometry_contract']='Existing source mesh/source views remain authoritative for geometry, assembly, animations and LOD. Generated sheets are 2D color references, not measured manufacturing drawings or Blender output.'
    dump(OUT/'Configs'/f"{m['id']}.json",m)
palette_evidence=[]
for i in range(1,5):
    p=OUT/'References'/f'Palette_{i:02}.jpg'
    palette_evidence.append({'file':p.relative_to(OUT).as_posix(),'sha256':digest(p),'colors':catalogue['palette_cards'][i-1]})
report={k:v for k,v in catalogue.items() if k!='models'}
report.update(models=models,palette_evidence=palette_evidence,generation_tool='built-in image_gen.imagegen',actual_model_construction=False,formal_ue_resource_update=False,source_capture_policy='Reuse immutable A_v2 native UE captures and existing CommanderLOD/SSF Blender source sheets. No UE editor capture or asset mutation in A_v3.',review_boundaries=['AI images communicate paint zoning; source geometry remains authoritative.','No native compile, gameplay launch, automation tests or formal model/material edits.','All A approvals pending; reference design is not Blender B approval.'])
dump(OUT/'review-manifest.json',report)
dump(OUT/'approval_A.json',{'version':catalogue['version'],'scope_count':len(models),'stage':'reference_A','models':{m['id']:{'name':m['name'],'status':'pending','decision':None,'blue_sha256':m['boards']['blue']['sha256'],'red_sha256':m['boards']['red']['sha256']} for m in models},'source':'No explicit user approval of these concrete A_v3 boards has been received.','next_stage':'Only explicitly approved specific model/version proceeds to Blender B.'})
(OUT/'gallery-data.js').write_text('window.REFERENCE_REVIEW = '+json.dumps(report,ensure_ascii=False)+';\n',encoding='utf8')
summary=['# 14 个模型配色参考 A_v3 审核','',
'共 28 张独立蓝红图板，每张包含三分之四效果、正视、左侧视和后视。原十三模型全部重新设计，包含指挥中心和战略中心；制作中按用户明确决定补入重防号 WM01（UnitTypeId=2）。','',
'[可放大审核画廊](index.html) · [全套总览](overview.html) · [完整配置与来源清单](review-manifest.json) · [审核状态](approval_A.json)','',
'## 色卡与分区','',
'所有基础涂装只选用户四张色卡：乳白 #FEE4D9、固定米砂 #D5C09C、机构深灰 #2C3735；蓝方浅/深队色 #6AA4BE/#274E61，红方浅/深队色 #A34053/#662249。乳白目标降至可见模型表面的20–30%，参考图不作精确覆盖率测量。护盾顶部三个色点属于队色灯，蓝方蓝、红方红；其余原有固定功能色保留。两档队色与三档明暗分别管理。','',
'参考图用于审核大色块、队色区及线条整理。原模型尺寸、几何、装配、活动件、动画和 LOD 是施工依据；二维生成图不作尺寸测量图或实际 Blender 成品。','',
'## 模型清单','',
'| 模型 | 蓝方 | 红方 | 配置 | A 审核 |',
'|---|---|---|---|---|']
for m in models:
    summary.append(f"| {m['name']} | [图板]({m['boards']['blue']['path']}) | [图板]({m['boards']['red']['path']}) | [分区](Configs/{m['id']}.json) | 待用户决定 |")
summary+=['','彼之矛施工体共用彼之矛 A_v3，不额外计作独立模型。SSF 本轮继续作为已入库建筑美术资源，不新增玩法类型。重防号沿 WM01 稳定实现定位，非玩家 Ground 席位。','',
'## 源图与修订','',
'三种兵种沿用 CommanderLOD 的实际原模型四视图；SSF 六座沿用已入库 B_v2 的实际 Blender 四视图。其余五座玩法建筑复用 A_v2 已从 UE 网格或完整 Blueprint 提取的二十二张原生视图，三个原捕获报告原样保存；本轮没有重新操作编辑器或保存正式资产。两炮塔按 +Y 炮口方向：Source Right→Front、Source Back→Left、Source Left→Back。','',
'前哨按用户最新要求改为整件配色：中央最高宽矩形整件浅队色，相邻矩形整件米砂，外围窄矩形整件乳白，外部单个半圆环整件深队色；不按高度切成色带。被否定的按高度稿保存在 height-draft 文件，仅供追溯。','',
'原色卡与护盾反馈截图保存在 References；精确生成提示词保存在 Prompts，输入及输出路径见 [生成记录](generation-log.json)、[色卡来源](palette-source-map.json)。源视图和来源哈希在清单及各模型配置中。未选首稿保留供追溯，不进入主画廊。旧版 [A_v2](../LocalTeamColorReference_A_v2_20261008/REVIEW.md)及[红蓝实际 UE 对照](../LocalTeamColorReview_20261008/REVIEW.md)继续保留。','',
'## 核对记录','',
'[助手逐张读图](visual_qa.json) · [文件、来源和静态链接回读](delivery-check.json) · [冻结交付清单](frozen_delivery_manifest.json)。蓝红分区以配置中的基础 HEX 为准，二维光影不是新增涂装色，也不作为精确覆盖率或尺寸测量。','',
'本地浏览器交互未自动验证：前轮浏览器工具拒绝 file 协议，本轮沿用该限制，不替换协议或入口绕过。画廊提供本地文件与静态核对结果。','',
'## 当前审核阶段','',
'本轮停在参考设计 A。可以按“模型名 + A_v3 + 通过／需要修改的位置”逐项给出决定。没有明确通过的模型继续改参考，尚未进行 Blender 施工或正式 UE 更新。','',
'项目流程见 [模型制作技能](../../.agents/skills/guli-model-production/SKILL.md) 与 [美术规范](../../Progress/RequirementDocument/GuLiStrike美术规范.md)：参考图 → 用户审核 A → Blender 成品 → 用户审核 B → 正式 UE 资源。','']
(OUT/'REVIEW.md').write_text('\n'.join(summary),encoding='utf8')
print(json.dumps({'models':len(models),'boards':sum(len(m['boards']) for m in models),'configs':len(models),'approval_A':'pending'},ensure_ascii=False))

