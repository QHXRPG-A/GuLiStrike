$ErrorActionPreference = 'Stop'
$deliveryRoot = 'D:\UE5.7\test1\ArtSource\Environment\PineDemoAdaptation_20260918'
$manifest = Get-Content -LiteralPath (Join-Path $deliveryRoot 'copy_and_backup.json') -Raw | ConvertFrom-Json
$sourceChecks = @()
foreach ($r in @($manifest.copied) + @($manifest.backed_up)) {
    $p = Join-Path $manifest.source $r.relative
    $sha = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
    $sourceChecks += @{relative=$r.relative; sha256=$sha; same_as_baseline=($sha -eq $r.sha256)}
}
$approvalRoot = 'D:\UE5.7\test1\ArtSource\Environment\PineStyleAdaptation_20260918'
$approved = @()
$approvalFiles = @((Get-Item -LiteralPath (Join-Path $approvalRoot 'Review_v1.html')))
$approvalFiles += @(Get-ChildItem -LiteralPath (Join-Path $approvalRoot 'Previews') -File -Filter '*.png')
foreach ($f in $approvalFiles) {
    $approved += @{path=$f.FullName; bytes=$f.Length; sha256=(Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash}
}
$contentRecords = @()
foreach ($f in Get-ChildItem -LiteralPath (Join-Path $manifest.project 'Assets') -File -Recurse) {
    $contentRecords += @{relative=$f.FullName.Substring($manifest.project.Length+1); bytes=$f.Length;
        sha256=(Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash}
}
$mapFile = Join-Path $manifest.project 'Maps\Demo_Map.umap'
$mapRecords = @()
foreach ($relative in @('Maps\Demo_Map.umap','Maps\Demo_Map_BuiltData.uasset')) {
    $f = Get-Item -LiteralPath (Join-Path $manifest.project $relative)
    $mapRecords += @{relative=$relative; bytes=$f.Length; sha256=(Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash}
}
$evidence = @()
$evidenceFiles = @('Review_Demo_v1.html','README.md','readback.json','final_readback.json','camera_presets.json','gallery.json','editor_stability.md')
foreach ($relative in $evidenceFiles) {
    $f = Get-Item -LiteralPath (Join-Path $deliveryRoot $relative)
    $evidence += @{relative=$relative; bytes=$f.Length; sha256=(Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash}
}
foreach ($f in Get-ChildItem -LiteralPath (Join-Path $deliveryRoot 'Previews') -File -Filter '*.png') {
    $evidence += @{relative=('Previews/'+$f.Name); bytes=$f.Length; sha256=(Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash}
}
$sourceChanged = @($sourceChecks | Where-Object { -not $_.same_as_baseline })
@{acceptance_map='/Game/StylizedPineEnvironment/Maps/Demo_Map'; map_sha256=(Get-FileHash -LiteralPath $mapFile -Algorithm SHA256).Hash;
    approved_v1=$approved; source_integrity=$sourceChecks; source_baseline_mismatches=$sourceChanged.Count;
    adapted_content=$contentRecords; acceptance_map_files=$mapRecords; review_evidence=$evidence;
    generated_at=(Get-Date).ToString('o');
    source_blender_or_fbx='Not supplied by vendor; delivery is editable UE assets plus authoring recipes';
    review_status='v1 approved; full Demo_Map awaits final visual approval';
    performance_status='Not profiled; geometry counts do not imply GPU or draw-call pass'
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $deliveryRoot 'delivery_manifest.json') -Encoding utf8
@{source_checked=$sourceChecks.Count; source_baseline_mismatches=$sourceChanged.Count; approved_files=$approved.Count;
    content_files=$contentRecords.Count; map_sha256=(Get-FileHash -LiteralPath $mapFile -Algorithm SHA256).Hash} | ConvertTo-Json
