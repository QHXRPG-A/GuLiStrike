# GuLiStrike Progress Dashboard

本目录是 `Progress/` Markdown 唯一事实源的本地只读界面。它不会创建数据库，也不提供新增、编辑、删除或状态变更入口。

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

## 只读接口

- `GET /api/health`
- `GET /api/snapshot`
- `GET /api/search?q=关键词`
- `GET /api/documents/{id}`

服务固定绑定 `127.0.0.1`。文档只按元数据 ID 读取，非法 ID、路径穿越和所有写请求都会被拒绝。Markdown 文件修改后，服务按文件修改时间自动刷新内存缓存。

## 手动构建

```powershell
Set-Location .\Tools\ProgressDashboard
npm install
npm run build
python .\server.py --port 4317
```
