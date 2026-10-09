"""Build the reference-only A_v2 gallery and per-model paint contracts. No image editing."""
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
    m['paint_region_mode']='existing_parts_or_region_masks_after_A_approval'
    m['geometry_contract']='Existing source mesh/source views remain authoritative for geometry, assembly, animations and LOD. Generated sheets are 2D color references, not measured manufacturing drawings or Blender output.'
    dump(OUT/'Configs'/f"{m['id']}.json",m)
palette_evidence=[]
for i in range(1,5):
    p=OUT/'References'/f'Palette_{i:02}.jpg'
    palette_evidence.append({'file':p.relative_to(OUT).as_posix(),'sha256':digest(p),'colors':catalogue['palette_cards'][i-1]})
report={k:v for k,v in catalogue.items() if k!='models'}
report.update(models=models,palette_evidence=palette_evidence,generation_tool='built-in image_gen.imagegen',actual_model_construction=False,formal_ue_resource_update=False,review_boundaries=['AI images communicate paint zoning; source geometry remains authoritative.','No native compile, gameplay launch, automation tests or formal model/material edits.','All A approvals pending; reference design is not Blender B approval.'])
dump(OUT/'review-manifest.json',report)
dump(OUT/'approval_A.json',{'version':catalogue['version'],'scope_count':13,'stage':'reference_A','models':{m['id']:{'name':m['name'],'status':'pending','decision':None,'blue_sha256':m['boards']['blue']['sha256'],'red_sha256':m['boards']['red']['sha256']} for m in models},'source':'No explicit user approval of these concrete A_v2 boards has been received.','next_stage':'Only explicitly approved specific model/version proceeds to Blender B.'})
(OUT/'gallery-data.js').write_text('window.REFERENCE_REVIEW = '+json.dumps(report,ensure_ascii=False)+';\n',encoding='utf8')
summary=['# 13 个模型配色参考 A_v2 审核','',
'共 26 张独立蓝红图板，每张包含三分之四效果、正视、左侧视和后视。新增防空炮、哨戒炮、矿厂已纳入。','',
'[可放大审核画廊](index.html) · [全套总览](overview.html) · [完整配置与来源清单](review-manifest.json) · [审核状态](approval_A.json)','',
'## 色卡与分区','',
'本轮只从用户四张色卡选取：奶油白 #FEE4D9 作主装甲，深灰 #2C3735 作机构，蓝灰 #6AA4BE／莓红 #A34053 作队色。仅已有功能位置允许少量 #EE9D58 或 #0D9099；护盾顶部青灯两队固定一致。色阶是体积表达，基础涂装色以配置 HEX 为准。','',
'参考图用于审核大色块、队色区及线条整理。原模型尺寸、几何、装配、活动件、动画和 LOD 是施工依据；二维生成图不作尺寸测量图或实际 Blender 成品。','',
'## 模型清单','',
'| 模型 | 蓝方 | 红方 | 配置 | A 审核 |',
'|---|---|---|---|---|']
for m in models:
    summary.append(f"| {m['name']} | [图板]({m['boards']['blue']['path']}) | [图板]({m['boards']['red']['path']}) | [分区](Configs/{m['id']}.json) | 待用户决定 |")
summary+=['','彼之矛施工体共用彼之矛 A_v2，不额外计作第十四个模型。SSF 本轮继续作为已入库建筑美术资源，不新增玩法类型。','',
'## 源图与修订','',
'两种兵种沿用 CommanderLOD 的实际原模型四视图；SSF 六座沿用已入库 B_v2 的实际 Blender 四视图。其余五座玩法建筑从现有 UE 网格或完整 Blueprint 提取原生视图，未保存或修改正式资产。两炮塔沿 +Y 炮口方向重新核对正视：Source Right→Front、Source Back→Left、Source Left→Back。','',
'原色卡保存在 References；精确生成提示词保存在 Prompts；源视图和来源哈希在清单及各模型配置中。未选首稿保留供追溯，不进入主画廊。旧版 [红蓝实际 UE 对照](../LocalTeamColorReview_20261008/REVIEW.md)继续保留。','',
'## 当前审核阶段','',
'本轮停在参考设计 A。可以按“模型名 + A_v2 + 通过／需要修改的位置”逐项给出决定。没有明确通过的模型继续改参考，尚未进行 Blender 施工或正式 UE 更新。','',
'项目流程见 [模型制作技能](../../.agents/skills/guli-model-production/SKILL.md) 与 [美术规范](../../Progress/RequirementDocument/GuLiStrike美术规范.md)：参考图 → 用户审核 A → Blender 成品 → 用户审核 B → 正式 UE 资源。','']
(OUT/'REVIEW.md').write_text('\n'.join(summary),encoding='utf8')
print(json.dumps({'models':len(models),'boards':sum(len(m['boards']) for m in models),'configs':len(models),'approval_A':'pending'},ensure_ascii=False))

