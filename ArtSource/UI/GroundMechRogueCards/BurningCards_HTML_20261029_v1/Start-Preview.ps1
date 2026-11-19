param([switch]$NoBrowser)
$ErrorActionPreference='Stop'
$previewTaskName='GuLi-BurningCards-Preview-20261029'
$previewPython='C:\Users\a\AppData\Local\Programs\Python\Python313\pythonw.exe'
$previewScript=Join-Path $PSScriptRoot 'PreviewServer.py'
$previewTask=Get-ScheduledTask -TaskName $previewTaskName -TaskPath '\' -ErrorAction SilentlyContinue
if ($previewTask) {
    if ($previewTask.Actions[0].Arguments -notlike ('*'+$previewScript+'*')) { throw 'The preview task name belongs to another action.' }
    if ($previewTask.Actions[0].Execute -ne $previewPython) {
        $previewAction=New-ScheduledTaskAction -Execute $previewPython -Argument ('-u "'+$previewScript+'"') -WorkingDirectory $PSScriptRoot
        Set-ScheduledTask -TaskName $previewTaskName -TaskPath '\' -Action $previewAction | Out-Null
        Stop-ScheduledTask -TaskName $previewTaskName -TaskPath '\'
    }
} else {
    $previewListener=Get-NetTCPConnection -LocalPort 8192 -State Listen -ErrorAction SilentlyContinue
    if ($previewListener) { throw 'Port 8192 is already occupied; no process was stopped.' }
    $previewIdentity=[Security.Principal.WindowsIdentity]::GetCurrent()
    $previewAction=New-ScheduledTaskAction -Execute $previewPython -Argument ('-u "'+$previewScript+'"') -WorkingDirectory $PSScriptRoot
    $previewPrincipal=New-ScheduledTaskPrincipal -UserId $previewIdentity.User.Value -LogonType Interactive -RunLevel Limited
    $previewSettings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)
    Register-ScheduledTask -TaskName $previewTaskName -TaskPath '\' -Action $previewAction -Principal $previewPrincipal -Settings $previewSettings -Description 'Local read-only two-card HTML preview; manual start, no logon trigger.' | Out-Null
}
Enable-ScheduledTask -TaskName $previewTaskName -TaskPath '\' | Out-Null
Start-ScheduledTask -TaskName $previewTaskName -TaskPath '\'
for ($previewAttempt=0;$previewAttempt -lt 20;$previewAttempt++) {
    try {
        $previewHealth=Invoke-RestMethod -Uri 'http://127.0.0.1:8192/_health' -TimeoutSec 2
        if ($previewHealth.ready -and $previewHealth.layers -eq 12) {
            if (!$NoBrowser) { Start-Process 'http://127.0.0.1:8192/' }
            Write-Output 'Card preview ready: http://127.0.0.1:8192/'
            exit 0
        }
    } catch {}
    Start-Sleep -Seconds 1
}
throw 'The card preview did not become ready. Check preview-server.log.'
