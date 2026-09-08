[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)]
    [int]$Port = 4317
)

$ErrorActionPreference = 'Stop'
$DashboardRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$FrontendEntry = Join-Path $DashboardRoot 'dist\client\index.html'
$ServerScript = Join-Path $DashboardRoot 'server.py'
$Url = "http://127.0.0.1:$Port"

Push-Location $DashboardRoot
try {
    if (-not (Test-Path -LiteralPath $FrontendEntry)) {
        if (-not (Get-Command npm -ErrorAction SilentlyContinue)) {
            throw '未找到 npm；请安装 Node.js 22.13 或更高版本。'
        }
        if (-not (Test-Path -LiteralPath (Join-Path $DashboardRoot 'node_modules'))) {
            Write-Host '首次运行：正在安装前端依赖……' -ForegroundColor Cyan
            & npm install
            if ($LASTEXITCODE -ne 0) { throw "npm install 失败，退出码 $LASTEXITCODE" }
        }
        Write-Host '首次运行：正在构建只读前端……' -ForegroundColor Cyan
        & npm run build
        if ($LASTEXITCODE -ne 0) { throw "npm run build 失败，退出码 $LASTEXITCODE" }
    }

    if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
        throw '未找到 Python；请安装 Python 3.11 或更高版本。'
    }

    $BrowserJob = Start-Job -ScriptBlock {
        param($TargetUrl)
        for ($Attempt = 0; $Attempt -lt 50; $Attempt++) {
            try {
                $Response = Invoke-WebRequest -UseBasicParsing -Uri "$TargetUrl/api/health" -TimeoutSec 1
                if ($Response.StatusCode -eq 200) {
                    Start-Process $TargetUrl
                    return
                }
            } catch {
                Start-Sleep -Milliseconds 200
            }
        }
    } -ArgumentList $Url

    try {
        & python $ServerScript --port $Port
    } finally {
        Stop-Job -Job $BrowserJob -ErrorAction SilentlyContinue
        Remove-Job -Job $BrowserJob -Force -ErrorAction SilentlyContinue
    }
} finally {
    Pop-Location
}
