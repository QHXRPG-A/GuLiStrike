[CmdletBinding()]
param([string]$Label = 'network', [int]$Port = 17978, [switch]$RenderClients)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $projectRoot "TestResults/CommanderOrders/$Label"
if (Test-Path -LiteralPath $evidence) { throw "Evidence already exists: $evidence" }
New-Item -ItemType Directory -Path $evidence | Out-Null
$engineBinary = 'D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$project = Join-Path $projectRoot 'GuLiStrike.uproject'
$processes = [System.Collections.Generic.List[object]]::new()
try {
    foreach ($role in @('server', 'client1', 'client2')) {
        if ($role -eq 'server') {
            $url = '/Game/Maps/LVL_CommanderMassPrototype'
            $mode = 'server'
            $extra = @('-server', "-port=$Port")
        } else {
            $url = "127.0.0.1:$Port"
            $mode = 'client'
            $extra = @('-PktLag=100', '-PktLoss=2')
        }
        $report = (Join-Path $evidence "$role.json").Replace('\','/')
        $renderArgs = if ($RenderClients -and $role -ne 'server') { @('-RenderOffscreen','-windowed','-ResX=1920','-ResY=1080','-ForceRes') } else { @('-NullRHI') }
        $argsForProbe = @($project,$url,'-game','-Unattended','-NoSound','-NoSplash','-NoLiveCoding','-DisablePlugins=Fab',"-abslog=$evidence/$role.log",
            "-ExecCmds=`"t.MaxFPS 60,gs.Commander.OrderProbe $mode $report`"") + $extra
        $argsForProbe += $renderArgs
        $proc = Start-Process -FilePath $engineBinary -ArgumentList $argsForProbe -WindowStyle Hidden -PassThru
        $processes.Add($proc)
        if ($role -eq 'server') {
            $deadline = (Get-Date).AddSeconds(100)
            do {
                if ($proc.HasExited) { throw 'Server exited during setup' }
                if ((Test-Path -LiteralPath "$evidence/server.log") -and (Select-String -LiteralPath "$evidence/server.log" -SimpleMatch 'OrderProbe server ready' -Quiet)) { break }
                if ((Get-Date) -gt $deadline) { throw 'Server setup timeout' }
                Start-Sleep -Seconds 1
            } while ($true)
        }
    }
    $deadline = (Get-Date).AddSeconds(140)
    do {
        $active = @($processes | Where-Object { !$_.HasExited })
        if (!$active.Count) { break }
        if ((Get-Date) -gt $deadline) { throw 'Network probe timeout' }
        Start-Sleep -Seconds 1
    } while ($true)
    foreach ($role in @('server','client1','client2')) {
        $report = Get-Content -LiteralPath "$evidence/$role.json" -Raw | ConvertFrom-Json
        if (!$report.passed) { throw "$role failed: $($report.errors -join ', ')" }
        Write-Output "$role passed"
    }
} finally {
    foreach ($proc in $processes) { if (!$proc.HasExited) { Stop-Process -Id $proc.Id } }
}
