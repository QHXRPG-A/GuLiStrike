[CmdletBinding()]
param([ValidateRange(1024,65535)][int]$Port = 4321, [switch]$NoBrowser)
$ErrorActionPreference = 'Stop'
$DesignerRoot = $PSScriptRoot
$DesignerUrl = "http://127.0.0.1:$Port"
$PythonCommand = (Get-Command python -ErrorAction Stop).Source
Push-Location -LiteralPath $DesignerRoot
try {
    & $PythonCommand -c 'import numpy, PIL'
    if ($LASTEXITCODE -ne 0) {
        & $PythonCommand -m pip install -r (Join-Path $DesignerRoot 'requirements.txt')
        if ($LASTEXITCODE -ne 0) { throw 'Python依赖安装失败。' }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $DesignerRoot 'dist/index.html'))) {
        if (-not (Test-Path -LiteralPath (Join-Path $DesignerRoot 'node_modules'))) {
            & npm.cmd install --no-audit --no-fund
            if ($LASTEXITCODE -ne 0) { throw '前端依赖安装失败。' }
        }
        & npm.cmd run build
        if ($LASTEXITCODE -ne 0) { throw '前端构建失败。' }
    }
    $DesignerReady = $false
    try {
        $DesignerHealth = Invoke-RestMethod -Uri "$DesignerUrl/api/health" -TimeoutSec 2
        if ($DesignerHealth.app -ne 'guli-map-designer') { throw '端口被其他服务使用。' }
        $DesignerReady = $true
    } catch { }
    if (-not $DesignerReady) {
        $PythonWindowless = Join-Path (Split-Path -Parent $PythonCommand) 'pythonw.exe'
        if (-not (Test-Path -LiteralPath $PythonWindowless)) { $PythonWindowless = $PythonCommand }
        $DesignerProcess = Start-Process -FilePath $PythonWindowless -ArgumentList @('"' + (Join-Path $DesignerRoot 'server.py') + '"', '--port', "$Port") -WorkingDirectory $DesignerRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $DesignerRoot 'service.log') -RedirectStandardError (Join-Path $DesignerRoot 'service-error.log') -PassThru
        for ($DesignerAttempt=0; $DesignerAttempt -lt 50; $DesignerAttempt++) {
            try {
                $DesignerHealth = Invoke-RestMethod -Uri "$DesignerUrl/api/health" -TimeoutSec 1
                if ($DesignerHealth.app -eq 'guli-map-designer') { $DesignerReady=$true; break }
            } catch { }
            if ($DesignerProcess.HasExited) { break }
            Start-Sleep -Milliseconds 200
        }
        if (-not $DesignerReady) { throw "启动失败。检查 $DesignerRoot/service-error.log，或用 -Port 指定空闲端口。" }
    }
    Write-Host "GuLi 地图设计器：$DesignerUrl"
    if (-not $NoBrowser) { Start-Process $DesignerUrl }
} finally { Pop-Location }
