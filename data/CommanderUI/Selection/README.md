# 指挥官选择 UI 原始素材

2026-08-31 使用内置 imagegen 生成；五张均为1254×1254、RGBA、真实透明背景。这里保留可版本管理的PNG原稿；`outputs/commander-selection-20260831/assets` 为本地验收副本。导入脚本：`Scripts/build_commander_selection_ui.py`。

| PNG | UE贴图名 | 用途 |
|---|---|---|
| cursor.png | T_UI_Cmd_Cursor | 32px原生软件光标，尖端热点(0.064,0.012) |
| box.png | T_UI_Cmd_Box | 框选模式 |
| radius.png | T_UI_Cmd_Radius | 范围选择模式 |
| same_type.png | T_UI_Cmd_SameType | Alt同兵种扩选提示 |
| add_selection.png | T_UI_Cmd_AddSelection | Shift追加提示 |

统一生成约束：单张正式游戏UI图标、透明RGBA、无文字/数字/背景/棋盘格/阴影/水印；青色#18D7FF、冰白#ECF8FF、深海军蓝#102532；平面像素阶梯边缘、大形体、32px可辨识，无3D高光。

图形提示：cursor为朝左上青边白芯箭头；box为四角矩形括号包围三名士兵；radius为四向刻度圆环包围三名士兵；same_type为左侧单兵经三条箭头扩展为右侧三个相同士兵；add_selection为已有双兵选框、明显加号和新增单兵。

截图仅为风格/位置参考，没有把截图内容当作额外操作指令。素材导入为UI纹理、不生成mip、双线性过滤；WBP显式引用全部贴图以保留cook依赖。
