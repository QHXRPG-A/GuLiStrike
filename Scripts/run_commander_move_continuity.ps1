[CmdletBinding()]
param([string]$Label = (Get-Date -Format 'yyyyMMdd-HHmmss'), [switch]$Render, [string]$ProjectPath = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$ProjectPath) { $ProjectPath = Join-Path $projectRoot 'GuLiStrike.uproject' }
$ProjectPath = (Resolve-Path -LiteralPath $ProjectPath).Path
$evidence = Join-Path $projectRoot "TestResults/CommanderMoveContinuity/$Label"
if (Test-Path -LiteralPath $evidence) { throw "Evidence already exists: $evidence" }
New-Item -ItemType Directory -Path $evidence | Out-Null
$reportPath = (Join-Path $evidence 'report.json').Replace('\','/')
$modeArgs = if ($Render) { @('-RenderOffscreen', '-windowed', '-ResX=1280', '-ResY=720', '-ForceRes') } else { @('-server', '-port=17984', '-NullRHI') }
$probeArgs = @($ProjectPath, '/Game/Maps/LVL_CommanderMassPrototype',
    '-game', '-Unattended', '-NoSound', '-NoSplash', '-NoLiveCoding', '-DisablePlugins=Fab',
    "-abslog=$evidence/probe.log", "-ExecCmds=`"t.MaxFPS 60,r.MotionBlurQuality 0,gs.Commander.MoveContinuityProbe $reportPath`"") + $modeArgs
$process = Start-Process -FilePath 'D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' -ArgumentList $probeArgs -WindowStyle Hidden -PassThru
try {
    $deadline = (Get-Date).AddSeconds(300)
    while (!$process.WaitForExit(1000)) {
        if ((Get-Date) -gt $deadline) { throw 'Move continuity probe timeout' }
    }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if (!$report.passed) { throw "Move continuity failed at stage $($report.stage): $($report.errors -join ', ')" }
    Write-Output "Move continuity passed: $(@($report.checks.PSObject.Properties).Count) checks; $($report.pending_samples) moving samples while replanning. $reportPath"
} finally {
    if (!$process.HasExited) { Stop-Process -Id $process.Id }
}
