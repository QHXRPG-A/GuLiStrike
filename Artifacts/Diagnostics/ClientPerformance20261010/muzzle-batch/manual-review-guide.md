# 枪口批量化：玩家PIE验收

正式默认已为 `gs.Muzzles.BatchMode 2`。正常玩法直接使用已保存的 ID52 三档批量引用和独立 Muzzle Channel；不需要安装候选脚本。模式0仍是即时单次回退，模式1是F+1单次对照。切换模式会清理旧反馈，不重播。

地图：`/Game/Maps/LVL_CommanderMassPrototype`。保存了以下默认停用对象，正常Play不会自动增加预览负载：

| 对象 | 位置cm | 用途 |
|---|---|---|
| MuzzleReview_Old_Normal | -21500,-18000,1700 | 原即时单次，普通倍率 |
| MuzzleReview_Batch_Normal | -18000,-18000,1700 | 新F+1批量，普通倍率 |
| MuzzleReview_Old_Heavy | -21500,-14500,1700 | 原即时单次，重防号倍率2 |
| MuzzleReview_Batch_Heavy | -18000,-14500,1700 | 新F+1批量，重防号倍率2 |
| MuzzleReview_Camera_Near/Middle/Far | 见实体读回 | 近、中、远观察 |
| MuzzleReview_Guide | -22500,-20100,2300 | 区域操作提示 |

集中查看：在编辑器 Tools → Execute Python Script 执行 [muzzle_player_review.py](../../../Scripts/Performance/muzzle_player_review.py)，然后自行Play。需要专服加双客户端时，也可在Python控制台明确执行 `guli_muzzle_review_start()`。等待客户端进入，再执行以下函数：

| 操作 | 预期 |
|---|---|
| `guli_muzzle_review_view('near')` | 切到近镜头并启动四个对照。左列原即时单次，右列F+1批量；远侧一排为重防号倍率2。20发/秒，2.4秒发射、4秒周期，火焰随姿态以(600,150,0)cm/s移动。 |
| `guli_muzzle_review_view('middle')` / `('far')` | 核对各自按现有视图需求选取的LOD，核心颜色、轮廓与尺寸保留。 |
| `guli_muzzle_review_view('offscreen')`，再 `('near')` | 完全离屏后回收；回屏产生当前的新反馈，旧事件不补播。 |
| `guli_muzzle_review_stop()` | 停止新事件，已有反馈按正常寿命结束，约1秒内清空。 |
| `guli_muzzle_review_view('game')` | 停止对照，恢复正常玩家镜头。 |

这些是客户端视觉预览，不产生服务器射击、伤害或网络事件；正式性能结果来自另行重建的正常移动交火入口。真实游戏中还需观察转向、左右枪口、历史姿态、后坐力、死亡、总览返回及重防号战斗反馈，玩家结论单独记录。

可先查看 [旧/新移动跟随GIF](visual/old-new-moving-follow.gif)、[开始](visual/start.png)、[峰值](visual/peak.png)、[停止](visual/stop.png)。40帧使用实际截取时间组成，可见发射与结束；140个相同身份样本确认位置持续变化，末端槽位为0。预览截图上的即时FPS受截图影响，不用于性能结论。

[性能报告](performance-report.md)记录18份窗口：200单位平均26.4→34.0FPS；压力场景约12.7→12.9FPS，变化未超波动且真实枪口反馈因上游排队过期，未宣称压力枪口收益。[实体和正式资源读回](delivery-readback.json)、[源码构建加载](build-adoption-loaded-receipt.json)。
