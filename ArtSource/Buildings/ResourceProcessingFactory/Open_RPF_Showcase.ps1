$ErrorActionPreference = 'Stop'
if (Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object {$_.CommandLine -like '*GuLiStrike.uproject*'}) {
    Write-Output '本项目 UE 已打开。请打开 /Game/GuLiStrike/Buildings/ResourceProcessingFactory/Demo/LVL_RPF_Showcase。'
    return
}
Start-Process -FilePath 'D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealEditor.exe' -ArgumentList @('"D:/UE5.7/test1/GuLiStrike.uproject"','/Game/GuLiStrike/Buildings/ResourceProcessingFactory/Demo/LVL_RPF_Showcase','-nosplash','-NoLiveCoding','-ini:Engine:[SystemSettings]:r.D3D12.AllowAsyncCompute=0')
