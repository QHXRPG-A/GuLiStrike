"""Record actual formal adoption and player handoff; FPS is explicitly deferred."""
import hashlib,json,re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'outputs/performance/20261009-client-three-optimizations'
proof=json.loads((OUT/'client-three-delivery-readback.json').read_text(encoding='utf-8'))
build=json.loads((OUT/'build30-loaded-receipt.json').read_text(encoding='utf-8-sig'))
assert proof['success'] and proof['saved_entities']==14 and len(proof['protocol_checks'])==8
assert build['ExitCode']==0 and build['Match'] and len(build['LoadedModules'])==2

def write_existing(path,change):
    original=path.read_bytes();text=original.decode('utf-8-sig')
    result=change(text)
    assert hashlib.sha256(path.read_bytes()).digest()==hashlib.sha256(original).digest(),path
    path.write_text(result,encoding='utf-8')

def metadata(text,values):
    end=text.index('\n---',4);head=text[:end]
    for key,value in values.items():
        assert re.search(rf'^{key}:',head,re.M),key
        head=re.sub(rf'^{key}:.*$',f'{key}: {value}',head,flags=re.M)
    return head+text[end:]

names={'006':'客户端视野裁剪与特效三档LOD优化','008':'命中特效事件批量承载与生命周期优化',
       '007':'僚机对地导弹共用表现与脉冲预警优化'}
entry='''## 2026-10-10 正式接入与玩家 PIE 验收交付

用户明确要求“应用所有新的变更”，随后将本轮收尾改为“只剩下我打开PIE验收的那一步，FPS对照可以先不做”。因此已直接发布全部新资源和逻辑；该授权记录为正式应用决定，不冒充尚未收到的具体候选视觉通过。单项及整版FPS对照均暂缓，不作为本轮交付阻塞，也没有新FPS收益数字。

正式资源已通过Excel源表→导出→DataTable导入：ID5/52使用各自完整/简化/最简资源，ID5接入三个批量资源；ID36/45使用端点强度三档版本，主体仍为8节点、7.5/12cm与不透明材质。新增独立ID53登记僚机共用光点/实体尾迹，基础Scale=1；真实 `DA_WingmanGroundMissile` 已保存表现Profile与红圈样式，Profile的BatchSystem与ID53一致。原52行ID和基础Scale、共享ID2及重防号专属路由均保留。

三个批量资源输入版本1已保存，正式目录持有专用NDC。逐命中寿命按认证包络1.25秒与原释放上限取小值；读取起点仍与F+1实际出生对齐。读取器的非默认位置、旋转、缩放、颜色、随机输入、寿命、Slot/Generation；终止去重；双World隔离；离屏回收且回屏不重播；代次复用；空闲唤醒无旧记录；Reset淘汰旧发布；三档各一个共享组件；资源缺失及元数据不符的同档F+1单次回退均已技术核对。元数据缺失测试在Play前临时准备，退出后已恢复正式版本1。

真实僚机对地125枚入口使用正式配置完成双客户端核对；停止补给后保留原权威8秒寿命，接收结束并清理光点、红圈与尾迹。此前125/250/500枚、完整范围离屏、回屏重新增长及脉冲/长度上限证据保留。静态检查与源码版 `GuLiStrikeEditor Win64 Development` 构建通过（最新build30、退出码0），加载DLL和BuildId已核对。

收尾地图 `/Game/Maps/LVL_CommanderMassPrototype` 已保存并回读14个相关实体：9个默认停用的预览、4个观察相机、1个指南。正常玩法按保存引用运行全部新变更，不需要候选安装脚本。集中对照的可选助手只切镜头、启动既有真实入口，不修改资源、协议元数据或默认开关。操作见[玩家PIE验收指南](../../outputs/performance/20261009-client-three-optimizations/manual-review-guide.md)。

当前为 `verification / partial`：开发、正式应用和技术交付已完成，剩余活动步骤为玩家PIE视觉及玩法验收（包含快速转镜头、总览返回、屏外死亡/改令、双枪历史姿态、晚加入等实际操作）；FPS采样单列为用户暂缓。技术输入检查、玩家视觉决定及性能收益分别记录，未标记整版性能通过。

证据：[正式管线读回](../../outputs/performance/20261009-client-three-optimizations/client-three-formal-readback.json)、[功能核对](../../outputs/performance/20261009-client-three-optimizations/applied-functional-receipt.json)、[源码构建加载](../../outputs/performance/20261009-client-three-optimizations/build30-loaded-receipt.json)、[最终交付读回](../../outputs/performance/20261009-client-three-optimizations/client-three-delivery-readback.json)、[本轮增量归档](../Archive/20261010-三项客户端优化正式接入与PIE验收交付.md)。

'''

for number,name in names.items():
    path=ROOT/'Progress/DevelopmentDocumentation'/f'20261009-{name}.md'
    def update(text,number=number):
        text=metadata(text,{'status':'verification','verification':'partial','updated':"'2026-10-10'",
            'summary':'本项代码、正式资源与数据管线已接入游戏；源码构建、输入契约及相关生命周期检查通过，验证地图已保存回读。待玩家PIE验收，FPS对照按用户要求暂缓。',
            'next_action':'玩家直接打开LVL_CommanderMassPrototype的PIE验收视觉及玩法效果；FPS对照暂缓。',
            'status_note':'用户已明确授权应用全部新变更。正式引用、默认开关、资源输入版本及真实僚机Profile/预警已落盘并回读；最新源码构建和双客户端相关技术检查通过。测试场景已保存并完成实体数据核对，待玩家验证效果；不再以资源认证或正式切换作为剩余开发事项。性能未采样，用户视觉通过尚未记录。'})
        at=text.index('\n## ')
        if '## 2026-10-10 正式接入与玩家 PIE 验收交付' not in text:text=text[:at]+'\n'+entry+text[at:]
        text=text.replace('## 2026-10-10 手动 PIE 审核入口','## 2026-10-10 早期手动 PIE 审核入口（历史，已被正式接入替代）')
        text=text.replace('## 2026-10-10 实施状态与当前卡点','## 2026-10-10 正式接入前实施状态（历史）')
        text=text.replace('## 本轮事实与技术边界','## 原方案登记时事实与技术边界（历史）')
        text=text.replace('## 未来实施设计','## 已实现的接口与运行设计')
        text=text.replace('## 未来验证与采用规则','## 验证与采用规则（FPS按最新指示暂缓）')
        # Composite old checkboxes are split so completed development is not
        # confused with the remaining human acceptance or deferred benchmarks.
        lines=[]
        for line in text.splitlines():
            if line.startswith('- [ ] '):
                body=line[6:]
                if '性能' in body or '视觉' in body or '分镜' in body or '生命周期、双客户端及晚加入' in body:
                    if '用户视觉审核通过后仅切换' in body:
                        line='- [x] 按用户直接应用授权切换僚机正式表现配置，读回引用并检查局部回退；视觉通过仍待玩家确认。'
                    elif '旧资源' in body and 'W₀' in body:
                        line='- [x] 保存旧资源、参数、引用和缩放链，冻结W₀=65.6000009775cm；脉冲与尾迹提供可播放入口。'
                    elif '性能' in body:
                        line='- [ ] '+body+'（用户明确暂缓FPS对照；已有技术检查另见本轮证据。）'
                    else:
                        line='- [ ] '+body+'（可播放入口和技术交付已完成，玩家PIE效果验收待执行。）'
                else:line=line.replace('- [ ]','- [x]',1)
            lines.append(line)
        text='\n'.join(lines)+'\n'
        text=text.replace('本轮仅完成方案与文档；下面的实施项均未执行。','以下勾选按2026-10-10正式接入证据更新；玩家效果验收与用户暂缓的FPS对照分别记录。')
        text=text.replace('`W₀` 尚未冻结。实施前必须核对','`W₀` 已冻结为65.6000009775cm，证据见本轮正式交付与旧缩放链读回。原方案要求实施前核对')
        text=text.replace('## 2. 当前基线与待冻结记录','## 2. 保留的旧表现基线与冻结记录')
        text=text.replace('具体坐标与 Actor 名称在实施时核对现有布置后记录，本轮尚未创建。','实体已保存并回读，实际坐标、Actor名称及入口见本轮场景证据与玩家指南。')
        return text
    write_existing(path,update)
    requirement=ROOT/'Progress/RequirementDocument'/f'20261009-{name}.md'
    def update_requirement(text):
        text=metadata(text,{'updated':"'2026-10-10'",
            'next_action':'玩家打开现有地图PIE验收对应开发交付；FPS对照按用户要求暂缓。',
            'status_note':'需求保持approved。用户已授权全阶段开发并明确应用全部新变更；正式实现和验证入口已交付，待玩家PIE视觉与玩法验收，FPS对照暂缓。需求批准和直接应用授权不等于玩家效果或性能通过。'})
        at=text.index('\n## ')
        if '## 2026-10-10 正式应用与验收安排' not in text:
            text=text[:at]+'\n## 2026-10-10 正式应用与验收安排\n\n用户已要求应用所有新变更，正式代码、资源和数据管线已接入；对应开发文档为当前实施状态入口。当前剩余活动步骤为玩家PIE效果验收；FPS对照按用户最新指示暂缓。下文仅文档登记的范围描述保留为原方案历史。\n'+text[at:]
        if '本轮仅交付需求与开发方案，尚未实施或验证新增收益。' in text:
            text=text.replace('本轮仅交付需求与开发方案，尚未实施或验证新增收益。','对应开发已完成正式接入，待玩家PIE验收；新增性能收益未采样。')
        return text
    write_existing(requirement,update_requirement)

guide='''# 三项客户端优化：玩家 PIE 验收

全部新逻辑和资源已正式保存。**直接打开 `/Game/Maps/LVL_CommanderMassPrototype` 并 Play，正常玩法默认启用全部三项优化，无需安装候选或修改开关。** 本轮FPS对照按你的要求暂缓。

验收时观察：采矿绿色/建造紫色不透明光束保持7.5/12cm，远处端点按档减少；机枪枪口和命中近/中/远的核心反馈仍清楚，连续命中、离屏/回屏和停止无残留或旧事件重播；僚机对地导弹使用周期0.5秒的脉冲球形光点、实体尾迹及红圈，完全离屏后回屏从当前位置重新长尾迹。重防号仍走原专属路线。

集中观察区已保存9个默认停用预览、4个观察相机和1个指南，标签 `GuLiClientThreeReview`；光束/三档位于X约-17000至-12000、Y约-20500至-16800、Z1500cm。预览不会在正常游戏中自动增加负载。

需要集中对照时，在编辑器 Tools → Execute Python Script 执行 `Scripts/Performance/client_three_player_review.py`（旧arm脚本也只转入此助手）。它只提供镜头及真实压力入口，不改资源、输入版本或开关。然后自行按Play；若需要同进程一专服、Ground/Air双客户端，也可在Python控制台明确执行 `guli_client_three_review_start()`。

在编辑器的Python控制台运行以下操作；若输入模式是普通Cmd，前面加 `py `：

| 操作 | 命令 | 预期 |
|---|---|---|
| 三档及光束开始/峰值/停止循环 | `guli_client_three_review_view("fx")` | 两客户端看同一区域；每列固定档位，目录命中走正式批量入口 |
| 中/远观察 | `guli_client_three_review_view("middle")` 或 `("far")` | 保留轮廓和时序，端点减少；目录命中按完整范围分档 |
| 离屏及返回 | `guli_client_three_review_view("offscreen")`，稍后 `("near")` | 完全离屏后回收/暂停；恢复当前状态，旧瞬时事件不重播 |
| 停止区域预览 | `guli_client_three_review_stop_fx()` | 光束收束、瞬时特效按原生命周期结束 |
| 真实僚机对地入口 | `guli_client_three_review_ground(125)`，也支持250/500 | 需一专服及Air席位；等待真实机库部署，使用正式对地Definition、冻结落点和红圈 |
| 停止补给 | `guli_client_three_review_stop_ground()` | 不再补弹；已飞行导弹保留权威8秒寿命，结束时红圈/光点退出，尾迹再0.55秒消散 |
| 返回正常玩法镜头 | `guli_client_three_review_view("game")` | 返回各自玩家Pawn |

真实僚机入口使用保存的 `FlightEventsQAOrigin` 标记；`ClientThreeReview_Camera_Wingman` 为对应观察点。正常玩法也可直接选择Air席位、机库和僚机投弹，不必使用压力入口。局部回退开关仍保留，默认均为新行为（命中模式2）。

本轮技术检查已完成：源码构建及加载、正式Excel/导出/表/Profile读回、批量输入契约与生命周期、真实对地入口和结束清理、地图实体检查。请在PIE中确认快速转镜头/总览返回、屏外单位死亡与改令、双枪历史姿态、晚加入，以及新特效开始/峰值/停止/回屏的实际效果。玩家验收结果尚未记录；FPS、GT、GPU累计提升暂无新结论。
'''
write_existing(OUT/'manual-review-guide.md',lambda _:guide)

archive=ROOT/'Progress/Archive/20261010-三项客户端优化正式接入与PIE验收交付.md'
assert not archive.exists(),'Immutable archive already exists'
archive.write_text('''---
schema: guli-progress/v1
id: ARC-20261010-002
work_id: ''
kind: archive
role: root
title: 三项客户端优化正式接入与PIE验收交付
areas: [presentation, vfx, performance, combat, commander, ui, wingman]
categories: [art, gameplay, performance]
status: recorded
verification: partial
created: '2026-10-10'
updated: '2026-10-10'
summary: 全部新变更经用户直接应用授权正式接入，53行Effects及真实僚机Profile/预警保存读回；源码构建和相关双客户端技术检查通过，地图14实体已保存。待玩家PIE验收，FPS对照暂缓。
next_action: 玩家打开LVL_CommanderMassPrototype的PIE验收视觉及玩法效果；FPS对照暂缓。
relations:
  work_items: [WORK-20261009-006, WORK-20261009-008, WORK-20261009-007]
  related: [ARC-20261010-001]
status_note: 正式接入取代旧的临时候选安装流程；直接应用授权不等同于视觉通过。资源输入契约与实际生命周期已技术核对，完整玩家效果待确认，性能未采样。
---

# 三项客户端优化正式接入与PIE验收交付

用户要求“应用所有新的变更”，并补充“三项客户端性能优化开发与整版对照”应只剩玩家打开PIE验收，FPS对照先不做。本轮完成正式接入和技术交付，性能矩阵暂缓；保留此前四项优化与[早期手动审核入口归档](20261010-三项客户端优化手动PIE审核入口交付.md)的历史事实。

Excel `Data/Excel/GuLiStrikeVFX.xlsx` 的Effects正式写入ID5/52的简化/最简资源、ID5三个批量资源、ID36/45端点分档版本；新增ID53登记独立僚机光点/实体尾迹，Scale=1。正常导出和过滤导入产生53行，原52行ID/Scale及其他引用保持。批量资源输入版本1、目录专用Channel、真实对地Definition的Profile与红圈样式均已保存。新增系统/Profile位于 `/Game/GuLiStrike/FX/CommanderWeapons/`；仅 `/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundMissile` 接入该Profile，共享ID2与重防号路由保持。

批量命中的非默认位置/旋转/缩放/颜色/种子/寿命、F+1出生、终止去重、World隔离、离屏清理和不重播、代次复用、空闲唤醒、Reset旧行淘汰、三档各一组件、资源/元数据回退通过。共享寿命按资源包络1.25秒与原上限取小值，减少无粒子尾声的保留。新增两个PIE限定的输入/Reset接口及粒子数据读回字段只供计划内契约核对，不改变网络。真实125枚对地入口的双客户端Profile/红圈和结束清理通过；StopLoad保留权威8秒寿命，尾迹0.55秒尾声。此前125/250/500基本证据保留。

源码版 `D:/UnrealEngine-5.7` 的 `GuLiStrikeEditor Win64 Development` build29、build30均退出0；最终加载build30，BuildId为 `dd3ee083-a0fd-45c8-814e-67fe5ef95e31`，项目与引擎一致。DLL路径及哈希见[加载凭证](../../outputs/performance/20261009-client-three-optimizations/build30-loaded-receipt.json)。Python语法及相关差异静态检查通过。

最后更新并保存 `/Game/Maps/LVL_CommanderMassPrototype` 的14个实体：9个默认停用预览、4个镜头、1个指南，回读Map、位置、System、Scale、PolicyId、数量及触发文本。正常玩法直接使用正式引用；新玩家助手只切镜头和调用真实入口，旧arm文件转入该助手，退出不再还原成旧资源。集中操作、前置条件、预期效果见[玩家指南](../../outputs/performance/20261009-client-three-optimizations/manual-review-guide.md)。

证据：[正式数据读回](../../outputs/performance/20261009-client-three-optimizations/client-three-formal-readback.json)、[功能契约](../../outputs/performance/20261009-client-three-optimizations/applied-functional-receipt.json)、[地图实体](../../outputs/performance/20261009-client-three-optimizations/manual-review-scene-readback.json)、[最终交付读回](../../outputs/performance/20261009-client-three-optimizations/client-three-delivery-readback.json)。

开发文档转 `verification / partial`，剩余活动步骤为玩家PIE视觉及玩法验收，包含开始/峰值/停止/回屏、快速转镜头和总览、屏外单位行为、历史双枪姿态及晚加入等。没有标记用户视觉通过，也没有新FPS或累计收益；单项与整版性能对照按用户指示暂缓。
''',encoding='utf-8')
print(json.dumps({'success':True,'development_status':'verification','player_acceptance':'pending','performance':'deferred_by_user','archive':'ARC-20261010-002'}))
