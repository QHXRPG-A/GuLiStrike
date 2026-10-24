# 单颗矿石导航收尾证据

用户要求标记完成、清理临时文件并上传项目变更。本目录保留本次验证的103份紧凑证据；[实施记录](../../DevelopmentDocumentation/20261024-单颗矿石采空与异步导航恢复.md)包含测试方法、数字及限制。

- `aggregate.json`、`controlled/summary.json`：12个受控窗口，0/1/64/234颗各3轮，180秒、18018个保留帧。
- `state-validation.json`、各窗口`before.json`与`after.json`：存活数、队列、索引与世界代次核对。
- `measured/summary.json`：5个有效正常采矿窗口；无summary的失败窗口未计入结论。
- `trace-diagnostic/spike-analysis.json`与各窗口`diagnosis/analysis.json`：渲染等待调用栈提取结果。Trace窗口不混入性能对照。
- `build*.log.txt`、`build-ids.json`：最初编译错误及后续成功日志、模块一致性。
- `handoff.json`、`final-map-save.json`：恢复原Ground席位、停止临时PIE后的状态及最终保存结果。
- `retained-manifest.json`：复制来源、大小与SHA256；`cleanup-manifest.json`：精确删除目标、大小和实际结果。

原始目录 `D:/UE5.7/PerformanceCaptures/GuLiStrike/OreNavigation20261024/` 继续保留有效对照/自然采矿的原始profile以及小型回执。清理了20个文件、608458940字节（约580.27MiB）：4份utrace、Trace导出events及profile、2份无效预跑profile、6份Python字节码缓存。原始Trace已删除，当前调用栈诊断依赖本目录已提取的证据；需要重新查看完整Trace时应使用复现脚本重新采集。

未删除项目源码、运行所需的编译产物、验证地图、复现脚本或原有未提交工作。未额外运行PIE、编译或截图核验。完成登记依据用户收尾指令，不新增未执行检查的通过声明。

## GitHub 推送回执

当前项目变更已提交到本地`main`：`d8ecb28495edf762522b566f2be1c88dadd6eb6d`，共1369个文件，包含矿石导航、已有地面机甲/地图工具/资产与文档归档修改。暂存核对确认339个LFS路径均为指针、未夹带构建缓存；地图工具的原始二进制缓冲补齐LFS规则，证据文件保持原始字节。

`git push origin main`退出1，GitHub LFS返回：`This repository exceeded its LFS budget. The account responsible for the budget should increase it to restore access.` 上传显示0/100对象、0B，分支未更新。随后`git ls-remote origin refs/heads/main`确认远端仍为`6c339e942b78b05670f6372e819aebe317fb701c`。机器回执见`github-push.json`。

需仓库所属账户提高或解除LFS预算限制后再推送。未跳过LFS检查，未强推、改写历史或用缺失资产的提交替代项目。文档完成与临时文件清理已完成，GitHub上传仍受该外部限制阻塞；本回执另作本地提交保存。
