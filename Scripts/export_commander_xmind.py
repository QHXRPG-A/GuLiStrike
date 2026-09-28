"""Draft Xmind sheets exclusively from recursive UE asset readback.

This command does not read .uasset files or the C++ migration template. It writes
Markdown and semantic specs; run Xmind CLI after reviewing its dry-run billing.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAMES = {
    'CommanderOrders': '指挥官任务', 'Control': '公共控制', 'WaitForSafeExit': '等待安全退出',
    'Stopped': '持续停止', 'PrepareReplacementMove': '准备替换移动', 'SelectOrder': '选择任务',
    'TakeManualOrder': '取人工队列', 'TakeAutomaticOrder': '取自动任务', 'Idle': '空闲等待',
    'ExecuteOrder': '执行任务', 'FinishOrder': '统一收尾', 'Suspended': '暂停等待恢复',
    'StartOrder': '启动任务', 'ManualMove': '持续人工移动', 'ManualTransit': '持续运输',
    'ResourceJob': '资源任务', 'AcquireResource': '采矿分支', 'RecoverMiningPosition': '恢复采矿位',
    'SelectMiningPosition': '选择采矿位', 'TravelToMine': '持续前往矿位', 'ExtractResource': '持续采集',
    'ReturnCargo': '共用返货分支', 'SelectFactory': '选择工厂', 'TravelToFactory': '持续返厂',
    'UnloadCargo': '持续卸货', 'RecoverUnloadPoint': '更换卸货点', 'CycleHandoff': '采卸循环交接',
    'PrepareAutomaticRetry': '准备自动重试', 'FailManualResourceOrder': '报告人工任务失败',
    'WaitForResourceRetry': '按原间隔等待重试', 'ConstructionJob': '施工任务',
    'ReturnConstructionOrder': '退单并保留排除记录', 'RejectConstructionPosition': '排除失败位置',
    'ApproachConstructionSite': '施工准备', 'SelectConstructionDestination': '选择施工目的地',
    'TravelToConstructionSite': '持续前往工地', 'ClaimPositionOnArrival': '抵达后原子抢位',
    'ConstructBuilding': '持续施工', 'StrongholdAdvance': '据点推进任务',
    'StrongholdStageHandoff': '据点阶段交接', 'RejectUnreachableStronghold': '排除不可达据点',
    'SelectStronghold': '选择据点', 'AdvanceToStronghold': '持续推进', 'CaptureStronghold': '持续占领',
    'WaitForStronghold': '无目标限频等待',
}
FLAGS = ['CancelPending', 'Stopped', 'PendingMove', 'Active', 'Automatic', 'Queued', 'AutomaticReady',
         'WaitingTarget', 'Mining', 'Construction', 'Advance', 'Move', 'Return', 'Transit', 'Cargo',
         'Started', 'RetryReady', 'Suspended', 'WorkComplete', 'ReturnFirst', 'OrderTerminal',
         'ControlChanged', 'OperationStale']


def label(name):
    return NAMES.get(name, name)


def walk(state):
    yield state
    for child in state['children']:
        yield from walk(child)


def flags(raw):
    try:
        number = int(raw)
    except (ValueError, TypeError):
        return str(raw)
    names = [name for bit, name in enumerate(FLAGS) if number & (1 << bit)]
    extra = number & ~((1 << len(FLAGS)) - 1)
    if extra:
        names.append(hex(extra))
    return ' & '.join(names) or '无'


def conditions(nodes):
    if not nodes:
        return '无附加条件'
    parts = []
    for index, node in enumerate(nodes):
        props = node['properties']
        if {'Required', 'Forbidden', 'Phase', 'Result'} <= props.keys():
            text = f"必须 {flags(props['Required'])}；禁止 {flags(props['Forbidden'])}；Phase={props['Phase']}；Result={props['Result']}"
        else:
            text = node['type'] + ': ' + json.dumps(props, ensure_ascii=False)
        operand = (node['operand'] + ' ') if index else ''
        parts.append(f"{operand}[缩进{node['indent']}] {text}")
    return ' / '.join(parts)


def notes(lines, text):
    lines.extend('> ' + line for line in str(text).splitlines())


def state_sheet(asset):
    short = asset['asset'].split('.')[-1]
    lines = [f'# {short}｜实际资产层级', '']
    for root in asset['roots']:
        def render(state, level):
            if level > 6:
                raise ValueError('This Markdown exporter needs an extended mapping for asset depth > 4; never flatten it silently.')
            persistent = any(re.search(r'bCompleteOnReceipt=False', task['properties'].get('Operation', ''), re.I)
                             for task in state['tasks'])
            suffix = '｜持续 Running' if persistent else ''
            lines.append('#' * level + ' ' + label(state['name']) + suffix)
            notes(lines, f"真实状态：{state['path']}\nGUID：{state['id']}\n选择：{state['selection']}；启用：{state['enabled']}")
            notes(lines, '进入条件：' + conditions(state['enter_conditions']))
            if state['description']:
                notes(lines, state['description'])
            for task in state['tasks']:
                notes(lines, '任务：' + task['type'] + '\n参数：' + json.dumps(task['properties'], ensure_ascii=False))
            for edge in state['transitions']:
                target = edge['target_path'] or edge['target_type']
                notes(lines, f"出口：{edge['trigger']} → {target}；优先级 {edge['priority']}；启用 {edge['enabled']}；{conditions(edge['conditions'])}")
            notes(lines, '父状态转换按 UE 优先级继承；详见本树转换页。')
            lines.append('')
            for child in state['children']:
                render(child, level + 1)
        render(root, 2)
    return '\n'.join(lines)


def transition_sheet(asset):
    short = asset['asset'].split('.')[-1]
    lines = [f'# {short}｜实际转换路径', '']
    groups = defaultdict(lambda: defaultdict(list))
    all_states = [state for root in asset['roots'] for state in walk(root)]
    roots = {root['id'] for root in asset['roots']}
    for state in all_states:
        for edge in state['transitions']:
            target = edge['target_name']
            if edge['target_id'] in roots or '/Control/' in state['path'] or state['name'] in ('CommanderOrders', 'ExecuteOrder', 'FinishOrder', 'Suspended'):
                group = '上层打断、暂停与收尾'
            elif any(word in target for word in ('Recover', 'Retry', 'Reject', 'Fail', 'Wait', 'ReturnConstructionOrder')):
                group = '局部恢复与等待'
            else:
                group = '阶段推进与业务交接'
            groups[group][state['name']].append(edge)
    for group in ('阶段推进与业务交接', '局部恢复与等待', '上层打断、暂停与收尾'):
        lines.append('## ' + group)
        notes(lines, '本页为真实转换的阅读分类，不是额外的 UE 状态或执行顺序。')
        for source, edges in groups[group].items():
            lines.append('### 从 ' + label(source))
            # Combine trigger variants of the same destination/conditions without losing their actual values.
            buckets = defaultdict(list)
            for edge in edges:
                key = (edge['target_id'], edge['target_type'], edge['priority'], conditions(edge['conditions']), edge['enabled'])
                buckets[key].append(edge)
            for (_, target_type, priority, guard, enabled), same in buckets.items():
                target = label(same[0]['target_name']) if same[0]['target_path'] else target_type
                lines.append('#### → ' + target + ('（已禁用）' if not enabled else ''))
                notes(lines, f"触发：{' / '.join(e['trigger'] for e in same)}；优先级：{priority}\n{guard}\n转换 GUID：{' / '.join(e['id'] for e in same)}")
                notes(lines, '真实目标：' + (same[0]['target_path'] or target_type))
            lines.append('')
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=ROOT / 'Artifacts/CommanderStateTree/hierarchy-readback.json')
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'Artifacts/CommanderStateTree/HierarchyV2/ReviewSources')
    args = parser.parse_args()
    raw = args.input.read_bytes()
    report = json.loads(raw.decode('utf-8-sig'))
    if not report.get('read_only') or not report.get('success') or not report.get('four_bindings_match'):
        raise ValueError('Use a successful inspect_commander_state_trees.py readback including the four unit bindings.')
    assets = report['assets']
    if len(assets) != 3 or any(a.get('hierarchy_version') != '2' or a.get('dirty') or not a.get('compiled_matches_editor') for a in assets):
        raise ValueError('Three saved, compiled hierarchy-v2 assets are required.')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    overview = ['# 三棵指挥官 StateTree｜V2 资产审核', '', '## 真实资产与单位绑定']
    for asset in assets:
        short = asset['asset'].split('.')[-1]
        units = [unit for unit, path in report['unit_bindings'].items() if short in path]
        overview.extend([f'### {short}', f"> 兵种 ID：{'、'.join(units)}；{asset['state_count']} 个状态；深度 {asset['max_depth']}（根为 0）；持续适配任务 {asset['persistent_task_count']} 个。",
                         '> Schema：' + asset['schema'], '> 策略：' + json.dumps(asset['schema_properties'], ensure_ascii=False)])
    overview += ['', '## 阅读方式', '### 三张层级页对应 UE 的真实父子关系',
                 '### 三张转换页列出正常推进、局部恢复、上层打断',
                 '> 分类仅帮助阅读；每条边记录实际触发器、条件、目标 GUID 和优先级。',
                 '### 持续阶段保持 Running，由真实结果触发转换',
                 '### 进入条件、任务参数和全部出口保存在对应主题备注',
                 '', '## 资产维护', '### 在 UE 编辑、编译、保存树后重新导出',
                 '### 常规检查只读；V2 迁移不会覆盖已标记资产',
                 '', '## 审核证据', '### 实际资产递归回读', '> ' + str(args.input.resolve()),
                 '> SHA256：' + hashlib.sha256(raw).hexdigest(), '> UTC：' + report['exported_utc'],
                 '### 玩家运行效果待验收', '> Map：/Game/Maps/LVL_CommanderMassPrototype；导图及编译结果不代表运行效果通过。']
    drafts = {'Overview': '\n'.join(overview)}
    for asset in assets:
        key = asset['asset'].split('.')[-1].removeprefix('ST_Commander')
        drafts[key] = state_sheet(asset)
        drafts[key + '-Transitions'] = transition_sheet(asset)
    for name, markdown in drafts.items():
        path = args.output_dir / (name + '.md')
        path.write_text(markdown + '\n', encoding='utf-8')
        spec = {'version': 1, 'markdown': {'path': str(path.resolve())},
                'recipe': {'name': 'code-architecture', 'effective': 'code-architecture', 'createSkill': 'recipe-code-architecture'},
                # Overview is a short index; the other sheets preserve focused asset/edge views.
                'route': {'structuralType': 'hierarchy_tree', 'structureCommitment': 'preferred', 'toneTag': 'tech',
                          'density': 'light' if name == 'Overview' else 'standard'},
                # Use a free branching hierarchy: the CLI's TreeChart variants render as a vertical timeline.
                'baseline': {'skeleton': 'OrgChart-1', 'color': 'Dawn-#ffffff-MULTI_LINE_COLORS', 'diversify': False, 'seed': 'commander-hierarchy-v2'},
                'anchors': {'layout': [], 'group': [], 'focus': [], 'image': []}}
        (args.output_dir / (name + '.generate.json')).write_text(json.dumps(spec, ensure_ascii=False, indent=2), encoding='utf-8')
    manifest = {'source': str(args.input.resolve()), 'source_sha256': hashlib.sha256(raw).hexdigest(),
                'sheets': list(drafts), 'source_kind': 'actual_ue_assets', 'cli_executed': False}
    (args.output_dir / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(manifest, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
