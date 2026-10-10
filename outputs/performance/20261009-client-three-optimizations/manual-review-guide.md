# 三项客户端优化：玩家 PIE 验收

全部新逻辑和资源已正式保存。**直接打开 `/Game/Maps/LVL_CommanderMassPrototype` 并 Play，正常玩法默认启用全部三项优化，无需安装候选或修改开关。** 本轮FPS对照按你的要求暂缓。

验收时观察：采矿绿色/建造紫色不透明光束保持7.5/12cm，远处端点按档减少；机枪枪口和命中近/中/远的核心反馈仍清楚，连续命中、离屏/回屏和停止无残留或旧事件重播；僚机对地导弹使用周期0.5秒的脉冲球形光点、实体尾迹及红圈，完全离屏后回屏从当前位置重新长尾迹。重防号仍走原专属路线。

集中观察区已保存9个默认停用预览、4个观察相机和1个指南，标签 `GuLiClientThreeReview`；光束/三档位于X约-17000至-12000、Y约-20500至-16800、Z1500cm。预览不会在正常游戏中自动增加负载。

需要集中对照时，在编辑器 Tools → Execute Python Script 执行 `Scripts/Performance/client_three_player_review.py`（旧arm脚本也只转入此助手）。它只提供镜头及真实压力入口，不改资源、输入版本或开关。然后自行按Play；若需要同进程一专服、Ground/Air双客户端，也可在Python控制台明确执行 `guli_client_three_review_start()`。

在编辑器的Python控制台运行以下操作；若输入模式是普通Cmd，前面加 `py `：

| 操作 | 命令 | 预期 |
|---|---|---|
| 三档及光束开始/峰值/停止循环 | `guli_client_three_review_view("fx")` | 两客户端看同一区域；每列固定档位，目录命中走正式批量入口 |
| 中/远观察 | `guli_client_three_review_view("middle")` 或 `("far")` | 保留轮廓和时序，端点减少；目录命中按完整范围分档 |
| 离屏及返回 | `guli_client_three_review_view("offscreen")`，稍后 `("near")` | 完全离屏后回收/暂停；恢复当前状态，旧瞬时事件不重播 |
| 停止区域预览 | `guli_client_three_review_stop_fx()` | 光束收束、瞬时特效按原生命周期结束 |
| 真实僚机对地入口 | `guli_client_three_review_ground(125)`，也支持250/500 | 需一专服及Air席位；等待真实机库部署，使用正式对地Definition、冻结落点和红圈 |
| 停止补给 | `guli_client_three_review_stop_ground()` | 不再补弹；已飞行导弹保留权威8秒寿命，结束时红圈/光点退出，尾迹再0.55秒消散 |
| 返回正常玩法镜头 | `guli_client_three_review_view("game")` | 返回各自玩家Pawn |

真实僚机入口使用保存的 `FlightEventsQAOrigin` 标记；`ClientThreeReview_Camera_Wingman` 为对应观察点。正常玩法也可直接选择Air席位、机库和僚机投弹，不必使用压力入口。局部回退开关仍保留，默认均为新行为（命中模式2）。

本轮技术检查已完成：源码构建及加载、正式Excel/导出/表/Profile读回、批量输入契约与生命周期、真实对地入口和结束清理、地图实体检查。请在PIE中确认快速转镜头/总览返回、屏外单位死亡与改令、双枪历史姿态、晚加入，以及新特效开始/峰值/停止/回屏的实际效果。玩家验收结果尚未记录；FPS、GT、GPU累计提升暂无新结论。
