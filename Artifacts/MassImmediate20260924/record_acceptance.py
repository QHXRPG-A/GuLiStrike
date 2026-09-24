from pathlib import Path
import re
root=Path('D:/UE5.7/test1/Progress')
paths=[root/'RequirementDocument/20260923-Mass分帧寻路与避障稳定性.md',root/'DevelopmentDocumentation/20260923-Mass分帧寻路与避障稳定性.md',root/'Gameplay/Mass移动与避障.md']
for p in paths:
 s=p.read_text(encoding='utf-8');head,body=s.split('---',2)[1:]
 head=re.sub(r'^next_action:.*$','next_action: 本次响应与随机站位已获玩家验收；大人口合法部署及完整环境回归另列后续范围。',head,flags=re.M)
 head=re.sub(r'^status_note:.*$','status_note: 用户明确反馈“符合预期，收尾并总结”，本次即时响应和随机站位表现验收通过；未覆盖的大规模及环境矩阵不追加声称通过。',head,flags=re.M)
 if 'DevelopmentDocumentation' in str(p):
  head=re.sub(r'^status:.*$','status: done',head,flags=re.M)
  head=re.sub(r'^verification:.*$','verification: passed',head,flags=re.M)
  body=body.replace('玩家已明确表示即时响应延迟较低，并给出拥挤截图，要求随机唯一站位及包围盒宽度。此次实现和动态核对已完成，新站位的玩家视觉反馈待确认，故保持verification/partial，不标记全部验收完成。','玩家先确认即时响应延迟较低，并给出拥挤截图；随机唯一站位及包围盒间距修复后，明确反馈“符合预期，收尾并总结”。本次功能置done/passed；该验收不扩大为上文未覆盖的大人口、弱网和完整环境矩阵已通过。')
 elif 'RequirementDocument' in str(p):
  body=body.replace('- [ ] 玩家确认新随机站位的实际视觉表现；复核墙角、窄道、矿体、机甲和导航变化的完整矩阵。','- [x] 玩家明确确认新随机站位“符合预期”；本次收尾。\n- [ ] 完整墙角、窄道、矿体、机甲和导航变化矩阵另列后续回归。')
 body+='\n## 玩家验收确认（2026-09-24）\n\n用户反馈：“符合预期，收尾并总结”。本次即时响应与随机唯一站位已获确认，源码版编译和PIE动态证据见上文。采样诊断已关闭，当前PIE保持运行。大人口部署及未覆盖的完整回归继续按各自边界记录。见[验收增量记录](../Archive/20260924-Mass随机站位玩家验收.md)。\n'
 p.write_text('---'+head+'---'+body,encoding='utf-8')
p=root/'Archive/20260924-Mass随机站位玩家验收.md'
assert not p.exists()
p.write_text('''---
schema: guli-progress/v1
id: ARC-20260924-003
work_id: ''
kind: archive
role: root
title: Mass随机站位玩家验收
areas: [commander, network, performance]
categories: [gameplay, performance]
status: recorded
verification: passed
created: '2026-09-24'
updated: '2026-09-24'
summary: 用户在即时移动、随机唯一站位及模型包围盒间距修复后明确确认符合预期，要求收尾。
next_action: 本次关闭；大人口合法部署和未覆盖的完整环境回归保持后续记录。
relations:
  work_items: [WORK-20260923-003]
status_note: 验收依据为用户“符合预期，收尾并总结”；只确认本次响应和站位表现，不将未覆盖矩阵记为通过。
---

# Mass随机站位玩家验收

用户在源码版编译与PIE采样后明确反馈：“符合预期，收尾并总结”。即时响应、同条命令随机一对一站位，以及按选中最大模型实际包围盒宽度展开的表现完成本次验收。

[交付归档](20260924-Mass即时移动与随机站位交付.md)保留编译、BuildId、100人/混合50人动态数据和未覆盖范围的原始事实，不修改其当时“待玩家确认”的历史结论。本条补充后续验收事实，开发状态置done/passed。

收尾时关闭`guli.Commander.MoveLatencyDiagnostics`并回读为0；没有关闭当前PIE，三个PIE世界仍存在。没有新增或运行自动化测试，没有修改人口容量规则。大人口部署、真实重连、弱网及完整障碍矩阵尚未全覆盖；现有受阻保持移动意图的规则不改为全部保证到达。
''',encoding='utf-8')
print('Recorded acceptance as ARC-20260924-003.')
