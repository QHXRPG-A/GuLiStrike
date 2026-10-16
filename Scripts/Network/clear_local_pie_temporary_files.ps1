#requires -Version 7.0
<#
.SYNOPSIS
Reviews or removes regenerated local PIE/test files, preserving runtime binaries.
.DESCRIPTION
Run without arguments to review exact absolute paths. Close this project's editor,
then run with -Apply to remove those paths. This script never stops processes.
Formal source, configuration, content, documents and reusable tools are retained.
#>
[CmdletBinding()]
param([switch]$Apply)

$ErrorActionPreference='Stop'
$guliProjectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')).TrimEnd('\','/')
$guliProjectFile=Join-Path $guliProjectRoot 'GuLiStrike.uproject'
if (-not (Test-Path -LiteralPath $guliProjectFile -PathType Leaf)) {
    throw 'This tool must remain in GuLiStrike/Scripts/Network.'
}
$guliRelativeTargets=@(
    '.zcode','GuLiStrike.sln','Intermediate','DerivedDataCache',
    'Saved/Logs','Saved/Crashes','Saved/Screenshots','Saved/Profiling',
    'Saved/Automation','Saved/Temp','Saved/Autosaves',
    'Plugins/GuLiFlightNavigation/Intermediate',
    'Plugins/GuLiMapAuthoring/Intermediate',
    'Plugins/Tripo3DUEBridge-UE5.7-Win64/Intermediate',
    'Plugins/UnrealMCP/Intermediate','Plugins/UnrealMCPython/Intermediate',
    'Plugins/VibeUE/Intermediate',
    '.agents/skills/gulistrike-progress/scripts/__pycache__',
    'TestResults'
)
$guliTargets=@($guliRelativeTargets | ForEach-Object { Join-Path $guliProjectRoot $_ })
$guliCoordinatorBuild=Join-Path $guliProjectRoot 'Tools/GuLiCoordinator/build/windows'
if (Test-Path -LiteralPath $guliCoordinatorBuild) {
    # Inspect only this generated directory's immediate children; keep all runtime
    # executables and DLL dependencies together in Release.
    $guliTargets+=@(Get-ChildItem -LiteralPath $guliCoordinatorBuild -Force |
        Where-Object Name -ne 'Release' | ForEach-Object FullName)
}
$guliPlan=@(foreach ($guliTarget in $guliTargets) {
    $guliAbsolute=[IO.Path]::GetFullPath($guliTarget)
    if (-not $guliAbsolute.StartsWith($guliProjectRoot+'\',[StringComparison]::OrdinalIgnoreCase)) {
        throw "Target escaped the project: $guliAbsolute"
    }
    $guliExists=Test-Path -LiteralPath $guliAbsolute
    if ($guliExists) {
        $guliItem=Get-Item -LiteralPath $guliAbsolute -Force
        if ($guliItem.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing a reparse target: $guliAbsolute"
        }
    }
    [pscustomobject]@{path=$guliAbsolute;exists=$guliExists}
})
if (-not $Apply) {
    [pscustomobject]@{mode='review';paths=$guliPlan;apply_option='-Apply'} | ConvertTo-Json -Depth 4
    return
}
$guliRunning=@(Get-CimInstance Win32_Process |
    Where-Object { $_.Name -in @('UnrealEditor.exe','UnrealEditor-Cmd.exe','GuLiCoordinator.exe') -and
        $_.CommandLine -and ($_.CommandLine.Replace('\','/').Contains($guliProjectRoot.Replace('\','/')) -or
        $_.CommandLine.Contains('GuLiStrike.uproject')) })
if ($guliRunning.Count) {
    throw 'Close this project editor, logic client and Coordinator before cleanup.'
}
$guliRemoved=@(foreach ($guliEntry in $guliPlan) {
    if (-not $guliEntry.exists) { continue }
    Remove-Item -LiteralPath $guliEntry.path -Recurse -Force
    if (Test-Path -LiteralPath $guliEntry.path) { throw "Target remains: $($guliEntry.path)" }
    $guliEntry.path
})
$guliReport=[pscustomobject]@{
    date=(Get-Date -Format 'yyyy-MM-ddTHH:mm:ssK')
    mode='applied';removed=$guliRemoved
    preserved=@('Source','Config','Content','Scripts','Progress','Saved/Config',
        'Binaries','Plugins runtime Binaries','Tools/GuLiCoordinator/build/windows/Release')
}
$guliArchive=Join-Path $guliProjectRoot 'Progress/Archive/20261016-CS-P2P整合归档.md'
if (Test-Path -LiteralPath $guliArchive) {
    $guliJson=$guliReport | ConvertTo-Json -Depth 4
    Add-Content -LiteralPath $guliArchive -Encoding utf8 -Value (
        "`n`n## 手动执行清理的实际记录`n`n"+'```json'+"`n"+$guliJson+"`n"+'```'+"`n")
}
$guliReport | ConvertTo-Json -Depth 4
