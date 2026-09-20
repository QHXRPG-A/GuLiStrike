[CmdletBinding()]
param([string]$Label = 'business')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$evidence = Join-Path $projectRoot "TestResults/CommanderOrders/$Label"
if (Test-Path -LiteralPath $evidence) { throw "Evidence already exists: $evidence" }
New-Item -ItemType Directory -Path $evidence | Out-Null
$reportPath = (Join-Path $evidence 'report.json').Replace('\','/')
$probeArgs = @((Join-Path $projectRoot 'GuLiStrike.uproject'), '/Game/Maps/LVL_CommanderMassPrototype',
    '-game', '-server', '-port=17982', '-NullRHI', '-Unattended', '-NoSound', '-NoSplash', '-NoLiveCoding', '-DisablePlugins=Fab',
    "-abslog=$evidence/probe.log", "-ExecCmds=`"t.MaxFPS 60,gs.Commander.OrderBusinessProbe $reportPath`"")
$process = Start-Process -FilePath 'D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' -ArgumentList $probeArgs -WindowStyle Hidden -PassThru
try {
    if (!$process.WaitForExit(360000)) { throw 'Business probe timeout' }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if (!$report.passed) { throw "Business probe failed at stage $($report.stage): $($report.errors -join ', ')" }
    Write-Output "Business probe passed: $(@($report.checks.PSObject.Properties).Count) checks"
} finally {
    if (!$process.HasExited) { Stop-Process -Id $process.Id }
}
