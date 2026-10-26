> 2026-10-26 本会话已按用户指令完成归档。以下保留当时版本及审核事实，最终方向/时长与清理说明见[完成归档](../../../../Progress/Archive/20261026-地面机甲镜头方向闪现与会话完成归档.md)；不补记未执行的视觉验收。

# Ground 固定世界尾迹 A4

用户否定 A3 的“蝌蚪”观感，并明确“这个拖尾不应该有位移，不应该随着子弹被射出去”。本版按该修订直接制作，不将之前的制作许可登记为视觉通过。

## 当前实现

每发留下单条曲线，曲线分段的位置、长度、平面和波形在生成时固定。子弹只推进显露边界，已显露的线不随弹丸、机甲或镜头平移、旋转。命中点固定在世界位置，已存在的尾迹继续按年龄淡出；取消或换局立即回收。原 A3 资源保留，正式 PlayerBullet 现在指向 A4。

每个点保留 0.12 秒，再用 0.38 秒淡出。最长 12000 cm，覆盖当前 24000 cm/s 弹速下完整的 0.5 秒生命周期，避免长度限制提前切断消散过程。可见线宽 24 cm，弯曲幅度 45 cm；无圆形弹头或额外分叉。子弹判定仍沿直线，未改变伤害、碰撞半径、寿命、左右交替和整臂制退。

数值入口：[GuLiStrikeVfx.xlsx](../../../../Data/Excel/GuLiStrikeVfx.xlsx) 的 Effects / PlayerBullet。新增 TrailLengthCm、TrailHoldSeconds、TrailFadeSeconds、TrailBendCm，Scale.Y 控制线宽。Excel → JSON/CSV → 生成行结构 → DataTable 已完成。单发最多两段固定载体，完整 Bounds 随固定曲线定义；离屏释放、回屏仅恢复尚未消散的段，不重播枪口。组件回池前结束，复用时先写入全部参数再激活。

## 资源与证据

- 系统：`/Game/GuLiStrike/FX/GroundMech/FiringFeedback_A4/NS_GroundMech_PersistentCurve_A4`
- 材质：`/Game/GuLiStrike/FX/GroundMech/FiringFeedback_A4/M_GroundMech_PersistentCurve_A4`
- [制作/编译](asset_authoring.json)：1 发射器、1 Sprite 渲染器；Niagara 0 错误/警告，材质 15 表达式、0 贴图采样。
- [正式表回读](table_readback.json)：PlayerBullet / ID 8 指向 A4，长度 12000、保留 0.12、淡出 0.38、弯曲 45。
- [已保存资源重新加载与场景读取](saved_resource_readback.json)：五项 User 参数绑定有效；无位置积分模块；地图已保存，Ground 入口及 8 个现有验证实体保持，无脏资产。
- [源码构建](build.json)：沿用用户明确编译/重启授权，源码 UE `D:/UnrealEngine-5.7`，GuLiStrikeEditor Win64 Development，最终退出 0；引擎/项目 BuildId 一致。

用户在制作过程中反馈“还是跟子弹飞”，当时编辑器已加载新代码，但表仍指向 A3，四项新尾迹参数均为 0，见 [切换前记录](preimport_live_state.json)。此后已完成 A4 正式表导入并重新加载；不能把此前那次效果作为本版验收结果。

## 玩家验收

当前地图 `/Game/Maps/LVL_CommanderMassPrototype`，MainRole=Ground，玩家 `BP_GroundMech_Light`。验证区中心约 (-4000,50000,905)，`GroundMobility_Entry` 是定位点。现有出生和武器入口沿用，未额外创建靶场。

重新 Play 后单击或短点射，观察曲线是否留在经过的位置；转动镜头、移动机甲时线应保持原位。射击近处目标，命中后残留线应继续淡出。连续射击时检查可读性、长度和消散速度。

助手完成源码、表数据、保存资产和实体数据检查，未启动 PIE、截图或目检；视觉与实际运行效果待用户验收，未作性能结论。整臂制退、枪焰 2 倍、总射速 4/8/12、弹速 24000 和其他 Ground 玩法保持本轮前值。
