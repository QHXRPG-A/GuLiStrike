[CmdletBinding()]
param(
    [ValidateSet('S8A-OpeningSpectator', 'S8B-MidCombatJoin', 'S8C-TransferPendingJoin')]
    [string]$Role = 'S8A-OpeningSpectator',

    [ValidateRange(1024, 65535)]
    [int]$Port = 7810,

    [string]$CampaignId = '',

    [string]$OutputRoot = 'D:\UE5.7\test1\TestResults\WingmanPlan\Campaigns',

    [ValidateRange(10, 7200)]
    [int]$ServerSeconds = 80,

    [ValidateRange(10, 7200)]
    [int]$OwnerSeconds = 55,

    [ValidateRange(10, 7200)]
    [int]$ObserverSeconds = 24,

    [ValidateRange(5, 120)]
    [int]$MarkerTimeoutSeconds = 55,

    [ValidateRange(0, 10000)]
    [int]$OwnerStartIntervalMilliseconds = 4500
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$clientExe = 'D:\UE5.7\test1\Saved\StagedBuilds\WingmanQAClient\Windows\GuLiStrike\Binaries\Win64\GuLiStrike.exe'
$serverExe = 'D:\UE5.7\test1\Saved\StagedBuilds\WindowsServer\GuLiStrike\Binaries\Win64\GuLiStrikeServer.exe'
$mapUrl = '/Game/Maps/LVL_CommanderMassPrototype?game=/Script/GuLiStrike.GuLiWingmanQAGameMode'
$seed = 5700903
$tracked = [System.Collections.Generic.List[object]]::new()

if ([string]::IsNullOrWhiteSpace($CampaignId)) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $CampaignId = "source-dedicated-s8-$stamp"
}
$runId = "$Role-production"
$pairId = "$Role-localhost"
$campaignRoot = Join-Path $OutputRoot $CampaignId
$runRoot = Join-Path $campaignRoot $runId
$logRoot = Join-Path $campaignRoot 'LauncherLogs'
$serverLog = Join-Path $logRoot 'server.log'
$serverEvents = Join-Path $runRoot 'server-events.jsonl'
$expectedClients = if ($Role -eq 'S8C-TransferPendingJoin') { 6 } else { 5 }

foreach ($requiredExe in @($clientExe, $serverExe)) {
    if (-not (Test-Path -LiteralPath $requiredExe -PathType Leaf)) {
        throw "Required staged executable is missing: $requiredExe"
    }
}
if (Test-Path -LiteralPath $runRoot) {
    throw "Refusing to overwrite an existing QA run: $runRoot"
}
$boundTcp = Get-NetTCPConnection -LocalPort $Port -ErrorAction SilentlyContinue
$boundUdp = Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue
if ($boundTcp -or $boundUdp) {
    throw "Port $Port is already in use."
}
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null

function New-CommonArguments {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Endpoint,

        [Parameter(Mandatory = $true)]
        [int]$DurationSeconds,

        [Parameter(Mandatory = $true)]
        [string]$LogPath
    )

    return @(
        "-abslog=$LogPath"
        "-GuLiWingmanQAEndpoint=$Endpoint"
        "-GuLiWingmanQASeconds=$DurationSeconds"
        '-nullrhi'
        '-nosound'
        '-unattended'
        '-NoSplash'
        '-NoCrashDialog'
        '-NoSteam'
        '-ExecCmds="t.MaxFPS 60"'
        '-GuLiWingmanQA'
        "-GuLiWingmanQACampaign=$CampaignId"
        "-GuLiWingmanQARunId=$runId"
        "-GuLiWingmanQARole=$Role"
        "-GuLiWingmanQAPair=$pairId"
        '-GuLiWingmanQAProfile=CoreWingman'
        "-GuLiWingmanQAOutput=$OutputRoot"
        "-GuLiWingmanQASeed=$seed"
        '-GuLiWingmanQAAutoExit'
        "-GuLiWingmanQAExpectedClients=$expectedClients"
    )
}

function Start-TrackedProcess {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [string]$Executable,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments,

        [Parameter(Mandatory = $true)]
        [string]$LogPath
    )

    $process = Start-Process -FilePath $Executable -ArgumentList $Arguments `
        -WorkingDirectory (Split-Path -Parent $Executable) -WindowStyle Hidden -PassThru
    $record = [pscustomobject]@{
        Name = $Name
        Process = $process
        Log = $LogPath
    }
    $tracked.Add($record)
    return $record
}

function Wait-ForFilePattern {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Pattern,

        [Parameter(Mandatory = $true)]
        [int]$TimeoutSeconds,

        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$WatchProcess
    )

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $WatchProcess.Refresh()
        if ($WatchProcess.HasExited) {
            throw "Watched process exited with code $($WatchProcess.ExitCode) before '$Pattern' appeared in $Path"
        }
        if (Test-Path -LiteralPath $Path -PathType Leaf) {
            if (Select-String -LiteralPath $Path -Pattern $Pattern -Quiet) {
                return
            }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out after $TimeoutSeconds seconds waiting for '$Pattern' in $Path"
}

function Start-ClientEndpoint {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Endpoint,

        [Parameter(Mandatory = $true)]
        [int]$DurationSeconds,

        [switch]$SpectatorOnly
    )

    $logPath = Join-Path $logRoot "$Endpoint.log"
    $connectUrl = "127.0.0.1:$Port"
    if ($SpectatorOnly) {
        $connectUrl += '?SpectatorOnly=1'
    }
    $arguments = @($connectUrl) + (New-CommonArguments `
        -Endpoint $Endpoint -DurationSeconds $DurationSeconds -LogPath $logPath)
    return Start-TrackedProcess -Name $Endpoint -Executable $clientExe `
        -Arguments $arguments -LogPath $logPath
}

function Wait-ForAllProcesses {
    param([int]$TimeoutSeconds)

    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $allExited = $true
        foreach ($record in $tracked) {
            $record.Process.Refresh()
            if (-not $record.Process.HasExited) {
                $allExited = $false
            }
        }
        if ($allExited) {
            return
        }
        Start-Sleep -Milliseconds 250
    }
    $liveNames = @($tracked | Where-Object { -not $_.Process.HasExited } | ForEach-Object { $_.Name })
    throw "Timed out waiting for QA processes: $($liveNames -join ', ')"
}

try {
    $serverArguments = @($mapUrl, "-port=$Port") + (New-CommonArguments `
        -Endpoint 'server' -DurationSeconds $ServerSeconds -LogPath $serverLog)
    $server = Start-TrackedProcess -Name 'server' -Executable $serverExe `
        -Arguments $serverArguments -LogPath $serverLog
    Wait-ForFilePattern -Path $serverLog -Pattern 'GameNetDriver.*listening' `
        -TimeoutSeconds $MarkerTimeoutSeconds -WatchProcess $server.Process

    1..4 | ForEach-Object {
        Start-ClientEndpoint -Endpoint "owner-$_" -DurationSeconds $OwnerSeconds | Out-Null
        if ($_ -lt 4 -and $OwnerStartIntervalMilliseconds -gt 0) {
            Start-Sleep -Milliseconds $OwnerStartIntervalMilliseconds
        }
    }

    if ($Role -eq 'S8C-TransferPendingJoin') {
        if ($OwnerStartIntervalMilliseconds -gt 0) {
            Start-Sleep -Milliseconds $OwnerStartIntervalMilliseconds
        }
        Start-ClientEndpoint -Endpoint 'transfer-candidate' -DurationSeconds $OwnerSeconds | Out-Null
    }

    $joinMarker = switch ($Role) {
        'S8A-OpeningSpectator' { '"role_ready":"true"' }
        'S8B-MidCombatJoin' { 'S8_NONLETHAL_READY_FOR_OBSERVER' }
        'S8C-TransferPendingJoin' { 'S8_TRANSFER_OFFER_READY_FOR_OBSERVER' }
    }
    Wait-ForFilePattern -Path $serverEvents -Pattern $joinMarker `
        -TimeoutSeconds $MarkerTimeoutSeconds -WatchProcess $server.Process
    Start-ClientEndpoint -Endpoint 'observer' -DurationSeconds $ObserverSeconds -SpectatorOnly | Out-Null

    Wait-ForAllProcesses -TimeoutSeconds ($ServerSeconds + 30)
    $failedProcesses = @($tracked | Where-Object { $_.Process.ExitCode -ne 0 })
    if ($failedProcesses.Count -gt 0) {
        $details = $failedProcesses | ForEach-Object {
            "$($_.Name)=$($_.Process.ExitCode) [$($_.Log)]"
        }
        throw "QA process failure: $($details -join '; ')"
    }

    $acceptancePath = Join-Path $runRoot 'acceptance.json'
    if (-not (Test-Path -LiteralPath $acceptancePath -PathType Leaf)) {
        throw "Server acceptance file is missing: $acceptancePath"
    }
    $acceptance = Get-Content -LiteralPath $acceptancePath -Raw | ConvertFrom-Json
    if (-not $acceptance.run_completed -or -not $acceptance.scenario_passed -or -not $acceptance.passed) {
        throw "Server acceptance failed: $acceptancePath"
    }

    $requiredObserverGates = switch ($Role) {
        'S8A-OpeningSpectator' { @('OPENING_SPECTATOR_ATOMIC_BOOTSTRAP') }
        'S8B-MidCombatJoin' {
            @('MID_COMBAT_JOIN_ATOMIC_BOOTSTRAP', 'LATEJOIN_DAMAGE_LEDGER_CONSISTENT')
        }
        'S8C-TransferPendingJoin' { @('TRANSFER_PENDING_JOIN_FROZEN_CUT') }
    }
    $observerAssertionsPath = Join-Path $runRoot 'endpoint-observer.assertions.json'
    if (-not (Test-Path -LiteralPath $observerAssertionsPath -PathType Leaf)) {
        throw "Observer assertions file is missing: $observerAssertionsPath"
    }
    $observerAssertions = Get-Content -LiteralPath $observerAssertionsPath -Raw | ConvertFrom-Json
    foreach ($gate in $requiredObserverGates) {
        $gateProperty = $observerAssertions.gate_status.PSObject.Properties[$gate]
        if ($null -eq $gateProperty -or -not [bool]$gateProperty.Value) {
            throw "Observer endpoint did not prove required gate '$gate': $observerAssertionsPath"
        }
    }

    [pscustomobject]@{
        CampaignId = $CampaignId
        RunId = $runId
        Role = $Role
        ExpectedClients = $expectedClients
        Port = $Port
        Passed = [bool]$acceptance.passed
        ObserverGates = $requiredObserverGates -join ','
        EvidencePath = $runRoot
        AcceptancePath = $acceptancePath
    }
}
finally {
    foreach ($record in $tracked) {
        $record.Process.Refresh()
        if (-not $record.Process.HasExited) {
            Stop-Process -Id $record.Process.Id -Force -ErrorAction SilentlyContinue
        }
        $record.Process.Dispose()
    }
}
