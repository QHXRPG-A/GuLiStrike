"""Build source-grounded Markdown sheets for the three Commander StateTrees.

Only text source and existing editor readback reports are inspected. UE binary
assets are never read by this script. The generated Markdown feeds Xmind CLI.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
SOURCE = ROOT / "Source/GuLiStrike/Commander/Behavior/GuLiCommanderStateTreeAssetCommands.cpp"
SOURCE_REL = SOURCE.relative_to(ROOT).as_posix()
REPORT = ROOT / "Artifacts/CommanderStateTree/tree-assets.json"
BUILDER_READBACK = ROOT / "Artifacts/Map2300/20260923/builder-tree-readback.json"


@dataclass(frozen=True)
class State:
    name: str
    required: str
    forbidden: str
    task: str
    phase: str = "Any"
    result: str = "Any"
    line: int = 0


lines = SOURCE.read_text(encoding="utf-8-sig").splitlines()


def find_line(fragment: str) -> int:
    matches = [i for i, line in enumerate(lines, 1) if fragment in line]
    if len(matches) != 1:
        raise AssertionError((fragment, matches))
    return matches[0]


def parse_add(fragment: str, *, required_override: str | None = None) -> State:
    line_no = find_line(fragment)
    line = lines[line_no - 1].strip()
    match = re.search(r'Add\(TEXT\("([^"]+)"\),\s*(.*?)\);', line)
    if not match:
        raise AssertionError(line)
    name = match.group(1)
    args = [item.strip() for item in match.group(2).split(",")]
    if len(args) not in (3, 5):
        raise AssertionError((name, args))
    required, forbidden, task = args[:3]
    if required_override:
        required = required_override
    return State(
        name=name,
        required=required,
        forbidden=forbidden,
        task=task.removeprefix("Step::"),
        phase=args[3].removeprefix("Phase::") if len(args) == 5 else "Any",
        result=args[4].removeprefix("Result::") if len(args) == 5 else "Any",
        line=line_no,
    )


COMMON = [
    parse_add('Add(TEXT("WaitingSafeExit")'),
    parse_add('Add(TEXT("Stopped")'),
    parse_add('Add(TEXT("PreparingReplacementMove")'),
    parse_add('Add(TEXT("SuspendedByExternalControl")'),
    parse_add('Add(TEXT("StartTask")'),
    parse_add('Add(TEXT("WorkUnitFinished")'),
    parse_add('Add(TEXT("ManualMove")'),
]
TRANSIT = parse_add('Add(TEXT("ManualTransit")')

MASS = [
    parse_add('Add(TEXT("SelectStronghold")', required_override="Active | Started | Advance"),
    parse_add('Add(TEXT("MoveToStronghold")', required_override="Active | Started | Advance"),
    parse_add('Add(TEXT("NoStronghold_Wait")', required_override="Active | Started | Advance"),
    parse_add('Add(TEXT("WaitForCapture")', required_override="Active | Started | Advance"),
    parse_add('Add(TEXT("RejectUnreachableStronghold")', required_override="Active | Started | Advance"),
    parse_add('Add(TEXT("StrongholdCaptured")', required_override="Active | Started | Advance"),
    parse_add('Add(TEXT("RetryStrongholdSelection")', required_override="Active | Started | Advance"),
    parse_add('Add(TEXT("AdvancingOrCapturing")', required_override="Active | Started | Advance"),
]

BUILDER = [
    parse_add('Add(TEXT("SelectConstructionDestination")'),
    parse_add('Add(TEXT("ReturnCancelledConstructionOrder")'),
    parse_add('Add(TEXT("MoveToConstructionSite")'),
    parse_add('Add(TEXT("WaitForBudgetedConstructionPath")'),
    parse_add('Add(TEXT("RetryConstructionPosition")'),
    parse_add('Add(TEXT("ClaimPositionOnArrivalAndConstruct")'),
    parse_add('Add(TEXT("WorkingOrWaitingForArrival")'),
]


def parse_mining(prefix: str) -> list[State]:
    assert prefix in ("Mining", "Return")
    result = []
    for line_no, line in enumerate(lines, 1):
        if "MineState(TEXT(" not in line:
            continue
        if prefix == "Return" and "if (Kind == Mining)" in line:
            continue
        match = re.search(r'MineState\(TEXT\("([^"]+)"\),\s*(.*?)\);', line)
        if not match:
            raise AssertionError(line)
        args = [item.strip() for item in match.group(2).split(",")]
        if len(args) not in (3, 4, 5):
            raise AssertionError((match.group(1), args))
        phase, work_result, task = args[:3]
        extra = args[3] if len(args) >= 4 else "0"
        exclude = args[4] if len(args) >= 5 else "0"
        required = "Active | Started | " + prefix
        if extra != "0":
            required += " | " + extra
        forbidden = "Blocked"
        if exclude != "0":
            forbidden += " | " + exclude
        result.append(
            State(
                name=prefix + "_" + match.group(1),
                required=required,
                forbidden=forbidden,
                task=task.removeprefix("Step::"),
                phase=phase.removeprefix("Phase::"),
                result=work_result.removeprefix("Result::"),
                line=line_no,
            )
        )
    return result


MINING = parse_mining("Mining")
RETURN = parse_mining("Return")
TAIL_MANUAL = parse_add('Add(TEXT("SelectManualTask")')
TAIL_AUTOMATIC = State(
    "SelectAutomaticTask",
    "AutomaticReady",
    "Blocked | Active | Queued",
    "TakeAutomatic",
    line=find_line('AutomaticReady, Blocked | Active | Queued, Step::TakeAutomatic'),
)
TAIL_BUILDER_AUTOMATIC = State(
    "SelectAutomaticConstructionSite",
    TAIL_AUTOMATIC.required,
    TAIL_AUTOMATIC.forbidden,
    TAIL_AUTOMATIC.task,
    line=TAIL_AUTOMATIC.line,
)
TAIL_IDLE = parse_add('Add(TEXT("Idle")')

TREES = {
    "Mass": {
        "title": "ST_CommanderMass｜据点推进｜18 状态",
        "asset": "ST_CommanderMass",
        "schema": "GuLiCommanderMassStateTreeSchema",
        "lifetime": "InitialOnce",
        "units": "扫荡者、战争机器",
        "groups": [("入口与人工控制", COMMON), ("据点推进", MASS), ("取单与空闲", [TAIL_MANUAL, TAIL_AUTOMATIC, TAIL_IDLE])],
        "readback": "Artifacts/CommanderStateTree/tree-assets.json（2026-09-21）",
        "focus": ["无目标时等待并重试", "不可达据点的拒绝与重新选择", "抵达后占领、完成条件"],
    },
    "Miner": {
        "title": "ST_CommanderMiner｜采矿返厂｜57 状态",
        "asset": "ST_CommanderMiner",
        "schema": "GuLiCommanderActorStateTreeSchema",
        "lifetime": "Persistent",
        "units": "矿车",
        "groups": [("入口与人工控制", COMMON + [TRANSIT]), ("Mining 分支", MINING), ("Return 分支", RETURN), ("取单与空闲", [TAIL_MANUAL, TAIL_AUTOMATIC, TAIL_IDLE])],
        "readback": "Artifacts/CommanderStateTree/tree-assets.json（2026-09-22）",
        "focus": ["矿点丢失、无目标和操作失败时的自动重试／手动失败", "满载或矿点枯竭后的选厂与返厂", "卸货点失败、工厂不可达与重新选择"],
    },
    "Builder": {
        "title": "ST_CommanderBuilder｜建造｜18 状态",
        "asset": "ST_CommanderBuilder",
        "schema": "GuLiCommanderActorStateTreeSchema",
        "lifetime": "Persistent",
        "units": "建造车",
        "groups": [("入口与人工控制", COMMON + [TRANSIT]), ("施工与换单", BUILDER), ("取单与空闲", [TAIL_MANUAL, TAIL_BUILDER_AUTOMATIC, TAIL_IDLE])],
        "readback": "Artifacts/Map2300/20260923/builder-tree-readback.json（2026-09-23，Compiled）",
        "focus": ["外据点施工单在途中遇到本据点新单时退单", "抵达后才抢占施工位，失败时重试", "已经占位施工时保持任务"],
    },
}


ZH = {
    "WaitingSafeExit": "等待安全退出",
    "Stopped": "停止",
    "PreparingReplacementMove": "准备替换移动",
    "SuspendedByExternalControl": "外部控制暂停",
    "StartTask": "启动任务",
    "WorkUnitFinished": "工作单元完成",
    "ManualMove": "手动移动",
    "ManualTransit": "手动运输",
    "SelectManualTask": "取手动任务",
    "SelectAutomaticTask": "取自动任务",
    "SelectAutomaticConstructionSite": "选自动施工单",
    "Idle": "空闲",
    "SelectStronghold": "选择据点",
    "MoveToStronghold": "前往据点",
    "NoStronghold_Wait": "无据点时等待",
    "WaitForCapture": "等待占领",
    "RejectUnreachableStronghold": "拒绝不可达据点",
    "StrongholdCaptured": "据点占领完成",
    "RetryStrongholdSelection": "重新选择据点",
    "AdvancingOrCapturing": "推进或占领中",
    "SelectConstructionDestination": "选择施工目的地",
    "ReturnCancelledConstructionOrder": "退回已取消施工单",
    "MoveToConstructionSite": "前往施工地点",
    "WaitForBudgetedConstructionPath": "等待预算寻路",
    "RetryConstructionPosition": "重试施工位置",
    "ClaimPositionOnArrivalAndConstruct": "抵达抢位并施工",
    "WorkingOrWaitingForArrival": "施工或等待抵达",
    "SelectFactoryWithCargo": "携货选工厂",
    "SelectMineTarget": "选择矿点",
    "WaitForMiningPosition": "等待采矿位置",
    "WaitForBudgetedMiningPath": "等待预算寻路",
    "MoveToMine": "前往矿点",
    "BeginExtraction": "开始采集",
    "RepositionForExtraction": "调整采集位置",
    "RejectUnreachableMiningPosition": "拒绝不可达采矿位",
    "CargoFull_SelectFactory": "满载后选厂",
    "MineDepleted_SelectFactory": "矿点枯竭后选厂",
    "LostMine_ReturnCargo": "矿点丢失后返货",
    "LostMine_Retry": "矿点丢失后重试",
    "LostMine_ManualFailure": "矿点丢失时手动单失败",
    "NoTarget_Retry": "无目标时重试",
    "NoTarget_ManualFailure": "无目标时手动单失败",
    "TryNextUnloadPoint": "尝试下一卸货点",
    "UnreachableFactory_SelectNext": "工厂不可达时重选",
    "FailedAction_Retry": "操作失败后重试",
    "FailedAction_ManualFailure": "操作失败时手动单失败",
    "ReturnToFactory": "返回工厂",
    "FactoryLost_SelectAgain": "工厂失效后重选",
    "Unload": "卸货",
    "FinishCycle": "完成循环",
    "WaitForActionOrRetry": "等待操作或重试",
}


def zh_name(state: State) -> str:
    base = state.name
    if base.startswith("Mining_"):
        base = base.removeprefix("Mining_")
    if base.startswith("Return_"):
        base = base.removeprefix("Return_")
    return ZH[base]


def render_tree(key: str, spec: dict) -> None:
    states = [state for _, group in spec["groups"] for state in group]
    assert len(states) == (57 if key == "Miner" else 18)
    assert len({state.name for state in states}) == len(states)
    text = [
        f'# {spec["title"]}',
        "",
        "## 资产配置",
        f'### 单位绑定：{spec["units"]}',
        f'> 资产路径：/Game/GuLiStrike/Commander/Behavior/{spec["asset"]}.{spec["asset"]}',
        f'### Schema：{spec["schema"]}',
        f'### 生命周期：{spec["lifetime"]}；自动启用',
        "",
        "## CommanderOrders｜根状态：按子状态顺序选择",
        "> UE 原图中以下状态全部是 CommanderOrders 的直接子状态；导图分组仅供审核，不表示额外的 UE 状态层级。",
        "> 每个子状态含一个 EnterCondition 和一个 BehaviorTask；成功时 GotoState(CommanderOrders)，重新按优先级选子状态。",
    ]
    number = 0
    for group_name, group in spec["groups"]:
        first, last = number + 1, number + len(group)
        text.extend([f"### {first:02d}–{last:02d} {group_name}｜审核分组", "> 同组编号是 C++ AddChildState 的原始优先顺序。"])
        for state in group:
            number += 1
            text.extend(
                [
                    f"#### {number:02d} {state.name}｜{zh_name(state)}",
                    f"> 必需 Flags：{state.required}；禁止 Flags：{state.forbidden}。Blocked = CancelPending | Stopped | PendingMove。",
                    f"> Phase = {state.phase}；Result = {state.result}；Task = {state.task}。成功返回 CommanderOrders。",
                    f"> C++ 来源：{SOURCE_REL}:{state.line}。",
                ]
            )
    text.extend(["", "## 审核关注点"])
    for focus in spec["focus"]:
        text.append(f"### {focus}")
    text.extend(
        [
            "",
            "## 核对依据与边界",
            f'### 生成逻辑：{SOURCE_REL}',
            f'### 已保存状态名回读：{spec["readback"]}',
            "### 运行时行为仍待玩家在 UE 场景中验收",
            "> 本图展示已保存状态名、生成代码中的条件／任务／优先级，不把编辑器编译状态等同于游戏运行验证。",
        ]
    )
    (HERE / f"{key}.md").write_text("\n".join(text) + "\n", encoding="utf-8")


def render_overview() -> None:
    text = [
        "# GuLiStrike｜三棵指挥官 StateTree 审核总览",
        "",
        "## 三棵资产与单位绑定",
        "### ST_CommanderMass｜据点推进｜18 状态",
        "> 扫荡者、战争机器共用此资产；Mass Schema；InitialOnce；每单位独立运行实例。",
        "### ST_CommanderMiner｜采矿返厂｜57 状态",
        "> 矿车使用；Actor Schema；Persistent；每车独立运行实例。",
        "### ST_CommanderBuilder｜建造｜18 状态",
        "> 建造车使用；Actor Schema；Persistent；每车独立运行实例。2026-09-23 仅此树更新接单、退单、移动与到位抢位阶段。",
        "",
        "## 读图规则",
        "### CommanderOrders 按子状态顺序选择",
        "> SelectionBehavior = TrySelectChildrenInOrder。编号表示优先级，不能当作线性执行流程。",
        "### 每个状态的准入、任务和成功回跳",
        "> 每个直接子状态都设 Required/Forbidden Flags、Phase/Result 条件和一个 BehaviorTask；OnStateSucceeded → CommanderOrders。",
        "### 导图分组只是审核导航",
        "> 具体资产的所有编号状态均直接挂在 UE 原图的 CommanderOrders 下；导图里的入口、业务、取单分组不属于 UE 原图。",
        "### 状态条件符号",
        "> Blocked = CancelPending | Stopped | PendingMove；Any 表示该筛选维度不限；Flags=0 表示没有该类标志约束。",
        "",
        "## 各树审核重点",
        "### Mass：无据点／不可达／占领完成后如何重选",
        "### Miner：采矿与返厂两组状态的目标丢失、失败和卸货",
        "### Builder：本地优先、途中退单、抵达抢位与已施工锁单",
        "",
        "## 来源与审查边界",
        f"### 状态条件与任务：{SOURCE_REL}",
        "### Mass／Miner 已保存状态名：Artifacts/CommanderStateTree/tree-assets.json",
        "### Builder 最新状态名：Artifacts/Map2300/20260923/builder-tree-readback.json",
        "### 单位绑定：Progress/Gameplay/指挥官/01-战局选兵与移动.md",
        "### 建造规则：Progress/DevelopmentDocumentation/20260922-导航内存优化与对局容量预算.md",
        "### 本图用于结构审核；运行时效果待玩家验收",
    ]
    (HERE / "Overview.md").write_text("\n".join(text) + "\n", encoding="utf-8")


def validate_readbacks() -> None:
    old = json.loads(REPORT.read_text(encoding="utf-8"))
    reported = {item["asset"].split(".")[-1]: item["states"] for item in old["assets"]}
    for key in ("Mass", "Miner"):
        names = [state.name for _, group in TREES[key]["groups"] for state in group]
        assert names == reported[TREES[key]["asset"]], key
    builder = json.loads(BUILDER_READBACK.read_text(encoding="utf-8"))
    assert builder["compiled"] is True
    builder_names = [state.name for _, group in TREES["Builder"]["groups"] for state in group]
    assert builder_names == builder["states"][1:]
    assert builder["states"][0] == "CommanderOrders"


def main() -> None:
    validate_readbacks()
    render_overview()
    for key, spec in TREES.items():
        render_tree(key, spec)
    print("Verified saved state names: Mass=18, Miner=57, Builder=18; wrote four Markdown sheets")


if __name__ == "__main__":
    main()
