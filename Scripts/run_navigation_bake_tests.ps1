param([string]$ReportName = 'Automation')
$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$ReportRoot = Join-Path $ProjectRoot "TestResults\NavigationBake-20260915\$ReportName"
$LogPath = "$ReportRoot.log"
& 'D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' (Join-Path $ProjectRoot 'GuLiStrike.uproject') /Engine/Maps/Entry `
    -Unattended -NullRHI -NoSound -NoSplash -NoLiveCoding -DisablePlugins=Fab `
    '-ExecCmds=Automation RunTests GuLi.NavigationBake+GuLi.FlightNavigation.Editor' '-TestExit=Automation Test Queue Empty' `
    "-ReportExportPath=$ReportRoot" "-abslog=$LogPath"
if ($LASTEXITCODE -ne 0) { throw "Navigation automation exited with $LASTEXITCODE; inspect $LogPath" }
$Report = Get-Content -LiteralPath (Join-Path $ReportRoot 'index.json') -Raw | ConvertFrom-Json
if ($Report.failed -gt 0 -or $Report.notRun -gt 0 -or ($Report.succeeded + $Report.succeededWithWarnings) -eq 0) {
    throw "Navigation acceptance failed: $ReportRoot"
}
Write-Output "Passed $($Report.succeeded + $Report.succeededWithWarnings) navigation tests: $ReportRoot"
