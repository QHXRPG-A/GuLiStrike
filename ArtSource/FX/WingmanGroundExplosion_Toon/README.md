# 僚机对地轰炸动漫爆炸样板

已接入 `DA_WingmanGroundExplosion`，可以在原有僚机对地攻击中查看。制作交付日期：2026-09-16。

## 在 UE 中查看

内容浏览器路径：`/Game/GuLiStrike/FX/WingmanWeapons/StylizedExplosion`。

打开 `NS_WingmanGroundExplosion_Toon` 可预览单次爆炸。Niagara 编辑器默认作者缩放为 1；实际轰炸通过数据资产以 Scale=5 播放。不要再给扩散环叠加组件缩放。

四层分别是 `Ignition`、`FireSmoke`、`ExpansionRing`、`Sparks`，配置为 1 + 8 + 1 + 12 粒子，CPU 模拟，Once 生命周期。火球约 0.15s 膨胀至峰值，转浅粉灰烟后收缩溶解，1.8s 内消散，表现组件期限为 2s。团块网格为 320 三角面，环为 128 三角面。

## 截图

- 原版两层同机位：[原版较明显火球阶段](frames/old_detail/smoke_start.png)
- 新版：[火球](frames/toon_detail/peak.png)、[烟团](frames/toon_detail/smoke.png)、[消散后](frames/toon_detail/end.png)
- [明亮背景](frames/toon_bright/peak.png)、[18 度斜坡](frames/toon_slope/peak.png)、[远景](frames/toon_far/peak.png)

每组 `capture.json` 记录相机、分辨率、种子和时间。图片来自 UE SceneCapture，没有用 AI 重绘。各文件的 `peak` 等标签仅表示约定采样时间；旧版真实峰值与新版并不发生在同一时刻。

## 回退

停止 PIE 后，在 UE 的 Python 控制台执行以下两行即可恢复原版 Big_17 主层与冲击波；改回 `toon` 可以重新安装新版。

```python
WTE_INSTALL_VARIANT = 'old'
exec(open(r'D:/UE5.7/test1/Scripts/set_wingman_explosion_variant.py', encoding='utf8').read())
```

该入口只保存 `DA_WingmanGroundExplosion`，不会重建武器表或改动伤害。原配置快照保存在 `original-visual.json`。部署脚本已识别新版配置并保留它；整套部署没有在本次收尾时运行。

## 验证结果与边界

`final-readback.json`：Niagara 0 错误、0 警告，四材质编译通过；新引用、Scale=5、RandomYaw=true、MaximumLifetime=2、额外层为空，部署保护只读校验通过。所有制作脚本语法检查通过。未修改 C++、RPC、自动化测试、场景光照或全局渲染设置。

结构减负：原先两套系统共 10 个启用发射器，改成 1 套 4 个发射器；主烟团和环改为 Masked，去除折射层及 GPU 模拟，不使用动态灯、粒子碰撞和碎片。这个结构变化不能直接换算成帧率提升。

原版 1 / 10 / 30 发各三轮的 CSV 保存在 `performance/`。GPU 空场基线漂移显著，扣除后出现负值；该组不能支撑提速结论。新版配对采样、透明覆盖对照、严格外径 ±10% 校准和真实攻击的客户端/专用服务器回池动态验收尚未完成。按用户要求先提供可进入 UE 查看的一版，后续验收状态仍为 partial。
