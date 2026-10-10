"""Append current frame diagnosis while preserving prior implementation records."""
from __future__ import annotations
import hashlib
import json
import re
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'outputs/performance/20261009-stress-frame-analysis'
DEVS=ROOT/'Progress/DevelopmentDocumentation'
items=[
 ('20261009-PIE游戏线程耗时与避障候选查询优化.md',
  '保留避障交叉/停止/改令/环境边缘的玩家反馈；查询已达预算，追加性能优先按截帧报告处理命中特效和Ship生命周期。',
  '本轮查询平均1.223ms、活动P95 1.926ms达标，纯飞行预测约0.499ms/引擎帧。整体GT64.345ms，主要追加热点为命中实例、网络事件与Ship集中生命周期；不再将15.5FPS直接归因于避障。'),
 ('20261009-采矿激光与机枪闪光性能优化.md',
  '按截帧证据给命中特效补真正新建/复用/完成计数，比较空闲池与稳定槽位批次；新组合继续正式使用，保留细节玩家反馈。',
  '用户已明确“直接替换成新特效新逻辑”，正式组合使用授权记录，细节逐帧效果反馈分开。命中特效平均GT独占8.174ms、P95真实帧17.389ms；活跃249/209、空闲池32/32。GPU Ribbon组单帧9.253ms不具有资产归属，不能全部计入工具光束。采矿/建造系统GT独占合计约0.160ms。'),
 ('20261009-场景UI来源注册与绘制缓存优化.md',
  '保留四叶缓存后裁剪/建造预览/HUD期限/Ship面板的玩家反馈；区分场景UI与全局Slate成本，性能按截帧热点继续推进。',
  '双客户端来源稳态发现0。原生每LocalPlayer Update约0.239/0.171ms、Paint约0.160/0.193ms；全局ProcessLocalPlayerSlateOperations约3.327ms及Prepass/PaintSlow另列，不将其全部算作场景UI缓存失效。'),
 ('20261009-非Mass飞行弹丸六项性能优化.md',
  '按最慢帧验证Ship服务器Actor复用及完整复位，保持物理移动/判定路径；复用可靠飞行批次解码/应用容量，随后做同负载收益对照。',
  '数据池等既有新逻辑保持开启。纯预测GT约0.499ms，客户端表现总计1.301/1.346ms；边界间创建+25,512、结束+25,431。最慢101.096ms帧出现Ship125个寿命回调及补建/移动峰值，属于下一轮新热点，不能把此前类别快照优化记为未应用。'),
]

def main():
    receipts=[]
    for name,action,detail in items:
        path=DEVS/name
        original=path.read_bytes();text=original.decode('utf-8-sig')
        marker='## 600移动单位 / 500弹丸截帧追加'
        assert marker not in text, 'Diagnosis already appended: '+name
        changed,count=re.subn(r'^next_action:.*$', 'next_action: '+json.dumps(action,ensure_ascii=False),text,count=1,flags=re.M)
        assert count==1,name
        note='用户已授权直接使用新特效/逻辑；600/500压力截帧已完成，追加热点修复尚未实施。'
        def note_update(match):
            old=match.group(1).strip()
            value=json.loads(old) if old.startswith('"') else old
            return 'status_note: '+json.dumps(value+' '+note,ensure_ascii=False)
        changed,count=re.subn(r'^status_note:\s*(.*)$',note_update,changed,count=1,flags=re.M)
        assert count==1,name
        changed=changed.rstrip()+f'\n\n{marker}\n\n压力复现平均15.55FPS，整帧64.328ms、GT64.345ms、GPU16.217ms，P95帧88.020ms。固定600移动单位、四来源目标500弹丸、专服双客户端，10秒热身/30秒计时；完整CSV、真实普通/P95/最慢帧和GPU实帧已留档。\n\n{detail}\n\n[截帧完整报告](../../outputs/performance/20261009-stress-frame-analysis/report.md)、[交互时间线与实际画面](../../outputs/performance/20261009-stress-frame-analysis/index.html)、[增量归档](../Archive/20261009-600移动单位500弹丸PIE截帧耗时分析.md)。本轮只诊断，未修改生产代码/资产或声称新增FPS收益；新优化控制保持，压力场景控制已恢复。\n'
        assert path.read_bytes()==original, 'Concurrent update; re-read '+name
        path.write_text(changed,encoding='utf-8')
        receipts.append({'path':str(path),'before_sha256':hashlib.sha256(original).hexdigest(),
                        'after_sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    (OUT/'progress-update-manifest.json').write_text(json.dumps(receipts,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'updated':len(receipts),'archive':'ARC-20261009-010'},ensure_ascii=False))

if __name__=='__main__':main()
