"""Record the authorized implementation start without claiming validation."""
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
names={
'006':'客户端视野裁剪与特效三档LOD优化',
'008':'命中特效事件批量承载与生命周期优化',
'007':'僚机对地导弹共用表现与脉冲预警优化'}
for number,name in names.items():
    path=ROOT/'Progress/DevelopmentDocumentation'/f'20261009-{name}.md'
    text=path.read_text(encoding='utf-8-sig')
    end=text.index('\n---',4)
    header=text[:end]
    for key,value in {
      'status':'in_progress',
      'verification':'not_run',
      'summary':'用户已批准三项全阶段实现及单项、整版对照，按006→008→007实施；代码与数据管线正在开发，新增产物尚未构建和运行验收。',
      'next_action':'完成运行逻辑和资源认证，执行授权的源码构建与配对对照，最后保存验证场景并提交具体视觉候选审核。',
      'status_note':'本轮用户明确要求实施完整计划，替代此前仅文档的范围限定。保留前四项已交付优化；编译、运行、性能和新增视觉审核分别按实际证据登记。'
    }.items():header=re.sub(rf'^{key}:.*$',f'{key}: {value}',header,flags=re.M)
    body=text[end:]
    marker='## 2026-10-09 全阶段实施启动'
    if marker not in body:
        at=body.index('\n## ')
        body=body[:at]+f'\n{marker}\n\n用户通过“三项客户端性能优化开发与整版对照”明确授权代码、资源、Excel 管线、源码编译及一专服双客户端对照。原“本轮仅文档”描述属于前一方案交付阶段，本轮以此实施范围为准。测量沿用现有交付版，保留此前四项优化，不恢复旧代码。\n\n当前只记录实施启动；未执行的构建、性能、场景和视觉验收不得记为通过。\n'+body[at:]
    if number=='007':
        body=body.replace('屏外继续推进既有预测，并维护必要的有界历史；暂停组件或上传不取消逻辑飞行。恢复时显示当前光点与有效尾迹。',
          '屏外继续推进既有预测；完整表现范围完全离屏后停止尾迹生成并清理隐藏历史，暂停组件或上传不取消逻辑飞行。回屏使用当前脉冲相位，从当前位置积累新尾迹，不补画屏外轨迹。仍可见的历史尾迹和消散段计入完整范围。')
        body=body.replace('预测继续，恢复当前相位与历史，旧结束事件不重放','预测继续，恢复当前相位并从当前位置重新增长尾迹，旧结束事件不重放')
    path.write_text(header+body,encoding='utf-8')
req=ROOT/'Progress/RequirementDocument/20261009-僚机对地导弹共用表现与脉冲预警优化.md'
text=req.read_text(encoding='utf-8-sig')
rule='完全离屏后停止尾迹生成'
if rule not in text:
    at=text.index('\n## ')
    text=text[:at]+'\n## 2026-10-09 实施确认补充\n\n僚机尾迹完整范围完全离屏后停止尾迹生成并清理隐藏历史；逻辑预测继续。回屏使用原发射时间的当前脉冲相位，从当前位置重新增长，不补画屏外轨迹。仍可见的尾迹中段和消散段继续参与范围判断。\n'+text[at:]
    req.write_text(text,encoding='utf-8')
