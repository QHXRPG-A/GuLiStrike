param([string]$ReportName = 'Automation-01')
$ErrorActionPreference = 'Stop'
$projectRoot = 'D:/UE5.7/test1'
$reportRoot = Join-Path $projectRoot "TestResults/Scale020/$ReportName"
# Targeted correctness suites only; deliberately excludes performance/stress campaigns.
$filter = 'GuLiStrike.Scale020+GuLiStrike.Commander.Data+GuLiStrike.Commander.Selection+GuLiStrike.Commander.Network+GuLiStrike.Commander.Camera+GuLiStrike.Commander.Framework+GuLiStrike.Commander.Combat+GuLiStrike.Commander.Skills+GuLiStrike.Commander.Mass.Navigation+GuLiStrike.Resources+GuLiStrike.Building+GuLiStrike.Ship.Abilities+GuLiStrike.Ship.Build+GuLiStrike.Ship.Parts+GuLiStrike.Ship.WorldHUD+GuLiStrike.Ship.Movement+GuLiStrike.Wingman.Attack+GuLiStrike.Wingman.Combat+GuLiStrike.CombatEffects+GuLiStrike.Combat.Missile+GuLiStrike.Combat.ShipProjectile+GuLiStrike.Skills.Lifecycle+GuLiStrike.Teleport+GuLiStrike.RuntimeTuning'
& 'D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' "$projectRoot/GuLiStrike.uproject" /Engine/Maps/Entry `
    -Unattended -NullRHI -NoSound -NoSplash -NoLiveCoding '-DisablePlugins=Fab,UnrealMCPython,UnrealMCP' `
    "-ExecCmds=Automation RunTests $filter" '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$reportRoot" "-abslog=$reportRoot.log"
$engineExit = $LASTEXITCODE
if (!(Test-Path -LiteralPath "$reportRoot/index.json")) { throw "Missing test report; engine exit $engineExit" }
$report = Get-Content -LiteralPath "$reportRoot/index.json" -Raw | ConvertFrom-Json
Write-Output "Passed=$($report.succeeded + $report.succeededWithWarnings) Failed=$($report.failed) NotRun=$($report.notRun) EngineExit=$engineExit"
if ($engineExit -ne 0 -or $report.failed -gt 0 -or $report.notRun -gt 0) { exit 1 }
