$ErrorActionPreference = 'Stop'
$DesignerRoot=$PSScriptRoot
$ProjectRoot=Split-Path -Parent (Split-Path -Parent $DesignerRoot)
& python -X utf8 (Join-Path $ProjectRoot 'Scripts/commander_editor_python.py') --file (Join-Path $DesignerRoot 'ue/export_snapshot.py') --timeout 60
if ($LASTEXITCODE -ne 0) { throw '当前UE快照读取失败。请查看上方错误；不会自动重试或修改地图。' }
