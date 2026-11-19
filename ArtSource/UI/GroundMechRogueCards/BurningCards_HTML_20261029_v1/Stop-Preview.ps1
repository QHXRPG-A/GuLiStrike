$ErrorActionPreference='Stop'
$previewTaskName='GuLi-BurningCards-Preview-20261029'
$previewTask=Get-ScheduledTask -TaskName $previewTaskName -TaskPath '\' -ErrorAction SilentlyContinue
if ($previewTask) {
    $previewScript=Join-Path $PSScriptRoot 'PreviewServer.py'
    if ($previewTask.Actions[0].Arguments -notlike ('*'+$previewScript+'*')) { throw 'The task action does not match this preview.' }
    Disable-ScheduledTask -TaskName $previewTaskName -TaskPath '\' | Out-Null
    Stop-ScheduledTask -TaskName $previewTaskName -TaskPath '\'
    Write-Output 'Card preview stopped. The offline HTML remains usable.'
}
