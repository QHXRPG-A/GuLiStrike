# HUDuiPRO 独立组件动画索引

**交付状态：已完成。** 81 个组件、405 个配套 UE 资源已保存，AE 源及本会话临时文件已清理；运行 PNG 与最终交付证据保留。见[会话清理记录](session-cleanup.json)。

81 个组件各有独立媒体链，UE 根目录为 `/Game/Assets/UI/HUDuiPRO/Animations`。保留原合成尺寸、24 fps、完整时长和透明通道；共 111,426 帧 RGBA8 PNG。

动画内容由 AE 烘焙，数字和文字随源合成播放；玩法数值绑定可另接 UMG 文本层。3 个全局颜色控制合成不属于可播放组件；Final 组的完整驾驶舱静态纹理与 1080p 整段动画沿用原交付入口。

## 单独调用

1. 从下面目录中选取组件，将 `M_UI_HUDC_<组件键>` 设置为 UMG Image 的 Brush Material。
2. Widget 创建或需要播放时，对同目录 `MP_HUDC_<组件键>` 调用 `Open Source`，传入 `IMS_HUDC_<组件键>`；播放器已设打开即播和循环。
3. 需要暂停时调用该 MP 的 `Pause`；从头播放可重新打开 IMS，或在媒体就绪后 `Seek(0)` 再 `Play`。
4. Widget 移除或该组件停用时调用对应 MP 的 `Close`，释放序列读取；其他组件使用各自 MP。

每个目录另含 `MPL_HUDC_<组件键>`（单源播放列表）和 `MT_HUDC_<组件键>`（材质已绑定）。同一 MP 的多个显示实例共享播放时间；需要同一组件的不同播放进度时，分别创建播放器、媒体纹理，并在各自的材质中绑定对应纹理。

例如 `Arrows 05` 的组件键为 `Arrows_05`，目录为 `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Arrows/Arrows_05`，使用 `MP_HUDC_Arrows_05`、`IMS_HUDC_Arrows_05` 与 `M_UI_HUDC_Arrows_05`。

完整的 405 个 UE 对象路径见 [manifest.json](manifest.json) 的 `components[].assets`；媒体序列位于 `Content/Movies/HUDuiPRO/Components`，IMS 使用项目相对路径。

## 组件清单

### 3d_Objects（3）

| AE 合成 / 组件键 | 尺寸 | 秒 / 帧 | UE 目录 |
|---|---|---|---|
| Helicopter2 / `Helicopter2` | 1878×898 | 50.500 / 1212 | `/Game/Assets/UI/HUDuiPRO/Animations/3d_Objects/Military/Military_Parts/Helicopter2` |
| Missile1 / `Missile1` | 300×500 | 50.500 / 1212 | `/Game/Assets/UI/HUDuiPRO/Animations/3d_Objects/Military/Military_Parts/Missile1` |
| satellite / `satellite` | 1171×1080 | 50.500 / 1212 | `/Game/Assets/UI/HUDuiPRO/Animations/3d_Objects/Military/Military_Parts/satellite` |

### Basic_Elements（56）

| AE 合成 / 组件键 | 尺寸 | 秒 / 帧 | UE 目录 |
|---|---|---|---|
| Arrows 05 / `Arrows_05` | 100×100 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Arrows/Arrows_05` |
| Box 1 / `Box_1` | 400×400 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Squares/Boxes/Box_1` |
| Central point 01 / `Central_point_01` | 1080×1080 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Central_Points/Central_point_01` |
| Central point 03 / `Central_point_03` | 1080×1080 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Central_Points/Central_point_03` |
| Central point 04 / `Central_point_04` | 1080×1080 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Central_Points/Central_point_04` |
| Central point 05 / `Central_point_05` | 1080×1080 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Central_Points/Central_point_05` |
| Circle Elements 03 / `Circle_Elements_03` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Circles/Circle_Elements/Circle_Elements_03` |
| Circle Elements 04 / `Circle_Elements_04` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Circles/Circle_Elements/Circle_Elements_04` |
| Circle Elements 05 / `Circle_Elements_05` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Circles/Circle_Elements/Circle_Elements_05` |
| Circle Elements 06 / `Circle_Elements_06` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Circles/Circle_Elements/Circle_Elements_06` |
| Circle Elements Bars 01 / `Circle_Elements_Bars_01` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Circles/Circle_Elements_Bars/Circle_Elements_Bars_01` |
| Circle Elements Bars 02 / `Circle_Elements_Bars_02` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Circles/Circle_Elements_Bars/Circle_Elements_Bars_02` |
| Circle Elements Bars 04 / `Circle_Elements_Bars_04` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Circles/Circle_Elements_Bars/Circle_Elements_Bars_04` |
| Circle Elements Bars 08 / `Circle_Elements_Bars_08` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Circles/Circle_Elements_Bars/Circle_Elements_Bars_08` |
| Circle Elements Blocks 05 / `Circle_Elements_Blocks_05` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Circles/Circle_Elements_Blocks/Circle_Elements_Blocks_05` |
| Circle Elements Tech 07 / `Circle_Elements_Tech_07` | 600×600 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Circles/Circle_Elements_Tech/Circle_Elements_Tech_07` |
| Circle Elements Tech 15 / `Circle_Elements_Tech_15` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Circles/Circle_Elements_Tech/Circle_Elements_Tech_15` |
| Coding 2 / `Coding_2` | 400×1280 | 60.042 / 1441 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Text_Paragraphs_Boards/Code/Coding_2` |
| counter 1 / `counter_1` | 400×257 | 50.417 / 1210 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Time/counter_1` |
| Earth Map / `Earth_Map` | 1541×1396 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Maps/Earth_Map` |
| Earth Map 2 / `Earth_Map_2` | 1541×1396 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Maps/Earth_Map_2` |
| Earth Map Topography 2 / `Earth_Map_Topography_2` | 2500×1406 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Maps/Earth_Map_Topography_2` |
| Energy bar 1 / `Energy_bar_1` | 608×270 | 50.000 / 1200 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Progress_Bars/Energy/Energy_bar_1` |
| Energy bar 2 / `Energy_bar_2` | 666×1084 | 50.000 / 1200 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Progress_Bars/Energy/Energy_bar_2` |
| Energy bar 4 / `Energy_bar_4` | 528×231 | 50.000 / 1200 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Progress_Bars/Energy/Energy_bar_4` |
| Grid Main 07 / `Grid_Main_07` | 1920×1080 | 81.208 / 1949 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Grids/Grid_Main_07` |
| Map Earth / `Map_Earth` | 960×540 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Maps/Map_Earth` |
| Numeric Circular 01 / `Numeric_Circular_01` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Numerics/Numeric_Circular_01` |
| Numeric Circular 02 / `Numeric_Circular_02` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Numerics/Numeric_Circular_02` |
| Pointer 01 / `Pointer_01` | 300×300 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Pointers/Pointer_01` |
| Pointer 05 / `Pointer_05` | 300×300 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Pointers/Pointer_05` |
| Pointer 06 / `Pointer_06` | 300×300 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Pointers/Pointer_06` |
| Pointer 09 / `Pointer_09` | 300×300 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Pointers/Pointer_09` |
| Pointer Circular 04 / `Pointer_Circular_04` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Pointers/Pointer_Circular/Pointer_Circular_04` |
| Pointer Circular 08 / `Pointer_Circular_08` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Pointers/Pointer_Circular/Pointer_Circular_08` |
| Pointer Circular 12 / `Pointer_Circular_12` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Pointers/Pointer_Circular/Pointer_Circular_12` |
| Pointer Circular 18 / `Pointer_Circular_18` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Pointers/Pointer_Circular/Pointer_Circular_18` |
| Polygon 05 / `Polygon_05` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Polygons/Polygon_05` |
| Polygon 07 / `Polygon_07` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Polygons/Polygon_07` |
| Polygon 09 / `Polygon_09` | 610×610 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Polygons/Polygon_09` |
| Polygon 10 / `Polygon_10` | 500×500 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Polygons/Polygon_10` |
| Progress Bar Vertical 01 / `Progress_Bar_Vertical_01` | 59×344 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Progress_Bars/Progress_bar_Vertical/Progress_Bar_Vertical_01` |
| Progress Bar Vertical 02 / `Progress_Bar_Vertical_02` | 59×344 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Progress_Bars/Progress_bar_Vertical/Progress_Bar_Vertical_02` |
| Progress Bar Vertical 03 / `Progress_Bar_Vertical_03` | 157×260 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Progress_Bars/Progress_bar_Vertical/Progress_Bar_Vertical_03` |
| Scart 06 / `Scart_06` | 500×300 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Scart/Scart_06` |
| Scart Comp / `Scart_Comp` | 560×417 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Scart/Scart_Comp` |
| Scart Main / `Scart_Main` | 560×417 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Scart/Scart_Main` |
| Target 28 / `Target_28` | 300×300 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Targets/Target_28` |
| Target 33 / `Target_33` | 300×300 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Targets/Target_33` |
| Text Paragraph 03 / `Text_Paragraph_03` | 258×276 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Text_Paragraphs_Boards/Text_Paragraphs/Text_Paragraph_03` |
| Time Digital 02 / `Time_Digital_02` | 242×100 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Time/Time_Digital/Time_Digital_02` |
| Time Digital 04 / `Time_Digital_04` | 242×100 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Time/Time_Digital/Time_Digital_04` |
| Topo 3 / `Topo_3` | 2960×1665 | 50.000 / 1200 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Maps/Topo_3` |
| Xbox / `Xbox` | 250×250 | 50.000 / 1200 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Squares/Boxes/Xbox` |
| Xbox 2 / `Xbox_2` | 250×250 | 50.000 / 1200 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Squares/Boxes/Xbox_2` |
| Xbox 3 / `Xbox_3` | 250×250 | 50.000 / 1200 | `/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Squares/Boxes/Xbox_3` |

### Elements_Main（11）

| AE 合成 / 组件键 | 尺寸 | 秒 / 帧 | UE 目录 |
|---|---|---|---|
| GPRS points 1 / `GPRS_points_1` | 2041×1378 | 50.417 / 1210 | `/Game/Assets/UI/HUDuiPRO/Animations/Elements_Main/HUD_Parts/GPRS_points_1` |
| GPRS points 2 / `GPRS_points_2` | 2041×1378 | 50.417 / 1210 | `/Game/Assets/UI/HUDuiPRO/Animations/Elements_Main/HUD_Parts/GPRS_points_2` |
| Grid 01 / `Grid_01` | 3840×2160 | 50.000 / 1200 | `/Game/Assets/UI/HUDuiPRO/Animations/Elements_Main/Grid/Grid_01` |
| Grid 02 / `Grid_02` | 3840×2160 | 50.000 / 1200 | `/Game/Assets/UI/HUDuiPRO/Animations/Elements_Main/Grid/Grid_02` |
| Grid Main / `Grid_Main` | 3840×2160 | 50.000 / 1200 | `/Game/Assets/UI/HUDuiPRO/Animations/Elements_Main/Grid/Grid_Main` |
| Hitech_Box_Heli / `Hitech_Box_Heli` | 927×441 | 50.833 / 1220 | `/Game/Assets/UI/HUDuiPRO/Animations/Elements_Main/INFO_TEXT_PANELS/Hitech_Box_Heli` |
| Hitech_Box_Missiles 1 / `Hitech_Box_Missiles_1` | 927×200 | 50.833 / 1220 | `/Game/Assets/UI/HUDuiPRO/Animations/Elements_Main/INFO_TEXT_PANELS/Hitech_Box_Missiles_1` |
| Hitech_Box_satellite 2 / `Hitech_Box_satellite_2` | 927×441 | 55.333 / 1328 | `/Game/Assets/UI/HUDuiPRO/Animations/Elements_Main/INFO_TEXT_PANELS/Hitech_Box_satellite_2` |
| Postion GPRS / `Postion_GPRS` | 2041×1378 | 50.417 / 1210 | `/Game/Assets/UI/HUDuiPRO/Animations/Elements_Main/HUD_Parts/Postion_GPRS` |
| Statistics Lines 2 / `Statistics_Lines_2` | 500×150 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Elements_Main/Statistics/Statistics_Lines_2` |
| Statistics Lines 4 / `Statistics_Lines_4` | 401×417 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/Elements_Main/Statistics/Statistics_Lines_4` |

### HUD_SPECIAL_GADGETS（11）

| AE 合成 / 组件键 | 尺寸 | 秒 / 帧 | UE 目录 |
|---|---|---|---|
| Cockpit 2 / `Cockpit_2` | 3840×2300 | 50.333 / 1208 | `/Game/Assets/UI/HUDuiPRO/Animations/HUD_SPECIAL_GADGETS/Cockpit/Cockpit_2` |
| Display 01_content 2 / `Display_01_content_2` | 3840×2300 | 50.333 / 1208 | `/Game/Assets/UI/HUDuiPRO/Animations/HUD_SPECIAL_GADGETS/Cockpit/precomp/Display_01_content_2` |
| HUDTarget 1 / `HUDTarget_1` | 1920×1080 | 50.000 / 1200 | `/Game/Assets/UI/HUDuiPRO/Animations/HUD_SPECIAL_GADGETS/HUD_Hero/HUDTarget_1` |
| Panel 1 / `Panel_1` | 3840×2160 | 50.333 / 1208 | `/Game/Assets/UI/HUDuiPRO/Animations/HUD_SPECIAL_GADGETS/Cockpit/Panels/Panel_1` |
| Special Gadget 01 / `Special_Gadget_01` | 800×800 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/HUD_SPECIAL_GADGETS/Gadgets_HUD_UI/Special_Gadget_01` |
| Special Gadget 04 / `Special_Gadget_04` | 800×800 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/HUD_SPECIAL_GADGETS/Gadgets_HUD_UI/Special_Gadget_04` |
| Special Gadget 06 / `Special_Gadget_06` | 800×800 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/HUD_SPECIAL_GADGETS/Gadgets_HUD_UI/Special_Gadget_06` |
| Special Gadget 09 / `Special_Gadget_09` | 900×800 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/HUD_SPECIAL_GADGETS/Gadgets_HUD_UI/Special_Gadget_09` |
| Special Gadget 10 / `Special_Gadget_10` | 900×800 | 60.000 / 1440 | `/Game/Assets/UI/HUDuiPRO/Animations/HUD_SPECIAL_GADGETS/Gadgets_HUD_UI/Special_Gadget_10` |
| Special Gadget 20 / `Special_Gadget_20` | 2098×832 | 50.000 / 1200 | `/Game/Assets/UI/HUDuiPRO/Animations/HUD_SPECIAL_GADGETS/Gadgets_HUD_UI/Special_Gadget_20` |
| Target 01 / `Target_01` | 521×521 | 60.333 / 1448 | `/Game/Assets/UI/HUDuiPRO/Animations/HUD_SPECIAL_GADGETS/Cockpit/Targets/Target_01` |

## 保存与核对

序列占用 12,968,552,431 字节，约 12.08 GiB。各帧名称连续，PNG 容器的尺寸、RGBA8、头部 CRC 和结束块已核对；405 个 UE 对象的类型与媒体／材质引用已回读。

按用户要求，导入后已清理原始、适配及批渲染 AEP 和原始 MOV／PNG 素材；UE 资源、运行序列及 UE 贴图重导入原图继续保留。源文件哈希与清理记录见 [cleanup-report.json](cleanup-report.json)。结构证据见 [sequence-verification.json](sequence-verification.json)、[ue-verification.json](ue-verification.json) 与 [render-summary.json](render-summary.json)。

按项目约定，视觉、实际播放与性能由用户核验。本次未运行 PIE、Standalone、播放、性能测试或打包。打包复用 [资源说明](../README.md) 中的 ImgMedia 与非资产序列暂存要求。
