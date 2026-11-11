[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)]
    [int]$Port = 4317
)

$ErrorActionPreference = 'Stop'
$DashboardRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = (Resolve-Path -LiteralPath (Join-Path $DashboardRoot '..\..')).Path
$FrontendEntry = Join-Path $DashboardRoot 'dist\client\index.html'
$ServerScript = Join-Path $DashboardRoot 'server.py'
$Url = "http://127.0.0.1:$Port"

$Ready = $null
try {
    $Ready = Invoke-RestMethod -Uri "$Url/api/ready" -TimeoutSec 2
} catch { }
if ($Ready) {
    if ($Ready.app -ne 'guli-progress-dashboard' -or $Ready.project -ne $ProjectRoot) {
        throw "端口 $Port 上运行的是其他服务，请选择空闲端口。"
    }
    Start-Process $Url
    return
}

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
        param($TargetUrl, $ExpectedProject)
        for ($Attempt = 0; $Attempt -lt 50; $Attempt++) {
            try {
                $Ready = Invoke-RestMethod -Uri "$TargetUrl/api/ready" -TimeoutSec 1
                if ($Ready.app -eq 'guli-progress-dashboard' -and $Ready.project -eq $ExpectedProject) {
                    Start-Process $TargetUrl
                    return
                }
            } catch {
                Start-Sleep -Milliseconds 200
            }
        }
    } -ArgumentList $Url,$ProjectRoot

    try {
        & python $ServerScript --port $Port
    } finally {
        Stop-Job -Job $BrowserJob -ErrorAction SilentlyContinue
        Remove-Job -Job $BrowserJob -Force -ErrorAction SilentlyContinue
    }
} finally {
    Pop-Location
}
