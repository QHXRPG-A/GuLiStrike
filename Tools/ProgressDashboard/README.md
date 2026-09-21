# GuLiStrike Progress Dashboard

本目录是 `Progress/` Markdown 唯一事实源的本地界面。默认只读，唯一例外是「当前推进队列」的状态写入：右键队列条目可直接设置开发状态（规划/实施/验收/完成/废弃），或通过「记录阻塞点」弹窗写入阻塞说明并把状态置为阻塞；服务端只改写对应根开发文档 front matter 的 `status`、`updated`（阻塞时含 `status_note`）字段并自动重建 `_Index`。除此之外不提供新增、编辑或删除入口。

## 启动

### 常驻服务（推荐）

注册为 Windows 计划任务：当前用户登录时自动启动，无窗口常驻，异常退出后每分钟自动重试；重复启动会自动幂等退出，不会产生多实例。

```powershell
.\Tools\ProgressDashboard\install_startup_task.ps1 -Start   # 注册并立即启动
.\Tools\ProgressDashboard\install_startup_task.ps1 -Remove  # 取消开机自启
```

日志写入 `Tools\ProgressDashboard\service.log`（超过 2MB 自动轮转为 `service.log.old`）。手动重启：任务计划程序中结束任务后重新运行，或直接杀掉 `pythonw.exe` 进程，任务会在一分钟内自动拉起。

### 前台运行

在 PowerShell 中运行：

```powershell
.\Tools\ProgressDashboard\start.ps1
```

首次缺少构建产物时，脚本会安装依赖并执行生产构建；之后直接启动前台 Python 服务并打开 <http://127.0.0.1:4317>。按 `Ctrl+C` 停止。

## 接口

### 只读

- `GET /api/health`
- `GET /api/snapshot`
- `GET /api/search?q=关键词`
- `GET /api/documents/{id}`
- `GET /api/artsource?dir=ArtSource下的相对目录`（逐级列目录，含大小、类型与子项计数）
- `GET /api/artsource/file?p=ArtSource下的相对文件`（仅允许图片、视频与 Markdown，路径禁止越界）

### 受限写入

- `POST /api/work-items/status`，请求体 `{"work_id": "WORK-…", "status": "planned|in_progress|blocked|verification|done|abandoned", "status_note": "可选；阻塞说明，最长 500 字"}`。
  - 仅接受白名单内的开发状态；目标必须是该 work_id 的根开发文档。
  - 写入前校验 front matter 存在 `status`/`updated` 字段，日期以带引号字符串写回（避免 YAML 解析成 date 对象破坏索引构建）。
  - `status_note` 非空时一并写回（单引号 YAML 标量，换行折叠为空格）；`blocked` 状态要求最终 `status_note` 非空，否则返回 422。
  - 切换到活跃状态（规划/实施/验收）时要求文档已填写 `next_action`，否则返回 422。
  - 写入成功后同步执行 `progress_docs.py build` 重建 `_Index`；失败时响应带 `index_error` 字段。
  - `changed: true` 的响应会附带写入后的最新 `snapshot`，前端据此立即刷新整页数据，不等待 15 秒轮询。
  - 缺少开发文档返回 404，非法参数返回 400。

服务固定绑定 `127.0.0.1`。文档只按元数据 ID 读取，非法 ID、路径穿越和其他所有写请求都会被拒绝。Markdown 文件修改后，服务按文件修改时间自动刷新内存缓存。侧边栏「美术相关」视图汇总 Progress 美术分类文档，并只读浏览 `ArtSource/` 素材目录。

## 队列交互

「当前推进队列」（战情总览与阶段聚焦视图共用）支持：

- **阶段筛选**：表头上方按阶段过滤条目，芯片带实时计数，默认「全部」。
- **列排序**：点击 工作项/阶段/任务/更新 表头切换排序方向，默认按更新日期降序。
- **右键设置状态**：右键条目打开菜单，选择目标状态后写回文档元数据；当前状态项禁用，「废弃」为终止性操作、以红色区分。废弃的条目退出活跃队列（阶段计入完成桶），在「完成」阶段视图中以红色「废弃」徽章显示，可右键改回其他状态。缺少开发文档的条目会提示改用 Markdown 修改。写入期间菜单关闭，失败原因显示在表格下方。
- **记录阻塞点**：菜单底部「记录阻塞点…」打开弹窗，编辑阻塞说明（预填当前 status_note，最长 500 字）后保存——说明写入 `status_note`、状态置为「阻塞」。阻塞是独立阶段：顶部流水线、阶段筛选与总览「当前阻塞」卡片都会统计；解除阻塞用右键切回其他状态即可。

## 手动构建

```powershell
Set-Location .\Tools\ProgressDashboard
npm install
npm run build
python .\server.py --port 4317
```
