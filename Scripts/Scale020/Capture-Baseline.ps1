param([string]$Project = 'D:/UE5.7/test1')
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $Project
$outDir = Join-Path $Project 'TestResults/Scale020'
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
$output = Join-Path $outDir 'source-baseline.json'
if (Test-Path -LiteralPath $output) { throw 'Baseline already exists; never replace it with a migrated snapshot.' }
$paths = @(& rg --files Source/GuLiStrike Config Data/Excel Tools/DataPipeline --glob '!Intermediate/**' --glob '!Saved/**' --glob '!Binaries/**' --glob '!Content/Assets/**' --glob '!DerivedDataCache/**' --glob '!Downloads/**' --glob '!**/__pycache__/**')
$rows = foreach ($path in ($paths | Sort-Object -Unique)) {
    $item = Get-Item -LiteralPath $path
    [ordered]@{path=$path.Replace('\','/'); sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant(); bytes=$item.Length}
}
$snapshot = [ordered]@{schema='guli-scale020/baseline-v1'; multiplier=0.2; captured_at=(Get-Date).ToUniversalTime().ToString('o'); git_head=(& git rev-parse HEAD); preexisting_status=@(& git status --short); files=@($rows)}
$snapshot | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $output -Encoding utf8NoBOM
Write-Output "Recorded $($rows.Count) source/config/workbook hashes in $output"
