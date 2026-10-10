param([int]$Enabled=1,[string]$Label='business',[string]$Probe='OrderBusinessProbe')
$ErrorActionPreference = 'Stop'
$projectRoot = 'D:/UE5.7/test1'
$evidence = Join-Path $projectRoot "Artifacts/PerformanceOptimization20261010/Functional/$Label"
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
$probeArgs = @((Join-Path $projectRoot 'GuLiStrike.uproject'), '/Game/Maps/LVL_CommanderMassPrototype',
    '-game', '-server', '-port=17982', '-NullRHI', '-Unattended', '-NoSound', '-NoSplash', '-NoLiveCoding', '-DisablePlugins=Fab',
    "-abslog=$evidence/probe.log", "-ExecCmds=`"t.MaxFPS 60,gs.StateTree.GroupIndex $Enabled,gs.StateTree.ConditionSnapshot $Enabled,gs.StateTree.VerifyGroupIndex $Enabled,gs.StateTree.VerifyConditionSnapshot $Enabled,gs.Commander.$Probe $evidence/report.json`"")
$process = Start-Process -FilePath 'D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' -ArgumentList $probeArgs -WindowStyle Hidden -PassThru
@{pid=$process.Id; started=(Get-Date).ToString('o'); fixture=$Probe; state_tree_index_cache_shadow=$Enabled} | ConvertTo-Json | Set-Content -LiteralPath "$evidence/launch.json"
try {
    $deadline=(Get-Date).AddSeconds(360)
    while (!$process.WaitForExit(10000)) {
        if ((Get-Date) -gt $deadline) { throw 'Business probe timeout' }
        if (Test-Path -LiteralPath "$evidence/probe.log") {
            Get-Content -LiteralPath "$evidence/probe.log" -Tail 10 | Select-String 'OrderBusiness stage|MoveContinuity stage|Error|Ensure'
        }
    }
    $report=Get-Content -LiteralPath "$evidence/report.json" -Raw | ConvertFrom-Json
    if (!$report.passed) { throw "Business probe failed at stage $($report.stage): $($report.errors -join ', ')" }
    $issues=Select-String -LiteralPath "$evidence/probe.log" -Pattern 'Ensure condition|Group.*mismatch|Snapshot.*mismatch'
    if ($issues) { throw "Shadow verification failed: $issues" }
    Write-Output "Business probe passed: $(@($report.checks.PSObject.Properties).Count) checks"
} finally {
    if (!$process.HasExited) { Stop-Process -Id $process.Id }
}
