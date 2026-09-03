[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet(
        'Main-S0-S5',
        'S1-Control',
        'S3C-AB',
        'S3C-BA',
        'S4-Idempotency-FireProposalReplay',
        'S4-Idempotency-ProjectileOverlapReplay',
        'S4-Idempotency-Concurrent',
        'S6-CorrectionReverse',
        'S6-DeathBeforePose',
        'S6-LeaseBeforeOldProposal',
        'S6-RespawnBeforeOldPose',
        'S7-LeaseLoss',
        'S7-Graceful-FireBeforeCommit',
        'S7-Graceful-DamageAckRace',
        'S7-ActualDisconnect',
        'S9-Control',
        'S9-Feature')]
    [string]$Role,

    [ValidateRange(1024, 65535)]
    [int]$Port = 7840,

    [string]$CampaignId = '',

    [string]$OutputRoot = 'D:\UE5.7\test1\TestResults\WingmanPlan\Campaigns',

    [ValidateRange(10, 7200)]
    [int]$ServerSeconds = 30,

    [ValidateRange(10, 7200)]
    [int]$ClientSeconds = 20,

    [ValidateRange(0, 4)]
    [int]$ExpectedClients = 4,

    [ValidateRange(5, 120)]
    [int]$ReadyTimeoutSeconds = 55,

    [ValidateRange(0, 10000)]
    [int]$ClientStartIntervalMilliseconds = 0
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
    $CampaignId = "source-dedicated-core-$stamp"
}
if ($ExpectedClients -gt 0 -and $ServerSeconds -le $ClientSeconds) {
    throw 'ServerSeconds must be greater than ClientSeconds so the server can merge all endpoint evidence.'
}

$runId = "$Role-production"
$pairId = "$Role-localhost"
$campaignRoot = Join-Path $OutputRoot $CampaignId
$runRoot = Join-Path $campaignRoot $runId
$logRoot = Join-Path $campaignRoot 'LauncherLogs' $Role
$serverLog = Join-Path $logRoot 'server.log'

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

    $arguments = @(
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
        "-GuLiWingmanQAExpectedClients=$ExpectedClients"
    )
    if ($Role -eq 'S9-Control') {
        $arguments += '-GuLiWingmanQADisableWingmen'
    }
    return $arguments
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
        if ((Test-Path -LiteralPath $Path -PathType Leaf) `
            -and (Select-String -LiteralPath $Path -Pattern $Pattern -Quiet)) {
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out after $TimeoutSeconds seconds waiting for '$Pattern' in $Path"
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
        -TimeoutSeconds $ReadyTimeoutSeconds -WatchProcess $server.Process

    if ($ExpectedClients -gt 0) {
        foreach ($index in 1..$ExpectedClients) {
            $endpoint = "owner-$index"
            $logPath = Join-Path $logRoot "$endpoint.log"
            $arguments = @("127.0.0.1:$Port") + (New-CommonArguments `
                -Endpoint $endpoint -DurationSeconds $ClientSeconds -LogPath $logPath)
            Start-TrackedProcess -Name $endpoint -Executable $clientExe `
                -Arguments $arguments -LogPath $logPath | Out-Null
            if ($index -lt $ExpectedClients -and $ClientStartIntervalMilliseconds -gt 0) {
                Start-Sleep -Milliseconds $ClientStartIntervalMilliseconds
            }
        }
    }

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
    if ($acceptance.suite_run_role -ne $Role -or -not $acceptance.run_completed `
        -or -not $acceptance.scenario_passed -or -not $acceptance.passed) {
        throw "Dedicated role acceptance failed: $acceptancePath"
    }

    [pscustomobject]@{
        CampaignId = $CampaignId
        RunId = $runId
        Role = $Role
        ExpectedClients = $ExpectedClients
        Port = $Port
        Passed = [bool]$acceptance.passed
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
