param(
    [string]$EngineRoot = 'D:\UnrealEngine-5.7',
    [string]$Map = '',
    [switch]$ValidateOnly,
    [string[]]$CookArguments = @()
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$ProjectFile = Join-Path $ProjectRoot 'GuLiStrike.uproject'
$EditorCommand = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$PrepareArguments = @($ProjectFile, '-run=GuLiNavigationPrepare', '-unattended', '-NoSplash', '-NoLiveCoding', '-NullRHI', '-NoSound')
if ($Map) { $PrepareArguments += "-Map=$Map" }
if ($ValidateOnly) { $PrepareArguments += '-ValidateOnly' }

& $EditorCommand @PrepareArguments
if ($LASTEXITCODE -ne 0) { throw "Navigation preparation failed ($LASTEXITCODE). Cook was not started." }

# Optional BuildCookRun arguments are forwarded only after preparation succeeds.
if ($CookArguments.Count -gt 0) {
    $AutomationTool = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
    & $AutomationTool BuildCookRun "-project=$ProjectFile" @CookArguments
    if ($LASTEXITCODE -ne 0) { throw "BuildCookRun failed ($LASTEXITCODE)." }
}
