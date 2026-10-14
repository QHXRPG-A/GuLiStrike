param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [switch]$WithEOS,
    [string]$EOSSDKRoot = 'D:/UnrealEngine-5.7/Engine/Source/ThirdParty/EOSSDK/SDK',
    [string]$CMake = '',
    [string]$ToolsetVersion = '14.44.35207',
    [ValidateRange(1, 64)]
    [int]$Parallel = 4,
    [switch]$UseCurlDownloads
)

# Builds the standalone service and its shared core; never starts the service or tests.
$ErrorActionPreference = 'Stop'
$coordinatorRoot = $PSScriptRoot
$coordinatorBuild = Join-Path $coordinatorRoot 'build/windows'
$coordinatorVcpkg = Join-Path $coordinatorRoot 'build/vcpkg-source'
$coordinatorManifest = Get-Content -LiteralPath (Join-Path $coordinatorRoot 'vcpkg.json') -Raw | ConvertFrom-Json
$coordinatorBaseline = $coordinatorManifest.'builtin-baseline'
if ($coordinatorBaseline -notmatch '^[0-9a-f]{40}$') { throw 'A pinned vcpkg baseline is required.' }

function Invoke-BuildCommand([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

if (!$CMake) {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmakeCommand) { $CMake = $cmakeCommand.Source }
    else {
        $CMake = 'C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    }
}
if (!(Test-Path -LiteralPath $CMake -PathType Leaf)) { throw 'Supply -CMake with the installed cmake.exe path.' }
if ($WithEOS -and !(Test-Path -LiteralPath (Join-Path $EOSSDKRoot 'Include/eos_sdk.h') -PathType Leaf)) {
    throw 'Supply -EOSSDKRoot with an EOS SDK containing Include/eos_sdk.h, Lib and Bin.'
}

if (!(Test-Path -LiteralPath (Join-Path $coordinatorVcpkg '.git'))) {
    if (Test-Path -LiteralPath $coordinatorVcpkg) { throw 'The dependency directory exists without Git metadata; choose an empty build directory.' }
    Invoke-BuildCommand 'git' @('init', $coordinatorVcpkg)
    Invoke-BuildCommand 'git' @('-C', $coordinatorVcpkg, 'remote', 'add', 'origin', 'https://github.com/microsoft/vcpkg.git')
    Invoke-BuildCommand 'git' @('-C', $coordinatorVcpkg, 'fetch', '--depth', '1', 'origin', $coordinatorBaseline)
    Invoke-BuildCommand 'git' @('-C', $coordinatorVcpkg, 'checkout', '--detach', 'FETCH_HEAD')
}
$coordinatorHead = & git -C $coordinatorVcpkg rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $coordinatorHead -ne $coordinatorBaseline) {
    throw 'The local vcpkg checkout does not match vcpkg.json. Existing checkout was preserved.'
}
if (!(Test-Path -LiteralPath (Join-Path $coordinatorVcpkg 'vcpkg.exe') -PathType Leaf)) {
    Invoke-BuildCommand (Join-Path $coordinatorVcpkg 'bootstrap-vcpkg.bat') @('-disableMetrics')
}

$previousBuildEnvironment = @{}
foreach ($name in @('PATH', 'VCPKG_MAX_CONCURRENCY', 'VCPKG_BINARY_SOURCES', 'X_VCPKG_ASSET_SOURCES')) {
    $previousBuildEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    $env:PATH = (Split-Path -Parent $CMake) + ';' + $env:PATH
    $env:VCPKG_MAX_CONCURRENCY = [string]$Parallel
    $env:VCPKG_BINARY_SOURCES = 'clear;files,' + (Join-Path $coordinatorRoot 'build/binary-cache') + ',readwrite'
    if ($UseCurlDownloads) {
        $env:X_VCPKG_ASSET_SOURCES = 'clear;x-script,C:/Windows/System32/curl.exe --silent --show-error --location --fail --connect-timeout 10 --max-time 120 {url} --output {dst}'
    }
    $eosOption = if ($WithEOS) { 'ON' } else { 'OFF' }
    Invoke-BuildCommand $CMake @(
        '-S', $coordinatorRoot, '-B', $coordinatorBuild,
        '-G', 'Visual Studio 17 2022', '-A', 'x64', '-T', ('version=' + $ToolsetVersion),
        ('-DCMAKE_TOOLCHAIN_FILE=' + (Join-Path $coordinatorVcpkg 'scripts/buildsystems/vcpkg.cmake')),
        ('-DGULI_COORDINATOR_EOS=' + $eosOption), ('-DEOSSDK_ROOT=' + $EOSSDKRoot)
    )
    Invoke-BuildCommand $CMake @('--build', $coordinatorBuild, '--config', $Configuration, '--parallel', [string]$Parallel)
}
finally {
    foreach ($name in $previousBuildEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $previousBuildEnvironment[$name], 'Process')
    }
}
