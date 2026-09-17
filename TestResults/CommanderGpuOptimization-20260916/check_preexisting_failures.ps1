# Re-run the same existing suite against the session's original source/config to preserve test order.
# Run after all performance captures. Always restore the implementation before final builds.
$ErrorActionPreference = 'Stop'
$project = 'D:\UE5.7\test1'
$evidence = Join-Path $project 'TestResults\CommanderGpuOptimization-20260916'
$build = 'D:\UnrealEngine-5.7\Engine\Build\BatchFiles\Build.bat'
$editor = 'D:\UnrealEngine-5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$manifest = Get-Content -LiteralPath "$evidence\before-manifest.json" -Raw | ConvertFrom-Json
$snapshot = Join-Path $evidence 'implementation-snapshot'
if (Test-Path -LiteralPath $snapshot) {
    $manifest = Get-Content -LiteralPath "$evidence\control-manifest.json" -Raw | ConvertFrom-Json
    foreach ($entry in $manifest) {
        if ((Get-FileHash -LiteralPath (Join-Path $project $entry.path) -Algorithm SHA256).Hash -ne $entry.implementation_sha256) { throw "Review current state before resuming: $($entry.path)" }
    }
} else {
    foreach ($entry in $manifest) {
        $source = Join-Path $project $entry.path
        $destination = Join-Path $snapshot $entry.path
        New-Item -ItemType Directory -Path (Split-Path $destination -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination
        $entry | Add-Member -NotePropertyName implementation_sha256 -NotePropertyValue (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$evidence\control-manifest.json" -Encoding utf8
}
$replaced = [System.Collections.Generic.List[object]]::new()
try {
    foreach ($entry in $manifest) {
        Copy-Item -LiteralPath (Join-Path "$evidence\before" $entry.path) -Destination (Join-Path $project $entry.path) -Force
        $replaced.Add($entry)
        if ($entry.path.EndsWith(".cpp")) { (Get-Item -LiteralPath (Join-Path $project $entry.path)).LastWriteTimeUtc = [DateTime]::UtcNow }
    }
    & $build GuLiStrikeEditor Win64 Development "-Project=$project\GuLiStrike.uproject" -WaitMutex -NoHotReloadFromIDE *> "$evidence\control-editor-build.log"
    if ($LASTEXITCODE -ne 0) { throw 'Original-source control build failed.' }
    $filters = 'GuLiStrike.Commander.Presentation.+GuLiStrike.Wingman.Actor.ClientOnlyPawnExecutionDomain+GuLiStrike.Wingman.StateTree.+GuLiStrike.Wingman.Pawn.+GuLiStrike.Wingman.Simulation.WeaponOnlyConfigPreservesFormation+GuLiStrike.Teleport.+GuLiStrike.Building.BuildingWorldTests+GuLiStrike.Resources.Rendering.HISMSwapIndex+GuLiStrike.CombatEffects.'
    & $editor "$project\GuLiStrike.uproject" -unattended -NullRHI -NoSound -NoSplash -NoLiveCoding -DisablePlugins=Fab "-ExecCmds=Automation RunTests $filters" '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$evidence\Automation-original-source" "-abslog=$evidence\automation-original-source.log" *> "$evidence\automation-original-source-console.log"
    Write-Output "Original-source test process exit=$LASTEXITCODE; read its JSON test results."
} catch {
    Write-Output $_.Exception.Message
    throw
} finally {
    foreach ($entry in $replaced) {
        $target = Join-Path $project $entry.path
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $entry.sha256) {
            throw "Concurrent edit found at $target; implementation copy retained in $snapshot. Restore only after reviewing."
        }
        Copy-Item -LiteralPath (Join-Path $snapshot $entry.path) -Destination $target -Force
        if ($entry.path.EndsWith(".cpp")) { (Get-Item -LiteralPath $target).LastWriteTimeUtc = [DateTime]::UtcNow }
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $entry.implementation_sha256) { throw "Restore hash mismatch: $target" }
    }
}
& $build GuLiStrikeEditor Win64 Development "-Project=$project\GuLiStrike.uproject" -WaitMutex -NoHotReloadFromIDE *> "$evidence\editor-build-final.log"
if ($LASTEXITCODE -ne 0) { throw 'Final implementation Editor build failed.' }
& $build GuLiStrike Win64 Development "-Project=$project\GuLiStrike.uproject" -WaitMutex -NoHotReloadFromIDE *> "$evidence\game-build-final.log"
if ($LASTEXITCODE -ne 0) { throw 'Final implementation Game build failed.' }
Write-Output 'Implementation restored, Editor and Game builds passed.'
