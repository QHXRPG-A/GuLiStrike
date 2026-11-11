[CmdletBinding()]
param(
    [switch]$Remove,
    [switch]$Start
)

$ErrorActionPreference = 'Stop'
$TaskName = 'GuLiStrike ProgressDashboard'
$TaskPath = '\'

function Stop-RunningTask {
    $Task = Get-ScheduledTask -TaskName $TaskName -TaskPath $TaskPath -ErrorAction SilentlyContinue
    if ($Task.State -ne 'Running') { return }

    Stop-ScheduledTask -TaskName $TaskName -TaskPath $TaskPath
    for ($Attempt = 0; $Attempt -lt 50; $Attempt++) {
        if ((Get-ScheduledTask -TaskName $TaskName -TaskPath $TaskPath).State -ne 'Running') { return }
        Start-Sleep -Milliseconds 200
    }
    throw "计划任务 '$TaskName' 未在 10 秒内停止。"
}

if ($Remove) {
    Stop-RunningTask
    if (Get-ScheduledTask -TaskName $TaskName -TaskPath $TaskPath -ErrorAction SilentlyContinue) {
        Unregister-ScheduledTask -TaskName $TaskName -TaskPath $TaskPath -Confirm:$false
    }
    Write-Host "已取消本地工具保活任务 '$TaskName'。"
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

$Action = New-ScheduledTaskAction -Execute $PythonwExe -Argument ('-B -X utf8 "{0}"' -f $Launcher) -WorkingDirectory $DashboardRoot
$Trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
$Settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -StartWhenAvailable `
    -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -RestartCount 10 -RestartInterval (New-TimeSpan -Minutes 1) `
    -MultipleInstances IgnoreNew

Register-ScheduledTask -TaskName $TaskName -TaskPath $TaskPath `
    -Description 'GuLiStrike 本地工具保活：项目进度面板、独立地图设计器、肉鸽牌关系图。登录时无窗口自启。' `
    -Action $Action -Trigger $Trigger -Settings $Settings -Force | Out-Null
Write-Host "已注册计划任务 '$TaskName'：当前用户登录时自启（无窗口），失败后每分钟重试。"

if ($Start) {
    Stop-RunningTask
    Start-ScheduledTask -TaskName $TaskName -TaskPath $TaskPath
    Write-Host '已触发本地工具保活服务启动。'
}
