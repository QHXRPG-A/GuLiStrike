#requires -Version 7.0
<#
.SYNOPSIS
Previews or removes the named 2026-10-17/18 performance-session temporary files.
.DESCRIPTION
Run without -Apply to inspect the exact paths. Save and close this project's
editor and clients before applying. This tool does not stop processes, search
generated trees, remove runtime binaries, or select newer captures by date.
It produces its receipt on stdout without creating another temporary file.
#>
[CmdletBinding(SupportsShouldProcess=$true,ConfirmImpact='Medium')]
param([switch]$Apply)

$ErrorActionPreference='Stop'
$guliSessionProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')).TrimEnd('\','/')
if (-not (Test-Path -LiteralPath (Join-Path $guliSessionProject 'GuLiStrike.uproject') -PathType Leaf)) {
    throw 'This tool must remain in GuLiStrike/Scripts/Network.'
}
$guliSessionCaptures=Join-Path (Split-Path $guliSessionProject -Parent) 'PerformanceCaptures'
$guliSessionTemp=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Temp'
$guliSessionTargets=@(
    foreach ($guliCapture in @(
        '20261017-AsyncHUD-v1','20261017-Hotspots-v1','20261017-PIE-mvb86w2i',
        '20261018-GameplayWorker-v1','20261018-LongPIE-v1','20261018-MassParallel-v1',
        '20261018-VisualRegression-v1','20261018-WM01-Q-v1'
    )) { Join-Path $guliSessionCaptures $guliCapture }
    foreach ($guliRelative in @(
        'Tools/GuLiCoordinator/build/native','Scripts/Performance/__pycache__',
        'Scripts/Network/__pycache__','Scripts/__pycache__',
        'outputs/commander-selection-20260831'
    )) { Join-Path $guliSessionProject $guliRelative }
    foreach ($guliRoom in @(
        '5273C21E4947F965CF92019315A7BFCD','608940CB493597A8634270966AA0C3CB',
        '7074597D4972D8BDC4FCFC8C9D97226A','B034D56C489A4B94D9603AA7ABC44B1A',
        'C2AC61E54D77DB6C2667C8A502486855'
    )) { Join-Path (Join-Path $guliSessionTemp 'GuLiStrikePIE') $guliRoom }
    foreach ($guliScreenshot in @(
        'codex-clipboard-db7f4ea1-d494-4826-a652-c1884744bbf1.png',
        'codex-clipboard-0eec2787-64d9-4123-be23-f4c1b5c61df1.png',
        'codex-clipboard-6ddd1110-6bbc-469d-8df6-9c9c60d1ca69.png'
    )) { Join-Path $guliSessionTemp $guliScreenshot }
)
$guliSessionRoots=@($guliSessionProject,$guliSessionCaptures,$guliSessionTemp)
$guliSessionPlan=@(foreach ($guliTarget in $guliSessionTargets) {
    $guliAbsolute=[IO.Path]::GetFullPath($guliTarget)
    $guliApprovedRoot=$guliSessionRoots | Where-Object {
        $guliAbsolute.StartsWith($_+'\',[StringComparison]::OrdinalIgnoreCase)
    } | Select-Object -First 1
    if (-not $guliApprovedRoot) { throw "Target escaped the named roots: $guliAbsolute" }
    # Check the target and each ancestor, so a replaced parent junction cannot
    # redirect an otherwise correct literal path into a different directory.
    $guliAncestor=$guliAbsolute
    while ($guliAncestor) {
        if (Test-Path -LiteralPath $guliAncestor) {
            $guliItem=Get-Item -LiteralPath $guliAncestor -Force
            if ($guliItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Refusing reparse target or ancestor: $guliAncestor"
            }
        }
        if ($guliAncestor.Equals($guliApprovedRoot,[StringComparison]::OrdinalIgnoreCase)) { break }
        $guliAncestor=Split-Path $guliAncestor -Parent
    }
    [pscustomobject]@{path=$guliAbsolute;exists=(Test-Path -LiteralPath $guliAbsolute)}
})
if (-not $Apply) {
    [pscustomobject]@{mode='preview';paths=$guliSessionPlan;apply_option='-Apply'} | ConvertTo-Json -Depth 4
    return
}
$guliSessionRunning=@(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('UnrealEditor.exe','UnrealEditor-Cmd.exe','GuLiCoordinator.exe') -and $_.CommandLine -and
    ($_.CommandLine.Replace('\','/').Contains($guliSessionProject.Replace('\','/')) -or
     $_.CommandLine.Contains('GuLiStrike.uproject'))
})
if ($guliSessionRunning.Count) { throw 'Save and close this project editor, clients and Coordinator before applying.' }
$guliSessionRemoved=@(foreach ($guliEntry in $guliSessionPlan) {
    if (-not $guliEntry.exists) { continue }
    if ($PSCmdlet.ShouldProcess($guliEntry.path,'Remove named session temporary files')) {
        Remove-Item -LiteralPath $guliEntry.path -Recurse -Force
        if (Test-Path -LiteralPath $guliEntry.path) { throw "Target remains: $($guliEntry.path)" }
        $guliEntry.path
    }
})
[pscustomobject]@{
    mode='applied';removed=$guliSessionRemoved
    remaining=@($guliSessionPlan | Where-Object { Test-Path -LiteralPath $_.path } | ForEach-Object path)
    preserved=@('Source','Config','Content','Progress','Binaries','Coordinator Release',
        'reusable tools','unrelated card artwork','newer capture directories')
} | ConvertTo-Json -Depth 4
