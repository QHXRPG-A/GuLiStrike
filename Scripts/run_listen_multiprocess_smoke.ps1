param(
    [ValidateSet("Normal", "Weak", "Reorder")]
    [string]$Profile = "Normal",
    [int]$Port = 7791,
    [ValidatePattern('^/Game/[A-Za-z0-9_./-]+$')]
    [string]$Map = "/Game/Maps/LVL_ShipTest",
    [ValidateRange(10, 360)]
    [int]$ServerDurationSeconds = 60,
    [ValidateRange(10, 360)]
    [int]$ClientDurationSeconds = 40,
    [ValidateRange(15, 240)]
    [int]$MaxFps = 60,
    [switch]$RpcDebug,
    [switch]$MoveShip,
    [switch]$PersistentTargets,
    [switch]$ActorLongGate,
    [string]$EditorExecutable = "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Win64-DebugGame.exe"
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot "GuLiStrike.uproject"
$editorPath = $EditorExecutable
$evidenceRoot = Join-Path $projectRoot "TestResults\WingmanPlan\ListenMultiprocessSmoke"
$runId = "{0}-{1}" -f (Get-Date -Format "yyyyMMdd-HHmmss"), $Profile.ToLowerInvariant()
$runRoot = Join-Path (Join-Path $evidenceRoot "runs") $runId
$serverLog = Join-Path $runRoot "server.log"
$clientLog = Join-Path $runRoot "client.log"
$serverJsonl = Join-Path $runRoot "server.samples.jsonl"
$clientJsonl = Join-Path $runRoot "client.samples.jsonl"
$manifestPath = Join-Path $runRoot "run_manifest.json"
$summaryPath = Join-Path $runRoot "index.json"
$latestPath = Join-Path $evidenceRoot "index.json"

function ConvertTo-CommandLineArgument([string]$Value) {
    if ($Value -notmatch '[\s"]') {
        return $Value
    }
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Get-ConflictingProcesses {
    $processes = Get-CimInstance Win32_Process
    return @($processes | Where-Object {
        $_.ProcessId -ne $PID -and (
            $_.Name -match '^UnrealEditor.*\.exe$' -or
            $_.Name -match '^GuLiStrike.*\.exe$' -or
            $_.Name -eq 'UnrealBuildTool.exe' -or
            ($_.Name -eq 'dotnet.exe' -and $_.CommandLine -match 'UnrealBuildTool')
        )
    } | Select-Object ProcessId, Name, CreationDate, CommandLine)
}

function Test-PortInUse([int]$RequestedPort) {
    $command = Get-Command Get-NetUDPEndpoint -ErrorAction SilentlyContinue
    if (-not $command) {
        return $false
    }
    return [bool](Get-NetUDPEndpoint -LocalPort $RequestedPort -ErrorAction SilentlyContinue)
}

function Start-SmokeProcess(
    [string]$Executable,
    [string[]]$Arguments
) {
    $argumentLine = ($Arguments | ForEach-Object { ConvertTo-CommandLineArgument $_ }) -join ' '
    return Start-Process -FilePath $Executable -ArgumentList $argumentLine -WindowStyle Hidden -PassThru
}

function Wait-ForListenSocket(
    [System.Diagnostics.Process]$Process,
    [int]$RequestedPort,
    [string]$LogPath,
    [int]$TimeoutSeconds
) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) {
            return $false
        }
        if (Get-Command Get-NetUDPEndpoint -ErrorAction SilentlyContinue) {
            $socket = Get-NetUDPEndpoint -LocalPort $RequestedPort -ErrorAction SilentlyContinue |
                Where-Object { $_.OwningProcess -eq $Process.Id } |
                Select-Object -First 1
            if ($socket) {
                return $true
            }
        }
        if ((Test-Path -LiteralPath $LogPath) -and
            (Select-String -LiteralPath $LogPath -Pattern 'IpNetDriver.*listening|GameNetDriver.*listen' -Quiet)) {
            return $true
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Wait-ForUdpSocketPair(
    [System.Diagnostics.Process]$ServerProcess,
    [System.Diagnostics.Process]$ClientProcess,
    [int]$RequestedPort,
    [int]$TimeoutSeconds
) {
    if (-not (Get-Command Get-NetUDPEndpoint -ErrorAction SilentlyContinue)) {
        return $null
    }
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $ServerProcess.Refresh()
        $ClientProcess.Refresh()
        if ($ServerProcess.HasExited -or $ClientProcess.HasExited) {
            return $null
        }
        $serverSocket = Get-NetUDPEndpoint -LocalPort $RequestedPort -ErrorAction SilentlyContinue |
            Where-Object { $_.OwningProcess -eq $ServerProcess.Id } |
            Select-Object -First 1
        if ($serverSocket) {
            $clientSocket = Get-NetUDPEndpoint -ErrorAction SilentlyContinue |
                Where-Object { $_.OwningProcess -eq $ClientProcess.Id } |
                Select-Object -First 1
            if ($clientSocket) {
                return [ordered]@{
                    protocol = 'UDP'
                    server_pid = $ServerProcess.Id
                    client_pid = $ClientProcess.Id
                    server_local_address = [string]$serverSocket.LocalAddress
                    server_local_port = [int]$serverSocket.LocalPort
                    client_local_address = [string]$clientSocket.LocalAddress
                    client_local_port = [int]$clientSocket.LocalPort
                    note = 'UDP has no OS-level established peer state; both PID-owned endpoints are paired with UE NetConnection-open telemetry.'
                }
            }
        }
        Start-Sleep -Milliseconds 250
    }
    return $null
}

function Wait-ForOwnedProcesses(
    [System.Diagnostics.Process[]]$Processes,
    [int]$TimeoutSeconds
) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $allExited = $true
        foreach ($process in $Processes) {
            $process.Refresh()
            if (-not $process.HasExited) {
                $allExited = $false
            }
        }
        if ($allExited) {
            return $true
        }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Stop-OwnedProcessPrecisely([System.Diagnostics.Process]$Process) {
    $result = [ordered]@{
        pid = $Process.Id
        close_requested = $false
        forced = $false
    }
    $Process.Refresh()
    if ($Process.HasExited) {
        return $result
    }
    $result.close_requested = $Process.CloseMainWindow()
    if ($result.close_requested) {
        try {
            if ($Process.WaitForExit(10000)) {
                return $result
            }
        } catch {
        }
    }
    $Process.Refresh()
    if (-not $Process.HasExited) {
        Stop-Process -Id $Process.Id -Force
        $result.forced = $true
    }
    return $result
}

function Export-SmokeSamples([string]$LogPath, [string]$JsonlPath) {
    $marker = '[GULI_LISTEN_SMOKE] '
    $jsonLines = [System.Collections.Generic.List[string]]::new()
    $objects = [System.Collections.Generic.List[object]]::new()
    if (Test-Path -LiteralPath $LogPath) {
        foreach ($line in [System.IO.File]::ReadLines($LogPath)) {
            $index = $line.IndexOf($marker, [StringComparison]::Ordinal)
            if ($index -lt 0) {
                continue
            }
            $json = $line.Substring($index + $marker.Length).Trim()
            try {
                $parsed = $json | ConvertFrom-Json
                $jsonLines.Add($json)
                $objects.Add($parsed)
            } catch {
            }
        }
    }
    [System.IO.File]::WriteAllLines($JsonlPath, $jsonLines)
    return @($objects)
}

function Test-AnySample([object[]]$Samples, [scriptblock]$Predicate) {
    foreach ($sample in $Samples) {
        if (& $Predicate $sample) {
            return $true
        }
    }
    return $false
}

function Test-ConsecutiveSamples(
    [object[]]$Samples,
    [int]$RequiredCount,
    [scriptblock]$Predicate
) {
    $consecutive = 0
    foreach ($sample in $Samples) {
        if (& $Predicate $sample) {
            ++$consecutive
            if ($consecutive -ge $RequiredCount) {
                return $true
            }
        } else {
            $consecutive = 0
        }
    }
    return $false
}

function Get-MaxArrayProperty(
    [object[]]$Samples,
    [string]$PropertyName,
    [int]$Length
) {
    [UInt64[]]$maximum = New-Object 'UInt64[]' $Length
    foreach ($sample in $Samples) {
        $property = $sample.PSObject.Properties[$PropertyName]
        if (-not $property) {
            continue
        }
        $values = @($property.Value)
        for ($index = 0; $index -lt $Length -and $index -lt $values.Count; ++$index) {
            $value = [UInt64]$values[$index]
            if ($value -gt $maximum[$index]) {
                $maximum[$index] = $value
            }
        }
    }
    return @($maximum)
}

if (-not (Test-Path -LiteralPath $editorPath)) {
    throw "Editor executable is missing: $editorPath"
}
if (-not (Test-Path -LiteralPath $projectPath)) {
    throw "Project is missing: $projectPath"
}
$conflicts = Get-ConflictingProcesses
if ($conflicts.Count -gt 0) {
    throw "Refusing to start while Unreal/UBT processes exist: $($conflicts | ConvertTo-Json -Compress)"
}
if (Test-PortInUse $Port) {
    throw "Refusing to use occupied UDP port $Port"
}
if ($ActorLongGate -and ($ServerDurationSeconds -lt 300 -or $ClientDurationSeconds -lt 300)) {
    throw "ActorLongGate requires both process durations to be at least 300 seconds"
}

New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$startedUtc = [DateTime]::UtcNow
$packetArguments = @()
if ($Profile -eq "Weak") {
    # Packet simulation is installed on both endpoints. Use 250 ms per direction so the
    # resulting round trip is the required approximately 500 ms, rather than accidentally
    # doubling the acceptance profile to approximately one second.
    # UE explicitly forbids PktLag together with PktOrder, so this profile isolates lag/loss.
    $packetArguments = @('-PktLag=250', '-PktLoss=5')
} elseif ($Profile -eq "Reorder") {
    # A separate run exercises reorder/loss without silently disabling PktLag.
    $packetArguments = @('-PktOrder=1', '-PktLoss=5')
}
$execCommands = "t.MaxFPS $MaxFps"
if ($RpcDebug) {
    $execCommands += ",net.RPC.Debug 1"
}
$commonArguments = @(
    $projectPath,
    '-game',
    '-Multiprocess',
    '-unattended',
    '-NoSplash',
    '-NoSound',
    '-NullRHI',
    '-NoVSync',
    "-ExecCmds=$execCommands",
    '-RenderOffScreen',
    '-stdout',
    '-FullStdOutLogOutput',
    '-GuLiListenSmoke'
) + $packetArguments
if ($RpcDebug) {
    $commonArguments += '-GuLiBootstrapTrace'
}
if ($MoveShip -or $ActorLongGate) {
    $commonArguments += '-GuLiListenSmokeMoveShip'
}
if ($ActorLongGate -or $PersistentTargets) {
    # Preserve the combat target field for the full 300-second repeated-sortie
    # gate. This non-Shipping switch keeps Wingman damage commits/effects but does
    # not mutate target health; ordinary PIE and every non-long smoke are unchanged.
    $commonArguments += '-GuLiListenSmokePersistentTargets'
}

$serverArguments = @(
    $projectPath,
    "${Map}?listen?Port=$Port"
) + $commonArguments[1..($commonArguments.Count - 1)] + @(
    '-GuLiListenSmokeRole=Server',
    "-GuLiListenSmokeRunId=$runId",
    "-GuLiListenSmokeSeconds=$ServerDurationSeconds",
    "-abslog=$serverLog"
)
$clientArguments = @(
    $projectPath,
    "127.0.0.1:$Port"
) + $commonArguments[1..($commonArguments.Count - 1)] + @(
    '-GuLiListenSmokeRole=Client',
    "-GuLiListenSmokeRunId=$runId",
    "-GuLiListenSmokeSeconds=$ClientDurationSeconds",
    "-abslog=$clientLog"
)

$server = $null
$client = $null
$serverCleanup = $null
$clientCleanup = $null
$listenSocketObserved = $false
$udpSocketPair = $null
$runError = $null
try {
    $server = Start-SmokeProcess $editorPath $serverArguments
    $listenSocketObserved = Wait-ForListenSocket $server $Port $serverLog 40
    if (-not $listenSocketObserved) {
        throw "Listen process did not open UDP port $Port within 40 seconds"
    }

    $client = Start-SmokeProcess $editorPath $clientArguments
    $udpSocketPair = Wait-ForUdpSocketPair $server $client $Port 30
    $manifest = [ordered]@{
        schema = 'guli.listen-smoke-manifest.v1'
        run_id = $runId
        profile = $Profile
        map = $Map
        move_ship = [bool]($MoveShip -or $ActorLongGate)
        actor_long_gate = [bool]$ActorLongGate
        persistent_targets = [bool]($ActorLongGate -or $PersistentTargets)
        port = $Port
        started_utc = $startedUtc.ToString('o')
        editor = $editorPath
        project = $projectPath
        server_pid = $server.Id
        client_pid = $client.Id
        server_arguments = $serverArguments
        client_arguments = $clientArguments
        listen_socket_observed_before_client_launch = $listenSocketObserved
        udp_socket_pair = $udpSocketPair
    }
    $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8

    [void](Wait-ForOwnedProcesses @($server, $client) ($ServerDurationSeconds + 60))
} catch {
    $runError = $_.Exception.Message
} finally {
    if ($client) {
        $clientCleanup = Stop-OwnedProcessPrecisely $client
    }
    if ($server) {
        $serverCleanup = Stop-OwnedProcessPrecisely $server
    }
}

$serverHasExited = $false
$clientHasExited = $false
if ($server) {
    $server.Refresh()
    $serverHasExited = $server.HasExited
}
if ($client) {
    $client.Refresh()
    $clientHasExited = $client.HasExited
}
$serverSamples = @(Export-SmokeSamples $serverLog $serverJsonl)
$clientSamples = @(Export-SmokeSamples $clientLog $clientJsonl)
$packetSimulationObserved = $Profile -eq 'Normal'
if ($Profile -eq 'Weak') {
    $serverPacketProfile = Test-AnySample $serverSamples {
        param($s)
        [int]$s.packet_lag_ms -eq 250 -and [int]$s.packet_loss_percent -eq 5 -and
        [int]$s.packet_order_enabled -eq 0
    }
    $clientPacketProfile = Test-AnySample $clientSamples {
        param($s)
        [int]$s.packet_lag_ms -eq 250 -and [int]$s.packet_loss_percent -eq 5 -and
        [int]$s.packet_order_enabled -eq 0
    }
    $packetSimulationObserved = $serverPacketProfile -and $clientPacketProfile
} elseif ($Profile -eq 'Reorder') {
    $serverPacketProfile = Test-AnySample $serverSamples {
        param($s)
        [int]$s.packet_lag_ms -eq 0 -and [int]$s.packet_loss_percent -eq 5 -and
        [int]$s.packet_order_enabled -eq 1
    }
    $clientPacketProfile = Test-AnySample $clientSamples {
        param($s)
        [int]$s.packet_lag_ms -eq 0 -and [int]$s.packet_loss_percent -eq 5 -and
        [int]$s.packet_order_enabled -eq 1
    }
    $packetSimulationObserved = $serverPacketProfile -and $clientPacketProfile
}
$serverSocketConnected = Test-AnySample $serverSamples { param($s) $s.socket_connected -eq $true }
$clientSocketConnected = Test-AnySample $clientSamples { param($s) $s.socket_connected -eq $true }
$serverEndpointReported = Test-AnySample $serverSamples {
    param($s)
    [string]$s.net_driver_local_address -match ":$Port$"
}
$clientRemoteEndpointReported = Test-AnySample $clientSamples {
    param($s)
    [string]$s.client_server_remote_address -match ":$Port$"
}
$serverBootstrapReady = Test-AnySample $serverSamples {
    param($s)
    $s.all_public_bootstraps_well_formed -eq $true -and
    [int]$s.well_formed_six_scope_bootstrap_count -gt 0
}
$clientBootstrapReady = Test-AnySample $clientSamples {
    param($s)
    $s.client_bootstrap_well_formed -eq $true -and $s.client_ability_config_usable -eq $true
}
$strictFlightReady = Test-AnySample $serverSamples {
    param($s)
    [int]$s.strict_accepted_flight_mask -eq 31 -and
    [int]$s.strict_ready_relay_group_count -gt 0 -and
    [int]$s.strict_accepted_batch_count -ge 5
}
$allRelayGroupsStrictReady = Test-AnySample $serverSamples {
    param($s)
    $groupCount = [int]$s.relay_group_count
    $groupCount -ge 2 -and
    [int]$s.active_relay_group_count -eq $groupCount -and
    [int]$s.strict_ready_relay_group_count -eq $groupCount -and
    [int]$s.atomic_committed_group_count -eq $groupCount -and
    [int]$s.strict_accepted_batch_count -ge ($groupCount * 5)
}
$allRelayGroupsSustainedAndGrowing = Test-ConsecutiveSamples $serverSamples 3 {
    param($s)
    $groupCount = [int]$s.relay_group_count
    $groupCount -ge 2 -and
    [int]$s.active_relay_group_count -eq $groupCount -and
    [int]$s.strict_ready_relay_group_count -eq $groupCount -and
    [int]$s.strict_growing_relay_group_count -eq $groupCount -and
    [int]$s.atomic_committed_group_count -eq $groupCount -and
    [int]$s.minimum_accepted_frame_across_strict_groups -gt 1
}
$serverHostAtomicBuildSubmitted = Test-AnySample $serverSamples {
    param($s)
    [int]$s.client_atomic_build_submitted_count -gt 0
}
$clientAtomicBuildSubmitted = Test-AnySample $clientSamples {
    param($s)
    [int]$s.client_atomic_build_submitted_count -gt 0
}
$serverHostAtomicResultAccepted = Test-AnySample $serverSamples {
    param($s)
    [int]$s.client_atomic_result_count -gt 0 -and
    [int]$s.client_last_atomic_result_disposition -eq 0 -and
    [int]$s.client_last_atomic_reject_reason -eq 0
}
$clientAtomicResultAccepted = Test-AnySample $clientSamples {
    param($s)
    [int]$s.client_atomic_result_count -gt 0 -and
    [int]$s.client_last_atomic_result_disposition -eq 0 -and
    [int]$s.client_last_atomic_reject_reason -eq 0
}
$atomicBuildFailuresObserved = Test-AnySample (@($serverSamples) + @($clientSamples)) {
    param($s)
    [int]$s.client_atomic_build_precondition_failure_count -ne 0 -or
    [int]$s.client_atomic_build_missing_simulation_failure_count -ne 0 -or
    [int]$s.client_atomic_build_flight_candidate_failure_count -ne 0 -or
    [int]$s.client_atomic_build_empty_failure_count -ne 0 -or
    [int]$s.client_atomic_build_fragment_failure_count -ne 0
}
$invalidTransferBaselineSerializationObserved = $false
foreach ($logPath in @($serverLog, $clientLog)) {
    if ((Test-Path -LiteralPath $logPath) -and
        (Select-String -LiteralPath $logPath -Pattern 'GuLiWingmanTransferBaseline:Group.*failed' -Quiet)) {
        $invalidTransferBaselineSerializationObserved = $true
    }
}
$serverDiagnosticObservations = [ordered]@{
    atomic_build_attempt_max = ($serverSamples | Measure-Object client_atomic_build_attempt_count -Maximum).Maximum
    atomic_build_submitted_max = ($serverSamples | Measure-Object client_atomic_build_submitted_count -Maximum).Maximum
    atomic_result_count_max = ($serverSamples | Measure-Object client_atomic_result_count -Maximum).Maximum
    atomic_accepted_count_max = ($serverSamples | Measure-Object client_atomic_accepted_count -Maximum).Maximum
    atomic_reject_count_max = ($serverSamples | Measure-Object client_atomic_reject_count -Maximum).Maximum
    atomic_result_dispositions_observed = @($serverSamples |
        Where-Object { [int]$_.client_atomic_result_count -gt 0 } |
        Select-Object -ExpandProperty client_last_atomic_result_disposition -Unique)
    atomic_reject_reasons_observed = @($serverSamples |
        Where-Object { [int]$_.client_atomic_result_count -gt 0 } |
        Select-Object -ExpandProperty client_last_atomic_reject_reason -Unique)
    atomic_flight_mode_masks_observed = @($serverSamples |
        Where-Object { $_.PSObject.Properties['client_last_atomic_flight_mode_mask'] } |
        Select-Object -ExpandProperty client_last_atomic_flight_mode_mask -Unique)
    owner_eligible_tick_count_max =
        ($serverSamples | Measure-Object client_owner_eligible_tick_count -Maximum).Maximum
    owner_inactive_gate_count_max =
        ($serverSamples | Measure-Object client_owner_inactive_gate_count -Maximum).Maximum
    owner_missing_runtime_gate_count_max =
        ($serverSamples | Measure-Object client_owner_missing_runtime_gate_count -Maximum).Maximum
    normal_build_attempt_count_max =
        ($serverSamples | Measure-Object client_normal_build_attempt_count -Maximum).Maximum
    normal_build_failure_count_max =
        ($serverSamples | Measure-Object client_normal_build_failure_count -Maximum).Maximum
    normal_build_last_failed_flight_indices_observed = @($serverSamples |
        Where-Object { [int]$_.client_normal_build_failure_count -gt 0 } |
        Select-Object -ExpandProperty client_normal_build_last_failed_flight_index -Unique)
    normal_submitted_count_max =
        ($serverSamples | Measure-Object client_normal_submitted_count -Maximum).Maximum
    normal_result_count_max =
        ($serverSamples | Measure-Object client_normal_result_count -Maximum).Maximum
    normal_accepted_count_max =
        ($serverSamples | Measure-Object client_normal_accepted_count -Maximum).Maximum
    normal_reject_count_max =
        ($serverSamples | Measure-Object client_normal_reject_count -Maximum).Maximum
    emergency_rebase_request_count_max =
        ($serverSamples | Measure-Object client_emergency_rebase_request_count -Maximum).Maximum
    emergency_rebase_result_count_max =
        ($serverSamples | Measure-Object client_emergency_rebase_result_count -Maximum).Maximum
    emergency_rebase_accepted_count_max =
        ($serverSamples | Measure-Object client_emergency_rebase_accepted_count -Maximum).Maximum
    emergency_rebase_applied_count_max =
        ($serverSamples | Measure-Object client_emergency_rebase_applied_count -Maximum).Maximum
    normal_result_dispositions_observed = @($serverSamples |
        Where-Object { [int]$_.client_normal_result_count -gt 0 } |
        Select-Object -ExpandProperty client_last_normal_result_disposition -Unique)
    normal_reject_reasons_observed = @($serverSamples |
        Where-Object { [int]$_.client_normal_result_count -gt 0 } |
        Select-Object -ExpandProperty client_last_normal_reject_reason -Unique)
    normal_flight_mode_masks_observed = @($serverSamples |
        Where-Object { $_.PSObject.Properties['client_last_normal_flight_mode_mask'] } |
        Select-Object -ExpandProperty client_last_normal_flight_mode_mask -Unique)
    client_simulation_tick_max =
        ($serverSamples | Measure-Object client_simulation_tick -Maximum).Maximum
    next_frame_sequence_max_by_flight =
        @(Get-MaxArrayProperty $serverSamples 'client_next_frame_sequences' 5)
    accepted_sequence_max_by_flight =
        @(Get-MaxArrayProperty $serverSamples 'client_accepted_sequences' 5)
    owner_gate_lifecycles_observed = @($serverSamples |
        Where-Object { $_.PSObject.Properties['client_relay_lifecycle'] } |
        Select-Object -ExpandProperty client_relay_lifecycle -Unique)
    owner_gate_local_pawn_observed = [bool]($serverSamples |
        Where-Object { $_.client_local_pawn_present -eq $true } | Select-Object -First 1)
    owner_gate_simulation_subsystem_observed = [bool]($serverSamples |
        Where-Object { $_.client_simulation_subsystem_present -eq $true } | Select-Object -First 1)
    owner_gate_carrier_source_valid_observed = [bool]($serverSamples |
        Where-Object { $_.client_relay_carrier_source_valid -eq $true } | Select-Object -First 1)
    owner_gate_upload_grant_observed = [bool]($serverSamples |
        Where-Object { $_.client_upload_rate_grant_well_formed -eq $true } | Select-Object -First 1)
    owner_gate_owned_group_observed = [bool]($serverSamples |
        Where-Object { $_.client_owned_relay_group_present -eq $true } | Select-Object -First 1)
    ability_config_acknowledged_group_count_max =
        ($serverSamples | Measure-Object ability_config_acknowledged_group_count -Maximum).Maximum
    bootstrap_acknowledged_group_count_max =
        ($serverSamples | Measure-Object bootstrap_acknowledged_group_count -Maximum).Maximum
    outstanding_bootstrap_group_count_max =
        ($serverSamples | Measure-Object outstanding_bootstrap_group_count -Maximum).Maximum
    atomic_required_group_count_max =
        ($serverSamples | Measure-Object atomic_required_group_count -Maximum).Maximum
    atomic_committed_group_count_max =
        ($serverSamples | Measure-Object atomic_committed_group_count -Maximum).Maximum
    active_relay_group_count_max =
        ($serverSamples | Measure-Object active_relay_group_count -Maximum).Maximum
    strict_accepted_batch_count_max =
        ($serverSamples | Measure-Object strict_accepted_batch_count -Maximum).Maximum
    strict_growing_relay_group_count_max =
        ($serverSamples | Measure-Object strict_growing_relay_group_count -Maximum).Maximum
    minimum_accepted_frame_across_strict_groups_max =
        ($serverSamples | Measure-Object minimum_accepted_frame_across_strict_groups -Maximum).Maximum
    server_emergency_rebase_accepted_count_max =
        ($serverSamples | Measure-Object server_emergency_rebase_accepted_count -Maximum).Maximum
    ship_motion_driver_active_observed = [bool]($serverSamples |
        Where-Object { $_.ship_motion_driver_active -eq $true } | Select-Object -First 1)
    ship_max_translation_cm =
        ($serverSamples | Measure-Object ship_max_translation_cm -Maximum).Maximum
    ship_max_rotation_deg =
        ($serverSamples | Measure-Object ship_max_rotation_deg -Maximum).Maximum
    owner_pawn_count_max = ($serverSamples | Measure-Object owner_pawn_count -Maximum).Maximum
    owner_state_tree_running_count_max =
        ($serverSamples | Measure-Object owner_state_tree_running_count -Maximum).Maximum
    owner_motion_eligible_count_max =
        ($serverSamples | Measure-Object owner_motion_eligible_count -Maximum).Maximum
    owner_current_stall_count_max =
        ($serverSamples | Measure-Object owner_current_stall_count -Maximum).Maximum
    owner_motion_stall_violation_count_max =
        ($serverSamples | Measure-Object owner_motion_stall_violation_count -Maximum).Maximum
    owner_max_low_displacement_seconds_max =
        ($serverSamples | Measure-Object owner_max_low_displacement_seconds -Maximum).Maximum
    owner_members_with_two_attack_runs_max =
        ($serverSamples | Measure-Object owner_members_with_two_attack_runs -Maximum).Maximum
    owner_min_attack_runs_max =
        ($serverSamples | Measure-Object owner_min_attack_runs -Maximum).Maximum
    world_validator_invocation_count_max =
        ($serverSamples | Measure-Object world_validator_invocation_count -Maximum).Maximum
    world_validator_context_reject_count_max =
        ($serverSamples | Measure-Object world_validator_context_reject_count -Maximum).Maximum
    world_validator_invalid_radius_reject_count_max =
        ($serverSamples | Measure-Object world_validator_invalid_radius_reject_count -Maximum).Maximum
    world_validator_missing_flight_nav_reject_count_max =
        ($serverSamples | Measure-Object world_validator_missing_flight_nav_reject_count -Maximum).Maximum
    world_validator_static_collision_reject_count_max =
        ($serverSamples | Measure-Object world_validator_static_collision_reject_count -Maximum).Maximum
    world_validator_tagged_dynamic_reject_count_max =
        ($serverSamples | Measure-Object world_validator_tagged_dynamic_reject_count -Maximum).Maximum
    world_validator_flight_nav_segment_reject_count_max =
        ($serverSamples | Measure-Object world_validator_flight_nav_segment_reject_count -Maximum).Maximum
    world_validator_accepted_count_max =
        ($serverSamples | Measure-Object world_validator_accepted_count -Maximum).Maximum
    world_validator_flight_nav_statuses_observed = @($serverSamples |
        Where-Object { $_.PSObject.Properties['world_validator_last_flight_nav_status'] } |
        Select-Object -ExpandProperty world_validator_last_flight_nav_status -Unique)
    world_validator_static_hit_actors_observed = @($serverSamples |
        Where-Object { $_.world_validator_last_static_hit_actor } |
        Select-Object -ExpandProperty world_validator_last_static_hit_actor -Unique)
    world_validator_static_hit_components_observed = @($serverSamples |
        Where-Object { $_.world_validator_last_static_hit_component } |
        Select-Object -ExpandProperty world_validator_last_static_hit_component -Unique)
}
$clientDiagnosticObservations = [ordered]@{
    atomic_build_attempt_max = ($clientSamples | Measure-Object client_atomic_build_attempt_count -Maximum).Maximum
    atomic_build_submitted_max = ($clientSamples | Measure-Object client_atomic_build_submitted_count -Maximum).Maximum
    atomic_result_count_max = ($clientSamples | Measure-Object client_atomic_result_count -Maximum).Maximum
    atomic_accepted_count_max = ($clientSamples | Measure-Object client_atomic_accepted_count -Maximum).Maximum
    atomic_reject_count_max = ($clientSamples | Measure-Object client_atomic_reject_count -Maximum).Maximum
    atomic_result_dispositions_observed = @($clientSamples |
        Where-Object { [int]$_.client_atomic_result_count -gt 0 } |
        Select-Object -ExpandProperty client_last_atomic_result_disposition -Unique)
    atomic_reject_reasons_observed = @($clientSamples |
        Where-Object { [int]$_.client_atomic_result_count -gt 0 } |
        Select-Object -ExpandProperty client_last_atomic_reject_reason -Unique)
    atomic_flight_mode_masks_observed = @($clientSamples |
        Where-Object { $_.PSObject.Properties['client_last_atomic_flight_mode_mask'] } |
        Select-Object -ExpandProperty client_last_atomic_flight_mode_mask -Unique)
    owner_eligible_tick_count_max =
        ($clientSamples | Measure-Object client_owner_eligible_tick_count -Maximum).Maximum
    owner_inactive_gate_count_max =
        ($clientSamples | Measure-Object client_owner_inactive_gate_count -Maximum).Maximum
    owner_missing_runtime_gate_count_max =
        ($clientSamples | Measure-Object client_owner_missing_runtime_gate_count -Maximum).Maximum
    normal_build_attempt_count_max =
        ($clientSamples | Measure-Object client_normal_build_attempt_count -Maximum).Maximum
    normal_build_failure_count_max =
        ($clientSamples | Measure-Object client_normal_build_failure_count -Maximum).Maximum
    normal_build_last_failed_flight_indices_observed = @($clientSamples |
        Where-Object { [int]$_.client_normal_build_failure_count -gt 0 } |
        Select-Object -ExpandProperty client_normal_build_last_failed_flight_index -Unique)
    normal_submitted_count_max =
        ($clientSamples | Measure-Object client_normal_submitted_count -Maximum).Maximum
    normal_result_count_max =
        ($clientSamples | Measure-Object client_normal_result_count -Maximum).Maximum
    normal_accepted_count_max =
        ($clientSamples | Measure-Object client_normal_accepted_count -Maximum).Maximum
    normal_reject_count_max =
        ($clientSamples | Measure-Object client_normal_reject_count -Maximum).Maximum
    emergency_rebase_request_count_max =
        ($clientSamples | Measure-Object client_emergency_rebase_request_count -Maximum).Maximum
    emergency_rebase_result_count_max =
        ($clientSamples | Measure-Object client_emergency_rebase_result_count -Maximum).Maximum
    emergency_rebase_accepted_count_max =
        ($clientSamples | Measure-Object client_emergency_rebase_accepted_count -Maximum).Maximum
    emergency_rebase_applied_count_max =
        ($clientSamples | Measure-Object client_emergency_rebase_applied_count -Maximum).Maximum
    normal_result_dispositions_observed = @($clientSamples |
        Where-Object { [int]$_.client_normal_result_count -gt 0 } |
        Select-Object -ExpandProperty client_last_normal_result_disposition -Unique)
    normal_reject_reasons_observed = @($clientSamples |
        Where-Object { [int]$_.client_normal_result_count -gt 0 } |
        Select-Object -ExpandProperty client_last_normal_reject_reason -Unique)
    normal_flight_mode_masks_observed = @($clientSamples |
        Where-Object { $_.PSObject.Properties['client_last_normal_flight_mode_mask'] } |
        Select-Object -ExpandProperty client_last_normal_flight_mode_mask -Unique)
    client_simulation_tick_max =
        ($clientSamples | Measure-Object client_simulation_tick -Maximum).Maximum
    next_frame_sequence_max_by_flight =
        @(Get-MaxArrayProperty $clientSamples 'client_next_frame_sequences' 5)
    accepted_sequence_max_by_flight =
        @(Get-MaxArrayProperty $clientSamples 'client_accepted_sequences' 5)
    owner_gate_lifecycles_observed = @($clientSamples |
        Where-Object { $_.PSObject.Properties['client_relay_lifecycle'] } |
        Select-Object -ExpandProperty client_relay_lifecycle -Unique)
    owner_gate_local_pawn_observed = [bool]($clientSamples |
        Where-Object { $_.client_local_pawn_present -eq $true } | Select-Object -First 1)
    owner_gate_simulation_subsystem_observed = [bool]($clientSamples |
        Where-Object { $_.client_simulation_subsystem_present -eq $true } | Select-Object -First 1)
    owner_gate_carrier_source_valid_observed = [bool]($clientSamples |
        Where-Object { $_.client_relay_carrier_source_valid -eq $true } | Select-Object -First 1)
    owner_gate_upload_grant_observed = [bool]($clientSamples |
        Where-Object { $_.client_upload_rate_grant_well_formed -eq $true } | Select-Object -First 1)
    owner_gate_owned_group_observed = [bool]($clientSamples |
        Where-Object { $_.client_owned_relay_group_present -eq $true } | Select-Object -First 1)
    atomic_build_precondition_failure_count_max =
        ($clientSamples | Measure-Object client_atomic_build_precondition_failure_count -Maximum).Maximum
    atomic_build_missing_simulation_failure_count_max =
        ($clientSamples | Measure-Object client_atomic_build_missing_simulation_failure_count -Maximum).Maximum
    atomic_build_flight_candidate_failure_count_max =
        ($clientSamples | Measure-Object client_atomic_build_flight_candidate_failure_count -Maximum).Maximum
    atomic_build_empty_failure_count_max =
        ($clientSamples | Measure-Object client_atomic_build_empty_failure_count -Maximum).Maximum
    atomic_build_fragment_failure_count_max =
        ($clientSamples | Measure-Object client_atomic_build_fragment_failure_count -Maximum).Maximum
    ship_motion_driver_active_observed = [bool]($clientSamples |
        Where-Object { $_.ship_motion_driver_active -eq $true } | Select-Object -First 1)
    ship_max_translation_cm =
        ($clientSamples | Measure-Object ship_max_translation_cm -Maximum).Maximum
    ship_max_rotation_deg =
        ($clientSamples | Measure-Object ship_max_rotation_deg -Maximum).Maximum
    owner_pawn_count_max = ($clientSamples | Measure-Object owner_pawn_count -Maximum).Maximum
    owner_state_tree_running_count_max =
        ($clientSamples | Measure-Object owner_state_tree_running_count -Maximum).Maximum
    owner_motion_eligible_count_max =
        ($clientSamples | Measure-Object owner_motion_eligible_count -Maximum).Maximum
    owner_current_stall_count_max =
        ($clientSamples | Measure-Object owner_current_stall_count -Maximum).Maximum
    owner_motion_stall_violation_count_max =
        ($clientSamples | Measure-Object owner_motion_stall_violation_count -Maximum).Maximum
    owner_max_low_displacement_seconds_max =
        ($clientSamples | Measure-Object owner_max_low_displacement_seconds -Maximum).Maximum
    owner_members_with_two_attack_runs_max =
        ($clientSamples | Measure-Object owner_members_with_two_attack_runs -Maximum).Maximum
    owner_min_attack_runs_max =
        ($clientSamples | Measure-Object owner_min_attack_runs -Maximum).Maximum
}
$serverMovementSamples = @($serverSamples | Where-Object {
    $_.phase -eq 'sample' -or $_.phase -eq 'final'
})
$zeroServerMovementWrites = $serverMovementSamples.Count -gt 0 -and
    -not (Test-AnySample $serverMovementSamples {
        param($s)
        -not $s.PSObject.Properties['server_wingman_movement_write_count'] -or
            [UInt64]$s.server_wingman_movement_write_count -ne 0
    })
$clientOwnerPawnReady = Test-AnySample $clientSamples {
    param($s)
    [int]$s.owner_pawn_count -ge 25
}
$serverRoleReady = Test-AnySample $serverSamples { param($s) $s.role_ready -eq $true }
$clientRoleReady = Test-AnySample $clientSamples { param($s) $s.role_ready -eq $true }
$serverFinalPresent = Test-AnySample $serverSamples { param($s) $s.phase -eq 'final' }
$clientFinalPresent = Test-AnySample $clientSamples { param($s) $s.phase -eq 'final' }
$normalExit = $serverHasExited -and $clientHasExited -and
    $serverCleanup -and $clientCleanup -and
    -not $serverCleanup.forced -and -not $clientCleanup.forced -and
    $serverFinalPresent -and $clientFinalPresent

# Historical readiness alone can hide a late recovery failure.  Require both endpoints to
# remain healthy and make forward progress at the end of the real connected interval.
$clientTimelineSamples = @($clientSamples | Where-Object {
    ($_.phase -eq 'sample' -or $_.phase -eq 'final') -and
		$_.socket_connected -eq $true -and
		$_.PSObject.Properties['client_relay_lifecycle']
})
$clientTail = @($clientTimelineSamples | Select-Object -Last 3)
$clientTailHealthy = $clientTail.Count -eq 3 -and
	-not (Test-AnySample $clientTail {
        param($s)
		[int]$s.client_relay_lifecycle -ne 2 -or
			$s.client_relay_owner_matches_local -ne $true -or
			$s.client_owned_relay_group_present -ne $true -or
			[int]$s.owner_pawn_count -lt 25
    })
$clientTailProgressing = $clientTail.Count -eq 3 -and
    [UInt64]$clientTail[-1].client_normal_accepted_count -gt
        [UInt64]$clientTail[0].client_normal_accepted_count
if ($clientTailProgressing) {
    $firstAccepted = @($clientTail[0].client_accepted_sequences)
    $lastAccepted = @($clientTail[-1].client_accepted_sequences)
    $clientTailProgressing = $firstAccepted.Count -eq 5 -and $lastAccepted.Count -eq 5
    for ($flightIndex = 0; $clientTailProgressing -and $flightIndex -lt 5; ++$flightIndex) {
        $clientTailProgressing = [UInt64]$lastAccepted[$flightIndex] -gt
            [UInt64]$firstAccepted[$flightIndex]
    }
}

$positiveClientRosterSamples = @($clientTimelineSamples | Where-Object {
    [int]$_.client_relay_roster_revision -gt 0
})
$initialClientRosterRevision = if ($positiveClientRosterSamples.Count -gt 0) {
    [int]$positiveClientRosterSamples[0].client_relay_roster_revision
} else { 0 }
$maximumClientRosterRevision = if ($positiveClientRosterSamples.Count -gt 0) {
    [int](($positiveClientRosterSamples |
        Measure-Object client_relay_roster_revision -Maximum).Maximum)
} else { 0 }
$requiredWeakRosterChurnObserved = $Profile -ne 'Weak' -or
    ($initialClientRosterRevision -gt 0 -and
        $maximumClientRosterRevision -gt $initialClientRosterRevision)

$serverConnectedTail = @($serverSamples | Where-Object {
    ($_.phase -eq 'sample' -or $_.phase -eq 'final') -and $_.socket_connected -eq $true
} | Select-Object -Last 3)
$serverConnectedTailHealthy = $serverConnectedTail.Count -eq 3 -and
    -not (Test-AnySample $serverConnectedTail {
        param($s)
        $groupCount = [int]$s.relay_group_count
        $groupCount -lt 2 -or
            [int]$s.active_relay_group_count -ne $groupCount -or
            [int]$s.ability_config_acknowledged_group_count -ne $groupCount -or
            [int]$s.atomic_committed_group_count -ne $groupCount -or
            [int]$s.strict_ready_relay_group_count -ne $groupCount -or
            [int]$s.strict_growing_relay_group_count -ne $groupCount
	})
$serverConnectedTailProgressing = $serverConnectedTail.Count -eq 3 -and
    [UInt64]$serverConnectedTail[-1].minimum_accepted_frame_across_strict_groups -gt
        [UInt64]$serverConnectedTail[0].minimum_accepted_frame_across_strict_groups

function Get-LatestRuntimeSegment {
    param([object[]]$Samples)
    $lastStartIndex = -1
    for ($sampleIndex = 0; $sampleIndex -lt $Samples.Count; ++$sampleIndex) {
        if ($Samples[$sampleIndex].phase -eq 'start') {
            $lastStartIndex = $sampleIndex
        }
    }
    if ($lastStartIndex + 1 -ge $Samples.Count) {
        return @()
    }
    return @($Samples[($lastStartIndex + 1)..($Samples.Count - 1)] | Where-Object {
        $_.phase -eq 'sample' -or $_.phase -eq 'final'
    })
}
$serverRuntimeSamples = @(Get-LatestRuntimeSegment $serverSamples)
$clientRuntimeSamples = @(Get-LatestRuntimeSegment $clientSamples)
$serverFirstActorReady = @($serverRuntimeSamples | Where-Object {
    $_.socket_connected -eq $true -and
        [int]$_.owner_pawn_count -eq 25 -and
        [int]$_.owner_motion_eligible_count -eq 25 -and
        [int]$_.owner_state_tree_running_count -eq 25
} | Select-Object -First 1)
$clientFirstActorReady = @($clientRuntimeSamples | Where-Object {
    $_.socket_connected -eq $true -and
        [int]$_.owner_pawn_count -eq 25 -and
        [int]$_.owner_motion_eligible_count -eq 25 -and
        [int]$_.owner_state_tree_running_count -eq 25
} | Select-Object -First 1)
$serverActorContinuity = $serverFirstActorReady.Count -eq 1
if ($serverActorContinuity) {
	$serverActorInterval = @($serverRuntimeSamples | Where-Object {
		[double]$_.elapsed_seconds -ge [double]$serverFirstActorReady[0].elapsed_seconds
	})
	$serverActorContinuity = $serverActorInterval.Count -ge 3 -and
		-not (Test-AnySample $serverActorInterval {
			param($s)
			[int]$s.owner_pawn_count -ne 25 -or
				[int]$s.owner_state_tree_running_count -ne
					[int]$s.owner_motion_eligible_count
		})
}
$clientActorContinuity = $clientFirstActorReady.Count -eq 1
if ($clientActorContinuity) {
	$clientActorInterval = @($clientRuntimeSamples | Where-Object {
		[double]$_.elapsed_seconds -ge [double]$clientFirstActorReady[0].elapsed_seconds
	})
	$clientActorContinuity = $clientActorInterval.Count -ge 3 -and
		-not (Test-AnySample $clientActorInterval {
			param($s)
			[int]$s.owner_pawn_count -ne 25 -or
				[int]$s.owner_state_tree_running_count -ne
					[int]$s.owner_motion_eligible_count
		})
}
$motionStallFree = -not (Test-AnySample (@($serverRuntimeSamples) + @($clientRuntimeSamples)) {
    param($s)
    [UInt64]$s.owner_motion_stall_violation_count -ne 0 -or
        [int]$s.owner_current_stall_count -ne 0
})
$candidateRejectsZero = -not (Test-AnySample (@($serverRuntimeSamples) + @($clientRuntimeSamples)) {
    param($s)
    [UInt64]$s.client_normal_reject_count -ne 0 -or
        [UInt64]$s.client_atomic_reject_count -ne 0
})
$serverHostTwoAttackRounds = Test-AnySample $serverRuntimeSamples {
    param($s)
    [int]$s.owner_members_with_two_attack_runs -eq 25 -and
        [UInt64]$s.owner_min_attack_runs -ge 2
}
$clientTwoAttackRounds = Test-AnySample $clientRuntimeSamples {
    param($s)
    [int]$s.owner_members_with_two_attack_runs -eq 25 -and
        [UInt64]$s.owner_min_attack_runs -ge 2
}
$serverMaximumElapsedSeconds =
    [double](($serverRuntimeSamples | Measure-Object elapsed_seconds -Maximum).Maximum)
$clientMaximumElapsedSeconds =
    [double](($clientRuntimeSamples | Measure-Object elapsed_seconds -Maximum).Maximum)
$actorLongDurationReached = $serverMaximumElapsedSeconds -ge 299.0 -and
    $clientMaximumElapsedSeconds -ge 299.0
$serverEmergencyRebaseAcceptedCount =
    [UInt64](($serverRuntimeSamples | Measure-Object server_emergency_rebase_accepted_count -Maximum).Maximum)
$serverHostEmergencyRebaseAcceptedCount =
    [UInt64](($serverRuntimeSamples | Measure-Object client_emergency_rebase_accepted_count -Maximum).Maximum)
$serverHostEmergencyRebaseAppliedCount =
    [UInt64](($serverRuntimeSamples | Measure-Object client_emergency_rebase_applied_count -Maximum).Maximum)
$clientEmergencyRebaseAcceptedCount =
    [UInt64](($clientRuntimeSamples | Measure-Object client_emergency_rebase_accepted_count -Maximum).Maximum)
$clientEmergencyRebaseAppliedCount =
    [UInt64](($clientRuntimeSamples | Measure-Object client_emergency_rebase_applied_count -Maximum).Maximum)
$rebaseAuthorizationConsistent =
    $serverHostEmergencyRebaseAcceptedCount -eq $serverHostEmergencyRebaseAppliedCount -and
    $clientEmergencyRebaseAcceptedCount -eq $clientEmergencyRebaseAppliedCount -and
    $serverEmergencyRebaseAcceptedCount -ge
        ($serverHostEmergencyRebaseAppliedCount + $clientEmergencyRebaseAppliedCount)
$serverShipMotionDriverActive = Test-AnySample $serverRuntimeSamples {
    param($s)
    $s.ship_motion_driver_active -eq $true
}
$clientShipMotionDriverActive = Test-AnySample $clientRuntimeSamples {
    param($s)
    $s.ship_motion_driver_active -eq $true
}
$serverShipMaximumTranslationCentimeters =
    [double](($serverRuntimeSamples | Measure-Object ship_max_translation_cm -Maximum).Maximum)
$clientShipMaximumTranslationCentimeters =
    [double](($clientRuntimeSamples | Measure-Object ship_max_translation_cm -Maximum).Maximum)
$serverShipMaximumRotationDegrees =
    [double](($serverRuntimeSamples | Measure-Object ship_max_rotation_deg -Maximum).Maximum)
$clientShipMaximumRotationDegrees =
    [double](($clientRuntimeSamples | Measure-Object ship_max_rotation_deg -Maximum).Maximum)
$shipMotionGatePassed = -not $ActorLongGate -or
    ($serverShipMotionDriverActive -and $clientShipMotionDriverActive -and
        $serverShipMaximumTranslationCentimeters -ge 10000.0 -and
        $clientShipMaximumTranslationCentimeters -ge 10000.0 -and
        $serverShipMaximumRotationDegrees -ge 30.0 -and
        $clientShipMaximumRotationDegrees -ge 30.0)
$actorLongGatePassed = -not $ActorLongGate -or
    ($actorLongDurationReached -and $serverActorContinuity -and $clientActorContinuity -and
        $motionStallFree -and $candidateRejectsZero -and
        $serverHostTwoAttackRounds -and $clientTwoAttackRounds -and
        $rebaseAuthorizationConsistent -and $shipMotionGatePassed)
$passed = -not $runError -and $listenSocketObserved -and $udpSocketPair -and
    $packetSimulationObserved -and
    $serverSocketConnected -and $clientSocketConnected -and
    $serverBootstrapReady -and $clientBootstrapReady -and $strictFlightReady -and
    $allRelayGroupsStrictReady -and $allRelayGroupsSustainedAndGrowing -and
    $serverHostAtomicBuildSubmitted -and $clientAtomicBuildSubmitted -and
    $serverHostAtomicResultAccepted -and $clientAtomicResultAccepted -and
    -not $atomicBuildFailuresObserved -and -not $invalidTransferBaselineSerializationObserved -and
    $zeroServerMovementWrites -and $clientOwnerPawnReady -and $serverRoleReady -and
    $clientRoleReady -and $clientTailHealthy -and $clientTailProgressing -and
    $requiredWeakRosterChurnObserved -and $serverConnectedTailHealthy -and
    $serverConnectedTailProgressing -and $normalExit -and $actorLongGatePassed

$summary = [ordered]@{
    schema = 'guli.listen-smoke-result.v1'
    run_id = $runId
    scope = 'real_two_process_listen_socket_smoke'
    profile = $Profile
    map = $Map
    ship_motion_requested = [bool]($MoveShip -or $ActorLongGate)
    actor_long_gate_requested = [bool]$ActorLongGate
    persistent_targets_requested = [bool]($ActorLongGate -or $PersistentTargets)
    port = $Port
    started_utc = $startedUtc.ToString('o')
    finished_utc = [DateTime]::UtcNow.ToString('o')
    passed = $passed
    run_error = $runError
    process = [ordered]@{
        server_pid = if ($server) { $server.Id } else { $null }
        client_pid = if ($client) { $client.Id } else { $null }
        server_exit_code = if ($serverHasExited) { $server.ExitCode } else { $null }
        client_exit_code = if ($clientHasExited) { $client.ExitCode } else { $null }
        server_cleanup = $serverCleanup
        client_cleanup = $clientCleanup
        normal_exit = $normalExit
    }
    evidence = [ordered]@{
        listen_socket_observed_before_client_launch = $listenSocketObserved
        os_udp_socket_pair = $udpSocketPair
        packet_simulation_profile_observed_in_both_logs = $packetSimulationObserved
        server_socket_connected = $serverSocketConnected
        client_socket_connected = $clientSocketConnected
        server_net_driver_endpoint_reported = $serverEndpointReported
        client_server_remote_endpoint_reported = $clientRemoteEndpointReported
        server_six_scope_bootstrap_ready = $serverBootstrapReady
        client_six_scope_bootstrap_ready = $clientBootstrapReady
        strict_per_flight_acceptance_ready = $strictFlightReady
        all_listen_and_remote_relay_groups_strict_ready = $allRelayGroupsStrictReady
        all_relay_groups_active_atomic_and_frames_growing_for_three_samples =
            $allRelayGroupsSustainedAndGrowing
        listen_host_atomic_build_submitted = $serverHostAtomicBuildSubmitted
        remote_client_atomic_build_submitted = $clientAtomicBuildSubmitted
        listen_host_atomic_result_accepted = $serverHostAtomicResultAccepted
        remote_client_atomic_result_accepted = $clientAtomicResultAccepted
        atomic_build_failures_observed = $atomicBuildFailuresObserved
        invalid_transfer_baseline_serialization_observed = $invalidTransferBaselineSerializationObserved
        server_wingman_movement_write_count_zero = $zeroServerMovementWrites
        client_owner_pawn_25_ready = $clientOwnerPawnReady
        client_tail_active_and_runtime_ready = $clientTailHealthy
        client_tail_all_flights_progressing = $clientTailProgressing
        required_weak_roster_churn_observed = $requiredWeakRosterChurnObserved
        initial_client_roster_revision = $initialClientRosterRevision
        maximum_client_roster_revision = $maximumClientRosterRevision
        server_connected_tail_all_groups_transaction_ready = $serverConnectedTailHealthy
        server_connected_tail_minimum_frame_progressing = $serverConnectedTailProgressing
        actor_long_duration_reached = $actorLongDurationReached
        listen_host_25_owner_pawns_and_state_trees_continuous = $serverActorContinuity
        remote_client_25_owner_pawns_and_state_trees_continuous = $clientActorContinuity
        owner_pawn_motion_stall_free = $motionStallFree
        normal_and_atomic_candidate_reject_counts_zero = $candidateRejectsZero
        listen_host_all_25_members_observed_two_attack_runs = $serverHostTwoAttackRounds
        remote_client_all_25_members_observed_two_attack_runs = $clientTwoAttackRounds
        emergency_rebases_have_matching_server_authorization = $rebaseAuthorizationConsistent
        moving_ship_position_and_rotation_gate_passed = $shipMotionGatePassed
        listen_host_ship_motion_driver_active = $serverShipMotionDriverActive
        remote_client_ship_motion_driver_active = $clientShipMotionDriverActive
        listen_host_ship_max_translation_cm = $serverShipMaximumTranslationCentimeters
        remote_client_ship_max_translation_cm = $clientShipMaximumTranslationCentimeters
        listen_host_ship_max_rotation_deg = $serverShipMaximumRotationDegrees
        remote_client_ship_max_rotation_deg = $clientShipMaximumRotationDegrees
        server_maximum_elapsed_seconds = $serverMaximumElapsedSeconds
        client_maximum_elapsed_seconds = $clientMaximumElapsedSeconds
        server_emergency_rebase_accepted_count = $serverEmergencyRebaseAcceptedCount
        listen_host_emergency_rebase_applied_count = $serverHostEmergencyRebaseAppliedCount
        remote_client_emergency_rebase_applied_count = $clientEmergencyRebaseAppliedCount
        actor_long_gate_passed = $actorLongGatePassed
        server_role_ready = $serverRoleReady
        client_role_ready = $clientRoleReady
        server_sample_count = $serverSamples.Count
        client_sample_count = $clientSamples.Count
    }
    diagnostic_observations = [ordered]@{
        listen_server = $serverDiagnosticObservations
        remote_client = $clientDiagnosticObservations
    }
    artifacts = [ordered]@{
        run_root = $runRoot
        manifest = $manifestPath
        server_log = $serverLog
        client_log = $clientLog
        server_jsonl = $serverJsonl
        client_jsonl = $clientJsonl
    }
    excluded_claims = @(
        'Launcher build cannot provide the required source-engine Dedicated Server process gate.',
        'This run is not the formal 10-minute 100-unit or split-4000 performance gate.',
        'Listen-host owner Pawn simulation shares the server process; zero movement here refers only to Relay authority movement writes.',
        'The Weak profile is packet emulation evidence, not Network Insights bandwidth certification.'
    )
}
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $summaryPath -Encoding utf8
$summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $latestPath -Encoding utf8

Write-Output ($summary | ConvertTo-Json -Depth 10)
if (-not $passed) {
    exit 1
}
