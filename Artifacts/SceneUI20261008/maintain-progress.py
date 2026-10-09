"""Apply this handoff's focused Progress entries and refresh the Commander split contract."""
import hashlib
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'.agents/skills/gulistrike-progress/scripts'))
import progress_docs as docs

def update(relative,change):
    path=ROOT/relative
    original=path.read_bytes()
    metadata,body=docs.split_front_matter(original.decode('utf-8-sig').replace('\r\n','\n'))
    metadata,body=change(metadata,body)
    assert path.read_bytes()==original,'File changed during update: '+str(path)
    path.write_text(docs.render_document(metadata,body),encoding='utf8')

def ledger(meta,body):
    meta['updated']='2026-10-08'
    body+='''

## 2026-10-08：v1.4 本地阵营配色与场景 UI 规则

用户明确要求“该ui不受任何环境影响”“玩家控制的永远是蓝色方，敌人永远是非蓝色方”，并批准实施完整计划；据此更新规范 v1.3→v1.4。新增本地敌我显示、脚环己方蓝/敌方红与世界20 cm环带、同色选择短标记、UI环境隔离、模型固定/可变分区与先审后接入规则。保留三档LOD、扫荡者无线稿和既有批准版本，不对已入库模型追溯重做。

`LocalTeamColorReview.20261008.v1` 提供四种Mass、八类玩法建筑和六座SSF共18组独立派生对照；[画廊与黄色候选分区](../../ArtSource/LocalTeamColorReview_20261008/REVIEW.md)共54张实际UE图，相机/材质/源引用逐模型记录。蓝红参考为 #2877DB / #D7534D，实际模型引用未切换。方向依据为本轮用户计划；这18个具体变色区与哨戒炮独立预览底色修复尚待用户核对，没有新成品“通过”决定。

该规则维护工作项的 done/passed 仅表示规范与台账维护完成，不代表候选配色通过。技术事实为素材保存与源码静态审查、对应地图17个实体保存重载回读；未执行原生编译、游戏运行、双客户端和性能验证。[本轮交付与边界](20261008-场景UI环境隔离与本地阵营配色.md)、[增量归档](../Archive/20261008-场景UI源码与红蓝模型对照交付.md)。
'''
    return meta,body

def commander(meta,body):
    meta['updated']='2026-10-08'
    meta['status_note']+=' 本轮新增本地配色与Slate场景UI源码未编译，环境/双方客户端效果待玩家验收，模型分区待审。'
    intro='''
2026-10-08 新源码待加载：场景UI改由每本地玩家一个UMG/Slate容器在后处理之后绘制；真实阵营只读解析为己方蓝、敌方红脚环，选择仅增加四个同色短标记，环带世界径向20 cm。血量、路径和放置功能色保留，服务器归属/权限/协议不改。18组模型独立红蓝与黄色分区候选待审，实际模型引用未切换。[源码、画廊与玩家验收入口](../DevelopmentDocumentation/20261008-场景UI环境隔离与本地阵营配色.md)。静态检查及原Mass地图17个本轮实体保存重载回读通过；未编译/未开游戏，不能以旧运行结果认定新UI通过。
'''
    body=body.replace('# 指挥官\n','# 指挥官\n'+intro,1)
    return meta,body

def commander_ui(meta,body):
    meta['updated']='2026-10-08'
    meta['status_note']+=' 2026-10-08场景UI与本地敌我颜色源码静态交付、地图保存回读通过；未编译，环境和双客户端未验收。'
    entry='''
### 新源码待加载：场景 UI 环境隔离与本地配色（2026-10-08）

每本地玩家一个无焦点UMG/Slate容器批量绘制场景UI，主体不透明，在场景后处理之后显示；脚环、血条、选区、指令/路径、放置与范围、世界图标和敌方轮廓停用对应旧场景绘制并复用已有数据。Ship世界面板复用原RT源，最终投影进同一容器；新增UI不按每Mass创建Widget或Actor。

显示颜色按本地真实阵营只读解析：己方脚环蓝、敌方红，未确定阵营不显示；残骸/选中不覆盖阵营色，选中增加四个同色外缘短标记。世界径向宽度20 cm，内外半径边缘分别投影，不使用固定像素宽度。普通半径来自模型宽度×50 cm，显式避障半径优先×100 cm；本轮四种参考312.5/625/155/1629.7544 cm。总览沿用脚环/血条隐藏规则，血量绿、路线绿、放置合法性绿/红保留意义。服务器真实归属、控制和网络协议保留。

四种Mass、八类玩法建筑和六座既有SSF共18组模型对照先审黄色可变区；固定结构、线稿、功能色、明暗参数、几何、动画和LOD不重做，正式模型尚未切换。代码静态审查、素材预览和原型地图保存重载回读完成；原生编译未执行，环境、双方客户端、归属刷新与成本未实测。[交付与玩家操作](../../DevelopmentDocumentation/20261008-场景UI环境隔离与本地阵营配色.md)。本条为新实现规则，下方旧场景Ring/血条绘制描述保留为历史。

'''
    body=body.replace(docs.SPLIT_CONTENT_MARKER,docs.SPLIT_CONTENT_MARKER+'\n'+entry,1)
    segment=body.split(docs.SPLIT_CONTENT_MARKER,1)[1]
    meta['split_segment_sha256']=docs.normalized_payload_hash(segment,ROOT/'Progress/Gameplay/指挥官/02-UI表现与性能.md')
    return meta,body

def building(meta,body):
    meta['updated']='2026-10-08'
    meta['status_note']+=' 2026-10-08新增放置UI蒙版与网格的Slate显示源码未编译，红蓝建筑候选分区未切换正式模型。'
    entry='''
2026-10-08 新显示源码待加载：建筑放置预览改为仅供捕获的白色模型蒙版，原CPU合法性与范围数据在最终Slate中显示不透明绿/红预览及网格，旧Decal隐藏；血条、图标和阵营轮廓同属场景UI环境隔离。敌我颜色仅按本地真实阵营解析，真实建造权限、费用、所有权与网络流程不改。八类玩法建筑及六座既有SSF已有[红蓝与黄色分区候选](../../ArtSource/LocalTeamColorReview_20261008/REVIEW.md)，SSF不增加玩法类型、实际模型引用尚未切换。地图保存回读通过，编译与环境/双方客户端效果未执行；下方半透明预览描述为此前实现。[本轮开发与玩家验收](../DevelopmentDocumentation/20261008-场景UI环境隔离与本地阵营配色.md)。
'''
    body=body.replace('# 建筑\n','# 建筑\n'+entry,1)
    return meta,body

update('Progress/DevelopmentDocumentation/GuLiStrike美术规范.md',ledger)
update('Progress/Gameplay/指挥官.md',commander)
update('Progress/Gameplay/指挥官/02-UI表现与性能.md',commander_ui)
update('Progress/Gameplay/建筑.md',building)

parent=ROOT/'Progress/Gameplay/指挥官.md'
meta,body=docs.split_front_matter(docs.read_text(parent))
parts=[]
for child in sorted(parent.with_suffix('').glob('*.md')):
    cm,cb=docs.split_front_matter(docs.read_text(child))
    assert cm['role']=='detail'
    parts.append((cm['split_order'],docs.transform_markdown_links(cb.split(docs.SPLIT_CONTENT_MARKER,1)[1],child)))
assert len(parts)==5
meta['split_previous_payload_sha256']=meta['split_payload_sha256']
meta['split_payload_sha256']=hashlib.sha256(''.join(p for _,p in sorted(parts)).encode('utf8')).hexdigest()
meta['split_revision']='20261008-scene-ui-local-palette'
parent.write_text(docs.render_document(meta,body),encoding='utf8')
print('Updated 4 Progress entries and Commander split hashes')
