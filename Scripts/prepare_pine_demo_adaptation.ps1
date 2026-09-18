$ErrorActionPreference = 'Stop'
$sourceRoot = 'D:\BaiduNetdiskDownload\塞尔达松树林\StylizedPineEnvironment\StylizedPineEnvironment'
$projectRoot = 'D:\UE5.7\test1\Content\StylizedPineEnvironment'
$deliveryRoot = 'D:\UE5.7\test1\ArtSource\Environment\PineDemoAdaptation_20260918'
$backupRoot = Join-Path $deliveryRoot 'Backup_ProjectBefore'
if (Test-Path -LiteralPath $backupRoot) { throw 'Backup already exists; inspect manifest before resuming.' }
foreach ($part in @('Demo','Maps')) {
    if (Test-Path -LiteralPath (Join-Path $projectRoot $part)) { throw "Destination $part already exists; refusing overwrite." }
    if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot $part))) { throw "Missing source $part" }
}
New-Item -ItemType Directory -Path $deliveryRoot -Force | Out-Null
Copy-Item -LiteralPath $projectRoot -Destination $backupRoot -Recurse
$records = @()
foreach ($part in @('Demo','Maps')) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot $part) -Destination (Join-Path $projectRoot $part) -Recurse
    foreach ($f in Get-ChildItem -LiteralPath (Join-Path $sourceRoot $part) -Recurse -File) {
        $relative = $f.FullName.Substring($sourceRoot.Length + 1)
        $dest = Join-Path $projectRoot $relative
        $a = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash
        $b = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash
        if ($a -ne $b) { throw "Copy hash mismatch: $relative" }
        $records += @{relative=$relative; bytes=$f.Length; sha256=$a}
    }
}
$backupRecords = @()
foreach ($f in Get-ChildItem -LiteralPath $backupRoot -Recurse -File) {
    $relative = $f.FullName.Substring($backupRoot.Length + 1)
    $original = Join-Path $projectRoot $relative
    $a = (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash
    $b = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash
    if ($a -ne $b) { throw "Backup mismatch: $relative" }
    $backupRecords += @{relative=$relative; bytes=$f.Length; sha256=$a}
}
@{source=$sourceRoot; project=$projectRoot; backup=$backupRoot; copied=$records; backed_up=$backupRecords;
  acceptance_map='/Game/StylizedPineEnvironment/Maps/Demo_Map'; original_source_untouched=$true;
  user_approval='审核通过，全面重构这些资源；直接复制原地图，在项目原资源路径替换，不复刻布局。';
  approved_preview='ArtSource/Environment/PineStyleAdaptation_20260918/Review_v1.html'
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $deliveryRoot 'copy_and_backup.json') -Encoding utf8
@{copied_files=$records.Count; backed_up_files=$backupRecords.Count; output=$deliveryRoot} | ConvertTo-Json
