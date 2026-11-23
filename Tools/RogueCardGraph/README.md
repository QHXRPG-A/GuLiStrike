# 肉鸽牌关系图

双击 [start.cmd](start.cmd)，浏览器会打开 [肉鸽牌关系图](http://127.0.0.1:4322/)。工具使用项目现有的 Python 和浏览器。

本工具由项目的 [统一保活任务](../ProgressDashboard/README.md#三个本地工具的保活) 在登录后启动。任务运行时可以直接打开本地页面。

1. 在顶部切换指挥官、地面、空中。节点显示卡名、总等级和已有的独立图标。点节点可在右侧查看牌面、逐级效果和关系，拖动节点调整摆放。指挥官页可看 5 张重防号牌面。地面页的 8 张游骑兵正式牌及电磁技术、分裂枪管、人工智能引擎、增强AOE、恶魔引擎、增强持续伤害都有独立图标。指针移到分层牌面上可看视差，移开后回正。「增强持续伤害」的第 3 层合并骨手、前臂和沙漏本体。各层 PNG 均可在预览页下载。[六层预览](../../ArtSource/UI/GroundMechRogueCards/DamageOverTime_20261123_v1/Parallax_v1/index.html)
2. 点「新增卡牌」，或在右侧点「编辑卡牌」，填写通用说明、总等级和各级效果。总等级填 0 表示不限。每组可编写五级，等级较多时可翻组或跳到指定等级。缩小总等级前，先调低超过新上限的后继解锁门槛。
3. 选「前置」，从 A 右侧圆点拖到 B，再点连线或右侧关系设置 A 的前置等级。多个前置都须满足。「互斥」表示两张牌二选一。「单向禁用」从触发方指向被停用方，先拿目标牌仍可拿触发牌。点禁用线可选「停用已有效果并停止获取」或「保留已有等级，仅停止后续获取」。也可先点圆点，再点目标卡来连线。
4. 拖动空白处平移，滚轮缩放，「适应画布」把当前页全部节点放进视野。
5. 点「保存草稿」或 Ctrl+S。草稿写入项目的 `data/Design/RogueCardGraph.json`，重开工具会恢复各席位内容、连线和节点位置。「撤销」或 Ctrl+Z 可恢复上一步编辑，删除卡牌时相连关系随之删除。

首次使用时，卡图从正式导出的卡表、公共文本和效果参数生成。旧草稿按保存的基线恢复现有卡的获取上限和效果预览。第三轮游骑兵的等级和逐级效果可在原有地面节点上查看，当前数值以保存的草稿为准。

## 后续接入

调好后可按保存的草稿安排正式接入。草稿包含正式来源及指纹、编辑后的图、按卡牌 ID 整理的 `changes.added`、`changes.removed` 和 `changes.updated`。节点用 `maxLevel` 和按等级编号保存的 `levelEffects` 记录等级设计。前置线 A → B 写入 B 的 `RequiredCardIds`，门槛由 `RequiredCardLevels` 表达。互斥在双方的 `ExcludedCardIds` 中成对记录。单向禁用只在触发方记录目标，`DisabledCardIds` 表示停用并停止获取，`StopAcquisitionCardIds` 表示仅停止获取。两者均为设计草稿字段。纯位置改动保存在画布数据中。

正式接入以 Excel 源表和项目导入流程为准。已有卡保持稳定 ID，新增草稿使用临时 ID，接入时再分配正式 ID、效果和公共文本键。若修改强化爆炸弹与爆破套餐的前置，接入时同时核对现有机制校验。空中新增卡还需要对应的游戏卡池、确认和效果结算入口。正式卡表更新后，页面会提示先对照草稿基线。

PowerShell 可用 `Tools\RogueCardGraph\start.ps1 -Port 4322` 启动。以 `-NoBrowser` 启动时，手动打开显示的本地地址。

## 更新图标与牌面

从项目根目录运行 `python Tools/RogueCardGraph/build_artwork.py` 并刷新页面。脚本使用 Pillow。正式卡按卡表的 `FrontMaterial` 和 `IconId` 匹配牌面材质与独立图标，审核中的草稿按稳定节点 ID 绑定。卡名可以继续编辑。网页读取工具目录内的成品文件，使用时只需启动关系图服务。

重防号早期 PNG 的导入路径已失效。首次重建或更换重防号贴图后，先用项目引擎导出当前材质引用的纹理，再运行上面的发布命令。导出脚本将纹理写到系统临时目录下的 `GuLiStrike-GraphTextures`。需要指定其他缓存目录时，可为两个步骤设置同一个 `GULI_GRAPH_TEXTURE_CACHE`。

```powershell
& 'D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' (Resolve-Path 'GuLiStrike.uproject').Path '-run=pythonscript' ('-script=' + (Resolve-Path 'Scripts/Cards/export_graph_artwork_textures.py').Path) '-nullrhi' '-unattended' '-nosound' '-nop4'
```
