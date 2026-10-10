"""Aggregate paired evidence without combining nested CPU scopes or P95s."""
from __future__ import annotations
import json, statistics
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'outputs/performance/20261009-implementation'
ORDER=['width','opaque','nodes16','nodes8','collision','solver','gpu','flash','runtime','flight']

def counter(leg,world,suffix):
    values=leg['counters']['worlds'][world]['profile']['counters']
    matches=[v for k,v in values.items() if k==suffix or k.endswith('/'+suffix)]
    assert len(matches)<=1,(world,suffix)
    return matches[0] if matches else None

def change(new,old):
    return 100*(new/old-1) if old else None

def publish_report(report):
    publication=json.loads((OUT/'formal-reference-proposal.json').read_text(encoding='utf-8'))
    adopted=publication.get('approval')=='approved_by_user' and publication.get('applied') is True
    visual_decision='用户已验收、已正式接入；收益未证实' if adopted else '按用户要求，待视觉验收；收益未证实'
    details={'avoidance':[],'flight':[],'scene_ui':[],'server_collision':[],'vfx_cpu':{},
        'scope':'Native means are per engine frame in each World; native P95 is active frame/batch P95. Means may be summed across Worlds, P95s may not. Trace exclusive scopes and all-thread work are kept separate.'}
    for pair in report['cases']['runtime']['pairs']:
        b,c=pair['baseline'],pair['candidate']
        old,new=counter(b,0,'Avoidance.QueryMs'),counter(c,0,'Avoidance.QueryMs')
        details['avoidance'].append({'pair':pair['pair'],'baseline':old,'candidate':new,
            'mean_change_percent':change(new['mean_per_engine_frame'],old['mean_per_engine_frame']),
            'mean_target_met':new['mean_per_engine_frame']<=1.6,'p95_target_met':new['p95_active_frame']<=3})
        for world in [1,2]:
            details['scene_ui'].append({'pair':pair['pair'],'world':world,
                'registry':c['counters']['worlds'][world]['registry'],
                'scopes':{name:counter(c,world,'SceneUI.'+name) for name in ['SourcesMs','OutlineCaptureMs','PlacementCaptureMs','WorldPanelsMs','UpdateMs','PaintMs']}})
    for pair in report['cases']['flight']['pairs']:
        b,c=pair['baseline'],pair['candidate']
        clients=[]
        for world in [1,2]:
            old,new=counter(b,world,'Flight.PresentationMs'),counter(c,world,'Flight.PresentationMs')
            clients.append({'world':world,'baseline':old,'candidate':new,
                'mean_change_percent':change(new['mean_per_engine_frame'],old['mean_per_engine_frame']),
                'p95_change_percent':change(new['p95_active_frame'],old['p95_active_frame']),
                'baseline_predictions':{source:counter(b,world,'Flight.Predictions.'+source) for source in ['Commander','Ground','Wingman','Ship']},
                'candidate_predictions':{source:counter(c,world,'Flight.Predictions.'+source) for source in ['Commander','Ground','Wingman','Ship']},
                'baseline_actor_active':counter(b,world,'Flight.ActorActive'),
                'candidate_actor_active':counter(c,world,'Flight.ActorActive'),
                'candidate_data_active':counter(c,world,'Flight.DataActive'),
                'baseline_snapshot':b['counters']['worlds'][world]['flight'],
                'candidate_snapshot':c['counters']['worlds'][world]['flight']})
        mean_old=sum(x['baseline']['mean_per_engine_frame'] for x in clients)
        mean_new=sum(x['candidate']['mean_per_engine_frame'] for x in clients)
        details['flight'].append({'pair':pair['pair'],'clients':clients,'summed_mean_change_percent':change(mean_new,mean_old)})
        details['server_collision'].append({'pair':pair['pair'],'candidate':{
            name:counter(c,0,'Projectile.'+name) for name in ['SnapshotMs','GridMs','CandidatesMs','NarrowPhaseMs','WorldSweepMs','SettleMs']}})
    for case in ORDER[:8]:
        rows=[]
        for pair in report['cases'][case]['pairs']:
            row={'pair':pair['pair']}
            for key in ['baseline','candidate']:
                trace=pair[key]['trace'];assert trace and trace['cpu_eligible'],pair[key]['directory']
                def own(items):
                    if case=='flash':
                        return sum(x['exclusive_ms_per_frame'] for x in items if 'NS_MachineGun' in x['timer'] or x['timer']=='NS_Flash')
                    return sum(x['exclusive_ms_per_frame'] for x in items if 'NS_MiningLaser' in x['timer'] or 'NS_ConstructionLaser' in x['timer'])
                row[key]={'gt_system_exclusive_ms_per_engine_frame':own(trace['vfx']),
                    'all_cpu_system_work_ms_per_engine_frame':own(trace['all_cpu_vfx']),
                    'window_method':trace['window_method'],'directory':pair[key]['directory']}
            row['gt_change_percent']=change(row['candidate']['gt_system_exclusive_ms_per_engine_frame'],row['baseline']['gt_system_exclusive_ms_per_engine_frame'])
            row['all_cpu_change_percent']=change(row['candidate']['all_cpu_system_work_ms_per_engine_frame'],row['baseline']['all_cpu_system_work_ms_per_engine_frame'])
            rows.append(row)
        details['vfx_cpu'][case]={'pairs':rows,'median_gt_change_percent':statistics.median(x['gt_change_percent'] for x in rows),
            'median_all_cpu_change_percent':statistics.median(x['all_cpu_change_percent'] for x in rows)}
    (OUT/'measured-stage-results.json').write_text(json.dumps(details,indent=2),encoding='utf-8')
    labels={'width':'仅加宽 1.5 倍','opaque':'加宽透明 → 加宽 Opaque','nodes16':'Opaque 80 → 16 节点','nodes8':'Opaque 80 → 8 节点',
        'collision':'禁用 Spark 表现碰撞','solver':'仅禁用 Beam 力求解','gpu':'Beam GPU 候选','flash':'枪口去表现碰撞候选',
        'runtime':'运行时局部回退开关 → 优化','flight':'仅 Actor 池 → 数据池'}
    decisions={'width':visual_decision,'opaque':visual_decision,
        'nodes16':'不采用；保留 80 节点','nodes8':'不采用；保留 80 节点',
        'collision':'仅保留审核候选；端点行为变化，默认保留原碰撞','solver':'不采用；收益不稳定',
        'gpu':'不采用；GPU 平均和 P95 明显退化','flash':'保留原模块；收益不足以支持碰撞精简',
        'runtime':'避障达标；整帧差异仅作有限参考','flight':'数据池平均收益成立；P95 目标未达到'}
    lines=['# 四项 PIE 性能优化实施与对照结果','',
        ('代码已实施并通过源码 UE5.7 Editor 构建；用户已认可四个视觉版本，ID36/45/5/52 已经 Excel 管线切换并完成原生与消费者回读。技术、性能与视觉验收分别记录，其他玩家功能验收待完成。'
         if adopted else '代码已实施并通过源码 UE5.7 Editor 构建；技术功能核对、性能测量和用户视觉验收分别记录。正式特效路径尚未切换，玩家和视觉验收待完成。'),
        '', '每项三对，热身 10 秒、采样 30 秒；同进程专服加双客户端、1280×720、质量档位 3，VSync/帧率限制关闭。数值为逐对相对变化的中位数，负值表示耗时下降。60 段最终有效 CSV 及对应 CPU 窗口均已核对。',
        '', '## 采用结论与整帧指标','',
        '| 对照 | 整帧平均 | GT 平均 / P95 | GPU 平均 / P95 | 结论 |','|---|---:|---:|---:|---|']
    for case,value in report['cases'].items():
        med=value['median_change_percent'];gt=med['GameThreadTime'];gpu=med['GPUTime']
        lines.append(f"| {labels[case]} | {med['FrameTime']['mean']:+.2f}% | {gt['mean']:+.2f}% / {gt['p95']:+.2f}% | {gpu['mean']:+.2f}% / {gpu['p95']:+.2f}% | {decisions[case]} |")
    lines+=['','运行时对照只切换 `QueryOptimizations/SparseCells/ProjectionCache/BoundsCull/DataPool`，双方均已有注册表、形状模板和服务器域索引，并非完整旧构建对照。Bounds 开关同时改变实际客户端 Ship 预测量；其约 6.5% 的整帧差异不能归因于数据池或整个四项改造。弹丸独立对照只切换 DataPool，其他开关相同，四类来源的实际预测量接近。',
        '', '## 避障预算','', '| 对 | 旧查询平均 / 批次 P95（ms） | 新查询平均 / 批次 P95（ms） | 平均 ≤1.6 / P95 ≤3 |','|---|---:|---:|---|']
    for row in details['avoidance']:
        b,c=row['baseline'],row['candidate']
        lines.append(f"| {row['pair']} | {b['mean_per_engine_frame']:.3f} / {b['p95_active_frame']:.3f} | {c['mean_per_engine_frame']:.3f} / {c['p95_active_frame']:.3f} | 达到 / 达到 |")
    lines+=['','600 个存活移动单位，查询范围、威胁排序和最终前缀保持；单独功能核对比较 11,684 个前缀，0 处差异。稀疏格子索引按原格子顺序跳过空格，并未缩短高速/大半径查询覆盖范围。',
        '', '## 弹丸平均与 P95','', '| 对 / 客户端 | Actor 平均 / P95（ms） | 数据池平均 / P95（ms） | 平均 / P95 变化 |','|---|---:|---:|---:|']
    for pair in details['flight']:
        for row in pair['clients']:
            b,c=row['baseline'],row['candidate']
            lines.append(f"| {pair['pair']} / {row['world']} | {b['mean_per_engine_frame']:.3f} / {b['p95_active_frame']:.3f} | {c['mean_per_engine_frame']:.3f} / {c['p95_active_frame']:.3f} | {row['mean_change_percent']:+.2f}% / {row['p95_change_percent']:+.2f}% |")
    mean_change=statistics.median(x['summed_mean_change_percent'] for x in details['flight'])
    lines+=['',f'两个客户端平均相加后，中位下降 {-mean_change:.2f}%。P95 分开统计，下降未达到 10%，不能判定整个平均/P95 目标通过。整帧平均约 64 ms，差异在波动范围内，仍受其他工作限制。',
        '', '原生入口 `gs.Flights.Load 500 45` 保持专服三来源各 125 枚，客户端窗口平均实际预测约 409–414 枚，来源比例和差异见结构化结果。数据池只保留 Ship 网格 Actor：窗口平均 Actor 活跃约 460 → 90–93，Actor 容量 768 → 128，数据容量 768；容量减少不等于操作系统工作集下降。',
        '', '## 特效分项','', '| 候选 | GT 系统独占平均变化 | 所有 CPU 线程的系统工作量变化 |','|---|---:|---:|']
    for case,row in details['vfx_cpu'].items():
        lines.append(f"| {labels[case]} | {row['median_gt_change_percent']:+.2f}% | {row['median_all_cpu_change_percent']:+.2f}% |")
    lines+=['','落点火花表现碰撞候选三对 GT 系统独占约 1.48–1.61 → 0.276–0.280 ms/引擎帧，所有 CPU 工作量约 1.62–1.75 → 0.422–0.426 ms。最终模块回读确认 Collision 在 Spark 上，Beam/Beam001 无 Collision 模块；该候选关闭 Spark Collision，会改变端点碰撞行为。服务器判定保留。该候选超过 20% 分项目标，但默认保留原火花碰撞，不能把该收益当作保持端点表现的正式 Opaque 版本收益。',
        '', '80/16/8 节点、力求解精简和 GPU 候选均已制作、编译、保存及三对采样。16/8 和力求解收益不稳定，保留 80 节点与既有力求解；GPU 平均 +47.71%、P95 +86.65%，不采用。枪口/命中已分别复制为项目资源，六层及事件链保留；枪口移除 Sparks/Debris 表现碰撞仅降低约 0.04 ms GT，整帧收益未证实，保留原模块。',
        '', '## UI 与进一步候选','',
        '客户端注册表稳态全世界发现迭代为 0；来源生成/销毁、晚到身份、Widget 重挂载、HUD 空提交/F+2 过期、跨屏/近裁剪面以及 Ship 失去控制/重新控制/重生均有真实回读。当前 UI 单客户端 Update 约 0.2–0.3 ms，Capture 名单与动画更新分离，叶节点内部模板/投影缓存已实施。未作完整旧 UI 构建对照，不能宣称四阶段 UI 的独立整帧收益。拆叶缺少新增收益证据，保留现有结构。',
        '', '曲线系数预计算已评估：独立弹丸对照预测部分约 0.46 ms/帧（两个客户端合计），远小于 Ship 蓝图/NS_Flash 等热点，尚无收益与轨迹等价证据，保留原随机求值序列。较小 Niagara 批次会改变 1024/64 槽与粒子索引合同并增加组件，当前没有测量支持，保留容量。服务器候选去重约 0.08–0.09 ms/帧，保留原 TSet 与首次平局顺序；已优化域索引/工作容量，并按域独立取快照，保持士兵 200 ms 历史及 5/30 Hz。',
        '', '## 内存与样本边界','', '| 对照 | 进程物理内存均值变化 | 虚拟内存均值变化 |','|---|---:|---:|']
    for case in ['runtime','flight','collision','gpu']:
        med=report['cases'][case]['median_change_percent']
        lines.append(f"| {labels[case]} | {med['PhysicalUsedMB']['mean']:+.2f}% | {med['VirtualUsedMB']['mean']:+.2f}% |")
    lines+=['', '进程内存包含编辑器、三个 World、资产与缓存；没有已证实的物理/虚拟内存收益，OS 工作集变化不能替代数据池占用测量。逐对绝对平均/P95 在 CSV 汇总和 JSON 中。',
        '', 'CPU 使用 `insights-windowed/summary.json`；早期资产样本以最后 CSV 帧数的完整 GT 帧恢复，边界误差 ≤0.25 秒，仅作分项诊断。最终运行时/弹丸和宽度第3对以原始 QPC 精确截取 30 秒，排除桥接命令帧。旧无窗口 `insights/` 汇总、修复域分类前的6段，以及原 `width-p3-Wide` 损坏 CPU 均不参与最终结论；宽度第3对已成对补采。初次 QPC 导出错用旧偏移造成空窗口，保留拒绝记录，现已重新映射原始追踪，未替换运行时原始数据。',
        '', '`all_cpu_vfx` 是 CPU 工作量，不能与 GT 延迟相加；Inclusive 与子项不相加，两个客户端 P95 不相加。注册表/模板/服务器域索引的收益不能由局部回退对照推算。',
        '', '## 构建、验证与保存入口','',
        '源码引擎 `D:/UnrealEngine-5.7`，目标 `GuLiStrikeEditor Win64 Development`，最后构建退出 0；编辑器实际加载该源码引擎与项目 DLL，BuildId 均为 `dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。已有相关回归：数据池路径14项及 Actor 路径1项通过；未新增测试框架。',
        '', 'Map `/Game/Maps/LVL_CommanderMassPrototype` 已最后保存并回读20个关闭状态的候选 Actor、3个指南和1个观察相机，以及原生三来源标记。光束行在 `(-16000,-20500/-18500,1500)`，相机 `(-8000,-30000,12500)`；实际建造/导航区域 `(15000,72000,902)`，三来源标记 `(0,65000,902)`。客户端控制台可用 `gs.Perf.Review start PR_Mining_Opaque__Actor`、`gs.Perf.Review start PR_Construction_Opaque__Actor`，`gs.Perf.Review stop` 关闭该客户端全部预览。',
        '', '采矿 5→7.5 cm，建造 8→12 cm；两个系统独立，原曲线、组件缩放、绿色/紫色及 Spark 保留，Opaque+Unlit 光束按原透明曲线收束宽度至0。制作脚本写绝对值，重跑不叠乘。'+
        ('正式 ID36/45/5/52 已切换至各自项目资源，基础缩放1/1/2/1及重防号动态倍率保留；原生目录、DataTable 全52行、蓝图双枪口绑定和 Cook 依赖均已回读。用户决定：“认可，切换这四项引用”。'
         if adopted else '正式 ID36/45/5/52 仍引用原资源，基础缩放1/1/2/1及重防号动态倍率保留；只修正了 ID36 的旧建造共用注释。'),
        '', '[可播放开始/峰值/停止对照](visual-review/index.html) · [全部逐对证据](paired-evidence-final.json) · [分项测量](measured-stage-results.json) · [加载构建](loaded-bounds-final-binary.json) · [已有回归](existing-regressions-final.json) · [功能核对](runtime-functional-review.json) · [生命周期核对](runtime-lifecycle-final-review.json) · [保存场景回读](final-saved-scene-readback.json)',
        '', ('[正式切换回读](formal-reference-final-readback.json) · [四行 Excel 差异](formal-reference-excel-diff.json)。本次用户视觉验收与正式接入已完成；避障、UI 和飞行等玩家功能验收仍分别保留，弹丸 P95 和整帧收益边界未改变。'
             if adopted else '参考视觉及用户验收待完成；没有用户视觉确认前，不把候选标成已通过或切换正式战斗引用。')]
    (OUT/'implementation-results.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')

def main():
    exclusions=set(json.loads((OUT/'runtime-pre-domain-fix-exclusion.json').read_text())['excluded'])
    cases={}
    for file in sorted((OUT/'paired').glob('*/result.json'),key=lambda p:p.stat().st_mtime):
        record=json.loads(file.read_text())
        if str(file.relative_to(OUT)) in exclusions or not record.get('adoption_eligible'):continue
        record['directory']=file.parent.name
        summary=file.parent/'insights-windowed/summary.json'
        record['trace']=json.loads(summary.read_text()) if summary.exists() else None
        cases.setdefault(record['case'],{}).setdefault(record['pair'],{})[record['variant']]=record
    report={'scope':'Paired whole-process CSV; per-World native scopes; CPU trace eligibility is independent of CSV. Pair ratios compare their own baseline. Local rollback controls do not recreate the original entire build.', 'cases':{}}
    variants={'width':('Source','Wide'),'opaque':('Wide','Opaque'),'nodes16':('Opaque','Nodes16'),'nodes8':('Opaque','Nodes8'),
        'collision':('Opaque','NoCollision'),'solver':('Opaque','NoSolver'),'gpu':('Opaque','GPU'),'flash':('Source','MuzzleNoCollision'),
        'runtime':('legacy_controls','optimized_controls'),'flight':('actor_pool','data_pool')}
    for case in ORDER:
        if case not in cases:continue
        base,candidate=variants[case];pairs=[]
        for pair,legs in sorted(cases[case].items()):
            if base not in legs or candidate not in legs:continue
            row={'pair':pair,'baseline':legs[base],'candidate':legs[candidate],'differences':{}}
            for metric in ['FrameTime','GameThreadTime','GPUTime','GPUFrameTime','PhysicalUsedMB','VirtualUsedMB']:
                if metric not in legs[base]['csv'] or metric not in legs[candidate]['csv']:continue
                b,c=legs[base]['csv'][metric],legs[candidate]['csv'][metric]
                row['differences'][metric]={key:100*(c[key]/b[key]-1) if b[key] else None for key in ['mean','p95']}
            pairs.append(row)
        report['cases'][case]={'pairs':pairs,'median_change_percent':{metric:{key:statistics.median(p['differences'][metric][key] for p in pairs if metric in p['differences']) for key in ['mean','p95']} for metric in pairs[0]['differences']} if pairs else {}}
    (OUT/'paired-evidence-final.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    publish_report(report)
    print(json.dumps({case:{'pairs':len(value['pairs']),'changes':value['median_change_percent']} for case,value in report['cases'].items()}),flush=True)

if __name__=='__main__':main()
