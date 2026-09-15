[CmdletBinding()]
param([ValidateSet('Both','Dedicated','Listen')][string]$Mode = 'Both')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$modes = if ($Mode -eq 'Both') { @('Dedicated','Listen') } else { @($Mode) }
foreach ($networkMode in $modes) {
    $name = $networkMode.ToLowerInvariant()
    $modeId = if ($networkMode -eq 'Dedicated') { 2 } else { 1 }
    $reportPath = Join-Path $projectRoot "TestResults\ComponentSkills\Network\$name.json"
    $started = Get-Date
    & 'D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' (Join-Path $projectRoot 'GuLiStrike.uproject') /Game/Maps/LVL_CommanderMassPrototype `
        -Unattended -NullRHI -NoSound -NoSplash -NoLiveCoding -DisablePlugins=Fab `
        "-ExecutePythonScript=$PSScriptRoot\validate_component_skills_network.py" "-ComponentSkillsMode=$modeId" `
        "-abslog=$projectRoot\TestResults\ComponentSkills\network-$name.log"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    if (!(Test-Path -LiteralPath $reportPath) -or (Get-Item -LiteralPath $reportPath).LastWriteTime -lt $started) {
        throw "No fresh $networkMode acceptance report: $reportPath"
    }
    $report = Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    if (!$report.passed) { throw "$networkMode skill acceptance failed: $reportPath" }
    Write-Output "$networkMode passed: $reportPath"
}
