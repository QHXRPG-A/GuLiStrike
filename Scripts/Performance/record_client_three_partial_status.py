"""Record implementation evidence without treating candidate creation as acceptance."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
STATES = {
    '006': ('客户端视野裁剪与特效三档LOD优化',
        '裁剪、三档LOD、单位屏外5Hz及弹丸呈现分离已实现并通过源码构建；LOD候选已编译，完整功能、性能及用户视觉验收尚未完成。',
        '完成全部可见、混合可见、屏外功能核对与三轮配对采样；完成可播放审核后更新正式引用。',
        '运行代码及三档资源候选已存在。Effects五个可选字段已通过Excel源表、生成和DataTable导入；原52行完整档引用和Scale保留，新增正式引用待具体候选审核。单位按需姿态、历史枪口及所有边界回归尚未闭环。',
        '代码：共用完整Bounds决策、一次性0.15秒离屏回收、单位网络状态与呈现分离、5Hz错峰、按需姿态和飞行每槽位裁剪均已构建。资源：ID5/52简化和最简候选、ID36/45端点20/10/0候选已保存、编译。新候选通过PIE临时覆盖参与验证，正式表尚未切换。',
        {1,2,3,4,6,7,8,9,10}),
    '008': ('命中特效事件批量承载与生命周期优化',
        '命中队列、F+1缓冲、槽位和三档批量候选已构建；8次及600次同帧输入、位置、尾迹身份和双World隔离检查通过，资源认证及整版对照尚未完成。',
        '补齐完整变换、清理复用、混合LOD及回退回归，认证候选，再运行模式1/2与整版配对对照。',
        '组件位置、系统年龄、随机默认参数图和事件尾迹出生前生命周期问题已修复。600次同帧输入在第一客户端共用1个组件，第二客户端独立3次输入；F+1出生、独立位置和Slot/Generation尾迹身份检查通过。完整变换已接入逐命中矩阵，但非默认输入及全部退出边界仍需回归；InputVersion保持0，正式资源未切换。',
        'C++承载、去重、512初始/256增长槽位、Generation/Epoch隔离、World独立发布、按命中离屏/期限清理、最多三组组件及延后一帧单次回退已构建。三个批量档位已保存编译。运行证据为8次及600次同帧输入、两个客户端World隔离和对应尾迹身份，不代表完整三档视觉或所有回退通过。资源认证、正式引用、批量收益均未完成。',
        {1,2,4,5,7}),
    '007': ('僚机对地导弹共用表现与脉冲预警优化',
        '可选僚机表现配置、球形脉冲/实体尾迹和真实对地负载入口已构建；125/250/500枚双客户端基础检查通过，性能与用户视觉验收未完成。',
        '补齐125/250/500真实对地功能与三组配对采样，记录红圈成本，完成具体视觉审核及正式配置切换。',
        '实际旧Core横向直径W0已冻结为65.6000009775cm。125/250/500枚真实DA_WingmanGroundMissile每客户端分别有4/8/16个表现组件，预警来源分别为125/250/500、合并64圈。250/500检查包含离屏组件归零而逻辑继续、回屏恢复、脉冲直径范围、每枚尾迹不超过50米及8段上限；全部终止边界、功能和性能尚未验收。',
        '可选Profile及GroundWarningStyle、32枚批次、0.5秒脉冲、1.5–2.5W0直径、8/4/2段与50米上限、0.55秒消散、有界弧长历史、修订上传、真实投弹预警和125/250/500入口已实现。球形光点和尾迹Opaque/Unlit资源已保存编译。完全离屏清历史，回屏从当前位置增长；共享ID2与重防号正式路由保持独立。',
        {2,3,4,5,6,8,9,10,12}),
}

for number, (name, summary, next_action, note, details, completed) in STATES.items():
    path = ROOT/'Progress/DevelopmentDocumentation'/f'20261009-{name}.md'
    text = path.read_text(encoding='utf-8-sig')
    end = text.index('\n---',4)
    header, body = text[:end], text[end:]
    for key,value in {'status':'in_progress','verification':'partial','updated':"'2026-10-10'",
                      'summary':summary,'next_action':next_action,'status_note':note}.items():
        header = re.sub(rf'^{key}:.*$',f'{key}: {value}',header,flags=re.M)
    marker = '## 2026-10-10 实施状态与当前卡点'
    entry = (f'{marker}\n\n{details}\n\n{note}\n\n'
        '源码构建使用用户已授权的 `D:/UnrealEngine-5.7`、`GuLiStrikeEditor Win64 Development`；'
        '多个增量已成功构建，最新构建与资源运行状态以[本轮证据目录](../../outputs/performance/20261009-client-three-optimizations/)为准。'
        '构建成功不代表Niagara图、功能、视觉或性能已通过。此前15.5 FPS仅为旧压力测量，不能作为新增三项收益。\n\n'
        '单项及整版三轮10秒热身/30秒采样尚未完成，当前没有新的累计FPS结果。'
        '最后保存验证区域、读回实体和新增视觉候选用户审核尚未完成；状态保留实施中。\n\n')
    if marker in body:
        start=body.index(marker); after=body.index('\n## ',start+len(marker)); body=body[:start]+entry+body[after+1:]
    else:
        at=body.index('\n## ');body=body[:at]+'\n'+entry+body[at+1:]
    body=body.replace('当前只记录实施启动；未执行的构建、性能、场景和视觉验收不得记为通过。',
                      '本段记录实施授权来源；当前进度和验证边界以2026-10-10实施状态及元数据为准。')
    body=body.replace('## 未来任务清单——全部未执行','## 实施任务清单（勾选仅对应已实现内容）')
    # Task lists keep their order. A checked implementation task does not imply the
    # separate functional/performance/visual acceptance tasks are complete.
    task_heading='## 7. 任务清单' if number=='007' else '## 实施任务清单'
    start=body.index(task_heading); finish=body.index('\n## ',start+len(task_heading))
    section=body[start:finish]; lines=section.splitlines(); index=0
    for i,line in enumerate(lines):
        if re.match(r'- \[[ x]\] ',line):
            if index in completed:lines[i]=line.replace('- [ ] ','- [x] ',1)
            index+=1
    body=body[:start]+'\n'.join(lines)+body[finish:]
    # Preserve the original planning prose with an explicit historical scope.
    for old,new in {
        '## 本轮文档状态与关联':'## 2026-10-09 原方案登记记录（历史）',
        '## 本轮文档交付记录与关联':'## 2026-10-09 原方案登记记录（历史）',
        '## 本轮结果与实施边界':'## 2026-10-09 原方案边界（历史）',
        '## 未来静态检查、场景与运行验收':'## 验收计划与原方案登记时状态（历史）',
    }.items():body=body.replace(old,new)
    body=body.replace('本轮状态：需求已确认、方案已落盘；代码和资产实施、编译、功能、性能、视觉、正式切换和 Map 场景均未执行。',
                      '2026-10-09方案登记时状态：需求与方案落盘，当时未实施；2026-10-10当前代码、资源和部分运行结果见本文实施状态。')
    path.write_text(header+body,encoding='utf-8')
print('Updated three implementation records; performance and visual acceptance remain incomplete.')
