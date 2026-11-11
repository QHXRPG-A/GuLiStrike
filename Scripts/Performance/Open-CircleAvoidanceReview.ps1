param(
    [ValidateSet('crossing','hold','gather','mixed','attack','buildings','ore','work')]
    [string]$Scenario = 'crossing'
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$outputPath = Join-Path $projectRoot ('Artifacts/CircularAvoidancePreview/' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $Scenario)
Push-Location $projectRoot
try {
    & python 'Scripts/Performance/capture_circular_avoidance.py' --scenario $Scenario --output $outputPath --preview
    if ($LASTEXITCODE -ne 0) { throw 'Preview preparation failed. Inspect failure.json in the output directory.' }
} finally { Pop-Location }
