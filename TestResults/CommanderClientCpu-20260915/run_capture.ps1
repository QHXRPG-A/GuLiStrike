[CmdletBinding()]
param(
    [int]$Population = 500,
    [ValidateRange(0, 2)][int]$Clients = 0,
    [int]$Seconds = 40,
    [string]$Label = 'run',
    [string]$EvidenceRoot = 'TestResults/CommanderMove10Hz',
    [int]$Port = 17951,
    [switch]$PartitionCpu,
    [switch]$HeadlessClients,
    [switch]$JoinBeforePopulation,
    [switch]$CaptureTrace,
    [switch]$CaptureNetwork,
    [ValidateSet('march', 'shuttle')][string]$Pattern = 'shuttle'
)
$ErrorActionPreference = 'Stop'
$projectRoot = 'D:\UE5.7\test1'
$runDir = Join-Path $projectRoot "$EvidenceRoot/${Label}-n${Population}-c${Clients}"
if (Test-Path -LiteralPath $runDir) { throw "Evidence directory already exists: $runDir" }
New-Item -ItemType Directory -Path $runDir | Out-Null
$engineDir = 'D:\UnrealEngine-5.7\Engine\Binaries\Win64'
$projectFile = Join-Path $projectRoot 'GuLiStrike.uproject'
$map = '/Game/Maps/LVL_CommanderMassPrototype'
$processes = [System.Collections.Generic.List[object]]::new()
$samples = [System.Collections.Generic.List[object]]::new()
$serverSeconds = $Seconds + $(if ($Clients -gt 0) { 10 } else { 0 })
$common = @('-game', '-Unattended', '-NoSound', '-NoSplash', '-NoLiveCoding', '-DisablePlugins=Fab', '-NoScreenMessages')
$common += '-ini:Engine:[/Script/GuLiStrike.GuLiGroundCrowdManager]:MaxAgents=2048'
if ($CaptureNetwork) { $common += '-MoveStressNetworkProfile' }
if ($CaptureTrace) { $common += @('-MoveStressTrace', '-statnamedevents') }
$cvars = 't.MaxFPS 60,t.IdleWhenNotForeground 0,Slate.bAllowThrottling 0,r.VSync 0,r.ScreenPercentage 100,r.DynamicRes.OperationMode 0,r.GPUCsvStatsEnabled 1'
foreach ($quality in @('ViewDistance','AntiAliasing','Shadow','GlobalIllumination','Reflection','PostProcess','Texture','Effects','Foliage','Shading')) {
    $cvars += ",sg.${quality}Quality 3"
}
$serverDir = (Join-Path $runDir 'server').Replace('\', '/')
$joinOrder = if ($JoinBeforePopulation) { 'join-first' } else { 'late-join' }
$serverArgs = @($projectFile, $map) + $common + @('-server', '-NullRHI', "-port=$Port", "-abslog=$runDir\server.log",
    "-ExecCmds=`"$cvars,gs.Commander.MoveStress $Population $serverSeconds $serverDir server $Clients $Pattern $joinOrder`"")
try {
    $server = Start-Process -FilePath "$engineDir\UnrealEditor-Cmd.exe" -ArgumentList $serverArgs -WindowStyle Hidden -PassThru
    if ($PartitionCpu) { $server.ProcessorAffinity = [IntPtr]0xFF }
    $processes.Add([pscustomobject]@{ Role = 'server'; Process = $server; Arguments = $serverArgs; PreviousCpu = 0.0 })
    Write-Output "Started server PID=$($server.Id), population=$Population clients=$Clients"
    $deadline = (Get-Date).AddSeconds(180)
    $readyMarker = if ($JoinBeforePopulation) { 'MoveStress awaiting baseline clients' } else { 'MoveStress prepared mode=server' }
    while ($true) {
        $server.Refresh()
        if ($server.HasExited) { throw "Server exited before prepared; see $runDir\server.log" }
        if ((Test-Path -LiteralPath "$runDir\server.log") -and
            (Select-String -LiteralPath "$runDir\server.log" -SimpleMatch $readyMarker -Quiet)) { break }
        if ((Get-Date) -gt $deadline) { throw 'Server preparation timed out' }
        Start-Sleep -Seconds 1
    }
    for ($index = 1; $index -le $Clients; $index++) {
        $clientDir = (Join-Path $runDir "client$index").Replace('\', '/')
        $clientRendering = if ($HeadlessClients) { @('-NullRHI') } else { @('-RenderOffscreen', '-windowed', '-ResX=1920', '-ResY=1080', '-ForceRes') }
        $clientFps = if ($HeadlessClients) { 60 } else { 0 }
        $clientTracing = if ($CaptureTrace) { @('-MoveStressTrace', '-statnamedevents') } else { @() }
        $argsForClient = @($projectFile, "127.0.0.1:$Port") + $common + $clientRendering + $clientTracing + @(
            "-abslog=$runDir\client$index.log", "-ExecCmds=`"$cvars,t.MaxFPS $clientFps,gs.Commander.MoveStress $Population $Seconds $clientDir client $Clients $Pattern $joinOrder`"")
        $client = Start-Process -FilePath "$engineDir\UnrealEditor.exe" -ArgumentList $argsForClient -WindowStyle Hidden -PassThru
        if ($PartitionCpu) { $client.ProcessorAffinity = [IntPtr]$(if ($index -eq 1) { 0x00FF0F00 } else { 0xFF00F000L }) }
        $processes.Add([pscustomobject]@{ Role = "client$index"; Process = $client; Arguments = $argsForClient; PreviousCpu = 0.0 })
        Write-Output "Started client$index PID=$($client.Id)"
    }
    $started = Get-Date
    $previousSample = $started
    $captureSeen = $false
    while ($true) {
        $now = Get-Date
        $qpcSeconds = [Diagnostics.Stopwatch]::GetTimestamp() / [double][Diagnostics.Stopwatch]::Frequency
        if (!$captureSeen) {
            $serverSummary = Join-Path $runDir 'server\summary.json'
            if (Test-Path -LiteralPath $serverSummary) {
                $setupResult = Get-Content -LiteralPath $serverSummary -Raw | ConvertFrom-Json
                if ($setupResult.error) { throw "Server setup failed: $($setupResult.error)" }
            }
            $server.Refresh()
            if ($server.HasExited) { throw 'Server exited before capture; see server.log' }
            $setupLog = Get-Content -LiteralPath "$runDir\server.log" -Raw
            if ($setupLog.Contains('Attempted to send bunch exceeding max allowed size.')) {
                @{ Stage='late_join_roster_bootstrap'; MeasurementValid=$false; RequestedPopulation=$Population
                    Reason='SoldierStateReplicator exceeds maximum constructed bunch size; see server.log'
                    Stopped=$now.ToString('o') } | ConvertTo-Json | Set-Content -LiteralPath "$runDir\run-failure.json" -Encoding utf8
                throw 'Roster bootstrap exceeds network bunch limit; no valid measurement window'
            }
            $captureSeen = $setupLog.Contains('MoveStress capture started')
        }
        $elapsed = ($now - $previousSample).TotalSeconds
        $anyAlive = $false
        foreach ($entry in $processes) {
            $entry.Process.Refresh()
            if ($entry.Process.HasExited) { continue }
            $anyAlive = $true
            $cpu = $entry.Process.TotalProcessorTime.TotalSeconds
            $samples.Add([pscustomobject]@{
                Seconds = ($now - $started).TotalSeconds; QpcSeconds = $qpcSeconds; Role = $entry.Role; Pid = $entry.Process.Id
                CpuCoreEquivalents = $(if ($elapsed -gt 0 -and $entry.PreviousCpu -gt 0) { ($cpu - $entry.PreviousCpu) / $elapsed } else { 0 })
                WorkingSetMiB = $entry.Process.WorkingSet64 / 1MB; PrivateMiB = $entry.Process.PrivateMemorySize64 / 1MB
            })
            $entry.PreviousCpu = $cpu
        }
        if (!$anyAlive) { break }
        if (($now - $started).TotalSeconds -gt ($serverSeconds + 280)) { throw 'Measurement processes timed out' }
        $previousSample = $now
        Start-Sleep -Seconds 1
    }
} finally {
    $samples | Export-Csv -LiteralPath "$runDir\processes.csv" -NoTypeInformation -Encoding utf8
    foreach ($entry in $processes) {
        $entry.Process.Refresh()
        if (!$entry.Process.HasExited) { Stop-Process -Id $entry.Process.Id }
    }
    @{
        Date = (Get-Date).ToString('o'); Population = $Population; Clients = $Clients; Duration = $Seconds
        PartitionCpu = [bool]$PartitionCpu; HeadlessClients = [bool]$HeadlessClients; JoinBeforePopulation=[bool]$JoinBeforePopulation; CaptureTrace=[bool]$CaptureTrace; CaptureNetwork=[bool]$CaptureNetwork; Map = $map; Engine = $engineDir; Pattern = $Pattern
        Renderer = $(if ($HeadlessClients) { 'NullRHI clients/server, 60 FPS world cap; network/CPU capture only' } else { '1920x1080, Epic, screen percentage 100, VSync off, client uncapped/server 60 FPS cap, offscreen' })
        Processes = @($processes | ForEach-Object { @{ Role = $_.Role; Pid = $_.Process.Id; Arguments = $_.Arguments } })
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$runDir\launch.json" -Encoding utf8
}
foreach ($entry in $processes) {
    $summary = Join-Path $runDir "$($entry.Role)\summary.json"
    if (!(Test-Path -LiteralPath $summary)) { throw "Missing report: $summary" }
    $data = Get-Content -LiteralPath $summary -Raw | ConvertFrom-Json
    if ($data.error) { throw "$($entry.Role) failed: $($data.error)" }
    Write-Output "$($entry.Role): $summary"
}

