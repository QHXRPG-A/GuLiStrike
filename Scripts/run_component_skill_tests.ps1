[CmdletBinding()]
param(
    [string]$Filter = 'GuLiStrike.Ship.Abilities+GuLiStrike.Ship.Build+GuLiStrike.Commander.Skills+GuLiStrike.Wingman.Attack+GuLiStrike.Wingman.Combat+GuLiStrike.Skills.Lifecycle+GuLiStrike.Teleport',
    [string]$ReportName = 'Automation'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$reportRoot = Join-Path $projectRoot "TestResults\ComponentSkills\$ReportName"
$logPath = Join-Path $projectRoot "TestResults\ComponentSkills\$ReportName.log"
& 'D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' (Join-Path $projectRoot 'GuLiStrike.uproject') /Engine/Maps/Entry `
    -Unattended -NullRHI -NoSound -NoSplash -NoLiveCoding -DisablePlugins=Fab `
    "-ExecCmds=Automation RunTests $Filter" '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$reportRoot" "-abslog=$logPath"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# UE's TestExit can return 0 even when individual tests fail; the report is authoritative.
$report = Get-Content -LiteralPath (Join-Path $reportRoot 'index.json') -Raw | ConvertFrom-Json
if ($report.failed -gt 0 -or $report.notRun -gt 0 -or ($report.succeeded + $report.succeededWithWarnings) -eq 0) {
    throw "Component skill acceptance failed: $reportRoot"
}
Write-Output "Passed $($report.succeeded + $report.succeededWithWarnings) tests: $reportRoot"
