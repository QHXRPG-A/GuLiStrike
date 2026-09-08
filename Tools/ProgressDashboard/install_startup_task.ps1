[CmdletBinding()]
param(
    [switch]$Remove,
    [switch]$Start
)

$ErrorActionPreference = 'Stop'
$TaskName = 'GuLiStrike ProgressDashboard'

if ($Remove) {
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host "已删除计划任务 '$TaskName'（正在运行的实例不受影响）。"
    return
}

$DashboardRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$PythonExe = (Get-Command python -ErrorAction Stop).Source
$PythonwExe = Join-Path (Split-Path -Parent $PythonExe) 'pythonw.exe'
if (-not (Test-Path -LiteralPath $PythonwExe)) {
    throw "未找到 pythonw.exe：$PythonwExe"
}
$Launcher = Join-Path $DashboardRoot 'service_launcher.py'
if (-not (Test-Path -LiteralPath $Launcher)) {
    throw "未找到服务入口：$Launcher"
}

$Action = New-ScheduledTaskAction -Execute $PythonwExe -Argument ('"{0}"' -f $Launcher) -WorkingDirectory $DashboardRoot
$Trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
$Settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable `
    -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -RestartCount 10 -RestartInterval (New-TimeSpan -Minutes 1) `
    -MultipleInstances IgnoreNew

Register-ScheduledTask -TaskName $TaskName `
    -Description 'GuLiStrike Progress Dashboard 只读本地服务（http://127.0.0.1:4317），登录自启，崩溃自动重启。' `
    -Action $Action -Trigger $Trigger -Settings $Settings -Force | Out-Null
Write-Host "已注册计划任务 '$TaskName'：当前用户登录时自启（无窗口），失败后每分钟重试。"

if ($Start) {
    Start-ScheduledTask -TaskName $TaskName
    Write-Host '已触发启动，稍后访问 http://127.0.0.1:4317'
}
