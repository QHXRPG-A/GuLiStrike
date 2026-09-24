---
schema: guli-progress/v1
id: ARC-20260922-006
work_id: WORK-20260922-002
kind: archive
role: root
title: FlightNav边界复核与收紧
areas: [performance, navigation]
categories: [gameplay, performance]
status: recorded
verification: partial
created: '2026-09-22'
updated: '2026-09-22'
summary: 用户指出FlightNav大于地图后，复核相对母舰的编队与恢复距离，去除重复余量，收紧至5.246km并减少下方空间；重新烘焙、保存及实体核对通过。
next_action: 玩家验证原地图边缘编队、恢复与攻击路径；独立测量内存收益。
relations:
  work_items: [WORK-20260922-002]
status_note: 原地图及FlightNav资产已保存，资源和地面导航签名保持有效；未执行PIE或性能测量。第一阶段缩图、编译和实体证据仍见ARC-20260922-005。
---

# 2026-09-22：FlightNav边界复核与收紧

## 调整依据

用户在[4.2km地图交付阶段](20260922-4.2km地图与7x7据点重布局交付.md)指出`FlightNav_LVL_CommanderMassPrototype`仍明显大于当前地形。原第一阶段XY半尺寸2783m，把180m编队半径和500m恢复距离相加；复核`GuLiWingmanFlightMovementComponent.cpp`后确认这两者均相对母舰，采用最大值即可保留相同距离包络，再增加3m代理与现有20m恢复航向探测余量。

保持覆盖完整4.2km地形，XY半尺寸由278300改262300cm，宽度5566→5246m。每侧523m余量用于边缘编队和恢复，不能简单缩到2.8km内部战场边缘。

地形最低Z约-3796.157cm。考虑母舰任意旋转，180m半径/30m相对高度产生约182.48m垂直包络，加3m代理和20m探测余量后，将下Z界从-33884.765625裁到-25000cm。原上界50750cm保留；新Actor中心为(0,0,12875)cm，半尺寸(262300,262300,37875)cm。

## 操作与核对

- 修改前复制本阶段地图和FlightNav资产至[阶段备份](../../Artifacts/Map4200/20260922/flight-bounds-refinement/backup)，初始源码/原始地形备份继续保留。
- 通过编辑器执行`Scripts/Map4200/tighten_flight_bounds.py`；未改原生代码或精度参数，无需再次编译。保留两种地面代理、80m Flight最小单元、深度6和300cm烘焙代理半径。
- [重新烘焙记录](../../Artifacts/Map4200/20260922/flight-bounds-refinement/refinement.json)：Flight重建1，地面重建0；最终Flight源签名`506066a3e2f5da41`。
- [保存记录](../../Artifacts/Map4200/20260922/flight-bounds-refinement/save-response.json)仅保存原地图和该地图FlightNav DataAsset；[实体回读](../../Artifacts/Map4200/20260922/flight-bounds-refinement/readback-response.json)通过，无脏包。49据点、200蓝簇/40红簇/6240节点及资源/地面哈希保持不变。
- 迁移脚本同步改用最大包络公式，脚本语法和文档索引再次核对。原地形视觉没有变化，不重复生成相同截图。

## 收益与验证边界

包围盒体积约减少20.5%，但不是内存收益。保持细分设置时，边界改变会影响八叉树：节点14089→16225，可飞单元6101→7374，Portal18534→22040。因此本次只确认范围更贴合使用范围，不宣称FlightNav内存下降。最终仍需玩家实际边界飞行、恢复和攻击验证，以及专服/客户端独立测量；未运行PIE、自动化测试或性能压测。
