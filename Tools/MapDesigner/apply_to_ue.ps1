[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Directory,[switch]$Apply,[int]$Population=600)
$ErrorActionPreference='Stop'
$DesignerArgs=@('-X','utf8',(Join-Path $PSScriptRoot 'ue/bridge.py'),'--directory',$Directory,'--population',"$Population")
if($Apply){$DesignerArgs+='--apply'}
& python @DesignerArgs
if($LASTEXITCODE -ne 0){throw 'UE适配未成功，见导出目录中的结果文件。未通过预检查时不会修改地图。'}
