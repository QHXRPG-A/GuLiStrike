# 全量重命名：彻底抹除 TwinStick → GuLiStrike

## 范围盘点

- **C++ 12 个类**：ATwinStick{Character, GameMode, PlayerController, AIController, NPC, NPCDestruction, Spawner, Projectile, Pickup, AoEAttack}、UTwinStickUI、TwinStickStateTreeUtility（26 个源文件 + Source/Variant_TwinStick 目录名）
- **16 个内容资产**：11 个 BP_TwinStick*、ST_TwinStickNPC、IMC_TwinStick(_MouseShoot)、BPI/UI_TouchInterface_TwinStick、UI_TwinStick
- **配置**：DefaultEngine.ini 的 GlobalDefaultGameMode 指向 BP_TwinStickGameMode
- **LVL_Main**：内含 44 处 TwinStick 字符串（Spawner/Pickup 引用，重存即消）

## 关键冲突与解法

目标名 `AGuLiStrikeCharacter/GameMode/PlayerController` 当前被**孤儿模板类**占用（原 test1*，其唯一使用者 BP_TopDown* 三张蓝图已在内容合并中删除，全项目零引用）。**先删除这 6 个死代码文件腾出名字**——这是实现"TwinStick 类改名为 GuLiStrike 类"的必要前提，也符合彻底抹除的意图。DefaultGame.ini 里对应的 `[/Script/GuLiStrike.GuLiStrikeCharacter]` section 一并删除。

## 执行步骤（编辑器需关闭，全程 git 可回退）

### R1：C++ 重构（离线）
1. 删除 6 个孤儿模板文件（GuLiStrike{Character,GameMode,PlayerController}.h/.cpp）
2. 26 个 TwinStick 源文件 git mv 到模块根（AI/、Gameplay/、UI/ 子结构保留），删除 Variant_TwinStick 目录，文件与类名 TwinStick*→GuLiStrike*
3. 全模块标识符替换（类名、include、generated.h、ForwardDeclare、日志文本中的 TwinStick 字样）
4. Build.cs 的 PublicIncludePaths 更新（去掉 Variant_TwinStick 层级）
5. DefaultEngine.ini [CoreRedirects] 加 12 条 ClassRedirects（TwinStick* → GuLiStrike*，保住全部 BP 的父类引用）

### R2：编译验证（UBT Development Editor，零错误才继续）

### R3：资产重命名（编辑器内，吸取前两次教训）
1. 先写 DefaultEditorPerProjectUserSettings.ini 关闭 checkout/保存弹窗（上次卡死的嫌疑主因之一）
2. 启动编辑器，TCP 逐个 rename_assets：**一次一个、每个之后 save_dirty、检查落盘**（上次失败根因是我并发跑了两份脚本互锁 + GameMode 是激活资产）
3. BP_TwinStickGameMode 放最后；完成后 GlobalDefaultGameMode 同步改为 BP_GuLiStrikeGameMode
4. rename_assets 自带 referencer 修复（不同于文件移动），BP 间引用自动更新并留重定向器

### R4：收尾
1. LVL_Main 重存（消除 44 处 TwinStick 字符串）
2. **审计**：Content/Config/Source 全域 grep "TwinStick" 归零（git 历史除外）
3. PIE 复验：GameMode/玩家/刷怪/NPC/拾取物全链路
4. git 提交

## 明确不动的东西

- ABlinkVFX（名字不含 TwinStick）、IA_Action_* 输入动作（已无 TwinStick 字样）、Strategy 孤儿类（本轮范围外）、Scifi_Skies 与 Maps 目录