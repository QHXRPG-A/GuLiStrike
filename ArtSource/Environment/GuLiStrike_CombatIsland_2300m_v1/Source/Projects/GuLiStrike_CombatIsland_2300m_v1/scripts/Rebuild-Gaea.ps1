param(
    [ValidateSet('draft','final')][string]$Stage = 'final',
    [switch]$RefreshLayout
)
$ErrorActionPreference = 'Stop'
$islandScriptRoot = $PSScriptRoot
$islandNode = (Get-Command node -ErrorAction Stop).Source
$islandPython = (Get-Command python -ErrorAction Stop).Source
if ($RefreshLayout) {
    & $islandPython (Join-Path $islandScriptRoot 'prepare_sources.py')
    if ($LASTEXITCODE -ne 0) { throw 'Source layout generation failed.' }
    & $islandPython (Join-Path $islandScriptRoot 'analyze_exports.py') foundation
    if ($LASTEXITCODE -ne 0) { throw 'Initial mask generation failed.' }
}
& $islandNode (Join-Path $islandScriptRoot 'gaea_project.mjs') $Stage --build
if ($LASTEXITCODE -ne 0) { throw 'Gaea terrain build failed.' }
& $islandPython (Join-Path $islandScriptRoot 'analyze_exports.py') $Stage --refresh-source-masks
if ($LASTEXITCODE -ne 0) { throw 'Terrain validation failed; inspect validation.json and previews.' }
& $islandNode (Join-Path $islandScriptRoot 'gaea_project.mjs') $Stage --build
if ($LASTEXITCODE -ne 0) { throw 'Gaea material mask build failed.' }
& $islandPython (Join-Path $islandScriptRoot 'analyze_exports.py') $Stage
if ($LASTEXITCODE -ne 0) { throw 'Final terrain validation failed.' }
if ($Stage -eq 'final') {
    & $islandPython (Join-Path $islandScriptRoot 'verify_delivery.py')
    if ($LASTEXITCODE -ne 0) { throw 'Delivery file verification failed.' }
}
