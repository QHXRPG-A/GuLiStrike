# 灼烧子弹透视修正版 v2

[参考图](01_BurningBullet_Perspective_v2.png) · [完整提示词](Prompts/01_BurningBullet_Perspective_v2.txt) · [实际请求](generation_request.json) · [结果记录](manifest.json)

依据用户标注，修订目标是让远处枪口、先驱号穿透位置、弹尾中心与弹尖沿同一条直线，移除原连接处的折角；参考中的旧机甲外观按用户要求保留。该图仍待用户核验，没有替换正式牌面。

输入包括上一版干净原图与用户红绿箭头反馈，均已冻结在Inputs。使用内置image_gen生成，原始输出1254×1254，项目副本未缩放或二次编辑；只做文件、PNG和尺寸检查，没有生成结果读图测试。

## 模型路由说明

当前插画造型不作为运行时模型参考。正式Ground模型由Excel模型目录4001解析为MechaDestroyer_A4B2，旧Style_v9、GroundMech/Meshes和MechaDestroyer_v1已清理，后续任务按当前目录与角色回读取参考。
