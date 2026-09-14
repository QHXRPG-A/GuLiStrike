param([switch]$SkipImport)
$ErrorActionPreference = 'Stop'
$RpfRoot = 'D:/UE5.7/test1'
$RpfEditor = 'D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealEditor.exe'
$RpfOutput = Join-Path $RpfRoot 'outputs/resource-processing-factory-20260909'
if (Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object {$_.CommandLine -like '*GuLiStrike.uproject*'}) {
    throw '请先保存并关闭本项目的 UE 编辑器，再运行资产重建脚本。'
}
New-Item -ItemType Directory -Path $RpfOutput -Force | Out-Null
function Invoke-RpfEditor([string]$Script, [string]$Stage, [switch]$Render) {
    $rpfArgs = @('"D:/UE5.7/test1/GuLiStrike.uproject"','/Engine/Maps/Entry','-unattended','-nosplash','-nosound','-NoLiveCoding',('-ExecutePythonScript="'+$Script+'"'),('-abslog="'+$RpfOutput+'/'+$Stage+'.log"'))
    if ($Render) { $rpfArgs += @('-RenderOffscreen','-ini:Engine:[SystemSettings]:r.D3D12.AllowAsyncCompute=0') }
    else { $rpfArgs += '-nullrhi' }
    $rpfStarted = Get-Date
    $rpfProcess = Start-Process -FilePath $RpfEditor -ArgumentList $rpfArgs -WindowStyle Hidden -PassThru
    $rpfProcess.WaitForExit()
    if ($rpfProcess.ExitCode -ne 0) {throw "UE $Stage 退出码 $($rpfProcess.ExitCode)，请查看输出日志。"}
    foreach ($rpfError in @('ue_import_error.txt','ue_blueprint_error.txt')) {
        $rpfErrorPath = Join-Path $RpfOutput $rpfError
        if ((Test-Path -LiteralPath $rpfErrorPath) -and ((Get-Item -LiteralPath $rpfErrorPath).LastWriteTime -ge $rpfStarted)) {throw "UE $Stage 未完成：$rpfErrorPath"}
    }
}
if (-not $SkipImport) {Invoke-RpfEditor "$RpfRoot/Scripts/import_resource_processing_factory.py" 'RPF_Import'}
Invoke-RpfEditor "$RpfRoot/Scripts/build_resource_processing_factory_blueprints.py --phase assets" 'RPF_Blueprints'
Invoke-RpfEditor "$RpfRoot/Scripts/build_resource_processing_factory_blueprints.py --phase map" 'RPF_Showcase' -Render
Write-Output '资源加工厂模型、控制蓝图和演示地图已保存。'
