$ErrorActionPreference = 'Stop'
$sessionRoot = [System.IO.Path]::GetFullPath('D:\UE5.7\test1\ArtSource\Buildings\SSFStyle_20261005')
$cleanupDirectory = Join-Path $sessionRoot 'SessionCleanup_20261007'
$planPath = Join-Path $cleanupDirectory 'cleanup_plan.json'
$plan = Get-Content -LiteralPath $planPath -Raw -Encoding UTF8 | ConvertFrom-Json
$exactExternalPaths = @($plan.input_archive_checks | ForEach-Object { [System.IO.Path]::GetFullPath($_.temporary_input) })
$validatedFiles = @()
$emptyDirectoryCandidates = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)

# Validate the complete exact file list before removing anything.
foreach ($entry in $plan.files) {
    $resolvedPath = (Resolve-Path -LiteralPath $entry.path).Path
    $insideSession = $resolvedPath.StartsWith($sessionRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)
    $exactClipboardInput = $entry.reason -eq 'clipboard_temporary_input_with_verified_archived_copy' -and $exactExternalPaths -contains $resolvedPath
    if (-not ($insideSession -or $exactClipboardInput)) { throw "Deletion target outside authorized scope: $resolvedPath" }
    $item = Get-Item -LiteralPath $resolvedPath
    if ($item.PSIsContainer -or ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint)) { throw "Unexpected directory or reparse point: $resolvedPath" }
    if ($item.Length -ne $entry.bytes) { throw "File changed after inventory: $resolvedPath" }
    if ($item.Extension -in @('.uasset', '.umap', '.uexp', '.ubulk', '.pak')) { throw 'UE asset in deletion list' }
    if ($exactClipboardInput) {
        $currentHash = (Get-FileHash -LiteralPath $resolvedPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $archivedHash = (Get-FileHash -LiteralPath $entry.preserved_copy -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($currentHash -ne $entry.sha256 -or $archivedHash -ne $entry.sha256) { throw 'Clipboard reference changed or archive unavailable' }
    }
    if ($insideSession) {
        $parentDirectory = $item.Directory
        while ($null -ne $parentDirectory -and $parentDirectory.FullName.StartsWith($sessionRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
            [void]$emptyDirectoryCandidates.Add($parentDirectory.FullName)
            $parentDirectory = $parentDirectory.Parent
        }
    }
    $validatedFiles += $resolvedPath
}

foreach ($filePath in $validatedFiles) { Remove-Item -LiteralPath $filePath -Force }

$removedDirectories = @()
foreach ($directoryPath in ($emptyDirectoryCandidates | Sort-Object Length -Descending)) {
    $resolvedDirectory = [System.IO.Path]::GetFullPath($directoryPath)
    if (-not $resolvedDirectory.StartsWith($sessionRoot + '\', [System.StringComparison]::OrdinalIgnoreCase)) { throw 'Empty directory outside session root' }
    if (Test-Path -LiteralPath $resolvedDirectory -PathType Container) {
        if (@(Get-ChildItem -LiteralPath $resolvedDirectory -Force).Count -eq 0) {
            Remove-Item -LiteralPath $resolvedDirectory -Force
            $removedDirectories += $resolvedDirectory
        }
    }
}

$result = [ordered]@{
    success = $true
    user_quote = $plan.user_quote
    upload_steering = $plan.upload_steering
    scope = $sessionRoot
    removed_files = $validatedFiles.Count
    removed_bytes = $plan.bytes
    removed_empty_directories = $removedDirectories
    archived_approved_B_v1_source = 'Production_B_v1/FrozenSource/SSF_Production_B_v1_Approved.blend'
    current_B_v1_source_preserved = $true
    formal_UE_assets_deleted = $false
    gameplay_integrated = $false
    timestamp = (Get-Date).ToString('o')
}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $cleanupDirectory 'cleanup_result.json') -Encoding utf8
$result | ConvertTo-Json -Depth 3
