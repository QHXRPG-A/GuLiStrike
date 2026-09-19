# SpiderMech：原模型风格版 v2

用户反馈 v1“几乎不能用，全是碎面”，随后明确“不再减面，只改色块和美术风格”。本版撤回 SpiderMech 的 19,656 三角面候选，恢复完整源网格 **839,778 三角面 / 533,878 顶点**；不继续减面、不重拓扑、不改轮廓。

- [实际效果、三视图与近景](Review_Spider_v2.html)
- [Blender 文件](SpiderMech_SourceStyle_v2.blend)，已打开 `Review_SpiderMech` 场景。
- [源网格一致性及保存读回](source_style_report.json)
- [交付清单](production_manifest.json)

顶点位置、三角索引、原 DiffuseUV、材质槽归属、原蒙皮权重和导入法线与源副本一致。保留既有299骨骨架和10个材质槽，修改器只剩原Armature。原六条附肢、尖足、翘尾、关节与软管细节全部保留。

配色沿用原蓝灰、赭黄与深灰。根据原贴图识别完整表面零件的主色，以连续色块替代磨损噪声和逐三角形涂色；保留受光的Principled预览，对应原Default Lit类别。颜色不会在一个零件内部沿长三角形随机切换。没有用新增法线贴图或高光掩盖减面缺陷。

诊断区分：旧v1材质的Normal输入本来就没有连接；逐面取色把长三角面分成黄、蓝、黑碎块，过度减面与全部平面着色进一步造成尖碎外观。灰模对照见 `Source_Gray_Close.png` 和 `Rejected_v1_Gray_Close.png`。`Spider_MaterialNormal_Candidate.blend` 是用户停止减面指令前的诊断实验，已弃用，不能作为当前成品。

当前文件在v1基础上另存，仅更新SpiderMech工作网格与材质；轻型装甲和其他模型保持。原 `Production_v1/Mechs_Style_SourceBased_v1.blend` 未覆盖，既有Ground玩家资产未修改。

本版是实际Blender候选，用户尚未确认B通过；未导入UE，不声明动画、碰撞或性能验收。原低模预算已被最新指令撤销。
