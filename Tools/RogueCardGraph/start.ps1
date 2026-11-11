[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)][int]$Port = 4322,
    [switch]$NoBrowser
)

$ErrorActionPreference = 'Stop'
$Url = "http://127.0.0.1:$Port"
$Project = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path

function Read-GraphService {
    try {
        Invoke-RestMethod -Uri "$Url/api/health" -TimeoutSec 1
    } catch {
        $null
    }
}

$Health = Read-GraphService
if ($Health -and ($Health.app -ne 'guli-rogue-card-graph' -or $Health.project -ne $Project)) {
    throw "端口 $Port 正在提供其他服务。请用 -Port 选一个空闲端口。"
}
if (-not $Health) {
    $Python = (Get-Command python -ErrorAction Stop).Source
    $Script = Join-Path $PSScriptRoot 'server.py'
    $ErrorLog = Join-Path $PSScriptRoot 'service-error.log'
    $StartArgs = @{
        FilePath = $Python
        ArgumentList = @('-B', '-X', 'utf8', ('"{0}"' -f $Script), '--port', $Port)
        WorkingDirectory = $PSScriptRoot
        WindowStyle = 'Hidden'
        RedirectStandardOutput = Join-Path $PSScriptRoot 'service.log'
        RedirectStandardError = $ErrorLog
        PassThru = $true
    }
    $Service = Start-Process @StartArgs
    for ($Attempt = 0; $Attempt -lt 30; $Attempt++) {
        $Health = Read-GraphService
        if ($Health.app -eq 'guli-rogue-card-graph' -or $Service.HasExited) { break }
        Start-Sleep -Milliseconds 200
    }
    if ($Health.app -ne 'guli-rogue-card-graph' -or $Health.project -ne $Project) {
        throw "工具启动失败。请查看 $ErrorLog。"
    }
}

Write-Host "肉鸽牌关系图 $Url"
if (-not $NoBrowser) { Start-Process $Url }
