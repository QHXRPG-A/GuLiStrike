"""Record the saved manual review and its limited smoke evidence, without adopting candidates."""
import json,re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'outputs/performance/20261009-client-three-optimizations'
scene=json.loads((OUT/'manual-review-scene-readback.json').read_text(encoding='utf-8'))
fx=json.loads((OUT/'manual-review-smoke-fx.json').read_text(encoding='utf-8'))['result']
ground=json.loads((OUT/'manual-review-smoke-ground.json').read_text(encoding='utf-8'))['result']
restore=json.loads((OUT/'manual-review-restore-receipt.json').read_text(encoding='utf-8'))['result']
assert scene['success'] and scene['saved'] and len(scene['entities'])==14
assert fx['success'] and len(fx['effects'])==2 and all(r['components']==1 and r['fallbacks']==0 for r in fx['effects'])
assert ground['success'] and ground['load']['groundMissile'] and ground['load']['peak']==125 and ground['load']['rejected']==0
assert all(r['wingman_components']>0 and r['warnings']>0 for r in ground['clients'])
assert restore['success'] and restore['phase']=='restored' and all(float(v)==0 for v in restore['batch_input_versions'])

entry='''## 2026-10-10 手动 PIE 审核入口

用户询问是否可以自行打开PIE审核。本轮已在现有 `/Game/Maps/LVL_CommanderMassPrototype` 保存14个相关实体：9个默认停用预览、4个观察相机、1个指南，并回读资源、缩放、位置和入口配置。

[`arm_client_three_visual_review.py`](../../Scripts/Performance/arm_client_three_visual_review.py)为一次手动PIE会话安装候选：仅对客户端World覆盖ID5/52三档及命中批量、ID36/45端点参数版本和真实僚机对地Definition的可选Profile。命中输入版本及真实投弹预警样式仅在内存中临时启用，退出PIE后恢复；不改正式Excel引用，不标记资源已认证。可直接在正常玩法观察，也可按[手动审核指南](../../outputs/performance/20261009-client-three-optimizations/manual-review-guide.md)进入光束/三档预览和125/250/500真实对地入口。

短启动核对：两个客户端候选安装成功；目录命中每客户端1个共享组件、回退0；真实对地入口峰值125、拒绝0，新光点/尾迹及预警已触发。客户端表现计数包含结束后的尾迹消散，不用它冒充服务器活跃量。退出后已回读确认三个批量资源输入版本还原0、预警样式还原空；随后可重新准备玩家审核。

证据：[地图实体](../../outputs/performance/20261009-client-three-optimizations/manual-review-scene-readback.json)、[命中入口](../../outputs/performance/20261009-client-three-optimizations/manual-review-smoke-fx.json)、[真实对地入口](../../outputs/performance/20261009-client-three-optimizations/manual-review-smoke-ground.json)、[退出还原](../../outputs/performance/20261009-client-three-optimizations/manual-review-restore-receipt.json)。本轮未修改原生源码，复用已构建接口；两份UE脚本及记录脚本语法检查通过。该核对不代替完整功能、资源认证、开始/峰值/停止及回屏的用户视觉决定，也不提供新的FPS收益结论。

'''
for name in ['客户端视野裁剪与特效三档LOD优化','命中特效事件批量承载与生命周期优化','僚机对地导弹共用表现与脉冲预警优化']:
    path=ROOT/'Progress/DevelopmentDocumentation'/f'20261009-{name}.md'
    text=path.read_text(encoding='utf-8-sig')
    marker='## 2026-10-10 手动 PIE 审核入口'
    if marker in text:
        start=text.index(marker);end=text.index('\n## ',start+len(marker))
        text=text[:start]+entry+text[end+1:]
    else:
        at=text.index('## 2026-10-10 实施状态与当前卡点')
        text=text[:at]+entry+text[at:]
    text=text.replace('最后保存验证区域、读回实体和新增视觉候选用户审核尚未完成；状态保留实施中。',
        '手动审核区已保存并回读，候选已完成短启动入口核对；完整功能、性能及用户视觉验收仍未完成，正式引用未切换，状态保留实施中。')
    text=re.sub(r'^status_note: (.*)$',lambda m:m.group(0) if '手动审核区14个实体' in m.group(1) else m.group(0)+' 手动审核区14个实体已保存回读，候选加载与退出还原已核对，完整验收和正式切换仍待完成。',text,flags=re.M)
    path.write_text(text,encoding='utf-8')
print('Recorded saved manual review; full acceptance and paired performance remain incomplete.')
