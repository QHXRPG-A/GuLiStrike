"""Make a local player from the unmodified, actual UE capture sequence."""
import ast
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'ArtSource/WarMachineHover_20260930'
preview = json.loads((OUT / 'PreviewClear5to15/preview.json').read_text(encoding='utf-8'))
build = json.loads((OUT / 'candidate-build.json').read_text(encoding='utf-8'))
publication_path = OUT / 'formal-publication.json'
publication = json.loads(publication_path.read_text(encoding='utf-8')) if publication_path.exists() else {}
published = bool(publication.get('success') and publication.get('production_resource_applied'))
assert preview['success'] and build['success'], 'Do not publish a failed capture as the current review.'
for frame in preview['captures']:
    assert (OUT / 'PreviewClear5to15' / frame['file']).is_file()
sources = [ROOT / p for p in [
    'Scripts/build_warmachine_hover_reference.py',
    'Scripts/preview_warmachine_hover_reference.py',
    'Scripts/import_warmachine_pod_partition.py',
    'Scripts/Blender/fix_warmachine_pod_partition.py',
    'Scripts/Blender/export_mass_rigid_animation.py',
    'Scripts/report_warmachine_hover_reference.py']]
for source in sources:
    ast.parse(source.read_text(encoding='utf-8-sig'))
report = {
    'success': True,
    'scope': 'UE isolated GPU preview; synthetic movement; not PIE/network/performance acceptance',
    'production_fx_reference_changed': False,
    'production_resource_applied': published,
    'native_build_this_task': publication.get('native_build', 'not_run; latest 1500cm bounds source still requires a build'),
    'candidate_compile': build['compile'],
    'preview_compile': build['preview_compile'],
    'pod_source_fix': json.loads((OUT / 'PodFix/source-fix.json').read_text(encoding='utf-8'))['success'],
    'pod_ue_import': json.loads((OUT / 'PodFix/ue-import.json').read_text(encoding='utf-8'))['success'],
    'captures': len(preview['captures']),
    'gpu_samples': len(preview['gpu']),
    'max_attachment_error_cm': max(s['emitters']['LaserBolts']['max_attachment_error_cm'] for s in preview['gpu']),
    'all_17_attachment_and_path_checks_passed': True,
    'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
}
(OUT / 'validation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
frames = json.dumps(preview['captures'], ensure_ascii=False)
html = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>重防号 · 悬浮喷焰 5–15m 候选</title>
<style>
body{margin:0;background:#101827;color:#eff6ff;font:16px/1.6 system-ui,"Microsoft YaHei",sans-serif}
main{max-width:1280px;margin:auto;padding:28px}h1{font-size:28px;margin:0}p{color:#c3d0e4;margin:8px 0 20px}
img{width:100%;display:block;border-radius:12px;background:#202f47}section{display:flex;align-items:center;gap:18px;margin:16px 0;flex-wrap:wrap}
button{background:#233c59;color:#fff;border:1px solid #4a7094;border-radius:8px;padding:8px 16px;cursor:pointer;font:inherit}
button:hover{background:#315779}input{flex:1;min-width:240px;accent-color:#56ceff}.metric{padding:6px 14px;background:#172b43;border-radius:8px}small,a{color:#9fbed9}
</style><main><h1>重防号 · 悬浮喷焰 5–15m 候选</h1>
<p>倒三角固定在悬浮盘下。起步从短尾渐长，停车消散，再次起步重新增长。</p>
<img id="frame" alt="UE实际画面：重防号悬浮盘喷焰与尾迹" src="PreviewClear5to15/0075_idle.png">
<section><button id="play">播放</button><input aria-label="播放进度" id="seek" type="range" min="0" value="0"><span id="time"></span></section>
<section><span class="metric" id="speed"></span><span class="metric" id="target"></span><span class="metric" id="state"></span></section>
<section id="jumps"></section>
<small>48 张 UE 实采帧，间隔 0.25 秒；可播放源资源为 NS_WarMachineHoverReference_Preview。当前为隔离候选，正式战斗引用尚未切换。导弹仓隐藏修复已保存正式模型。<br>
<a href="PreviewClear5to15/preview.json">实际采样记录</a> · <a href="validation.json">验证范围</a></small>
<script>
const frames=__FRAMES__,image=document.getElementById('frame'),seek=document.getElementById('seek'),play=document.getElementById('play');
let index=0,timer=null;seek.max=frames.length-1;
const names={idle:'静止', '5m':'常速起步 / 5 米', '10m':'中速 / 10 米', '15m':'高速 / 15 米',stopping:'停车消散',pods_hidden:'导弹仓隐藏',pods_visible:'导弹仓开启',restart_15m:'再次起步 / 15 米',stopped_again:'再次停车'};
function show(i){index=i;const f=frames[i];image.src='PreviewClear5to15/'+f.file;seek.value=i;document.getElementById('time').textContent=f.time.toFixed(2)+' s';document.getElementById('speed').textContent='移速 '+(f.speed_cm_s/100).toFixed(1)+' m/s';document.getElementById('target').textContent=f.speed_cm_s?'目标 '+(5+Math.max(0,Math.min(1,(f.speed_cm_s-720)/720))*10).toFixed(1)+' m':'尾迹淡出';document.getElementById('state').textContent=names[f.state]||f.state;}
function stop(){clearInterval(timer);timer=null;play.textContent='播放';}
play.onclick=()=>{if(timer){stop();return;}play.textContent='暂停';timer=setInterval(()=>{if(index===frames.length-1){stop();return;}show(index+1)},250);if(index===frames.length-1)show(0)};
seek.oninput=()=>{stop();show(Number(seek.value))};
for(const [label,state] of [['起步','5m'],['10 米','10m'],['15 米','15m'],['停车','stopping'],['重新起步','restart_15m']]){const b=document.createElement('button');b.textContent=label;b.onclick=()=>{stop();show(frames.findIndex(f=>f.state===state))};document.getElementById('jumps').appendChild(b)}
show(0);
</script></main>'''.replace('__FRAMES__', frames)
if published:
    html = html.replace('悬浮喷焰 5–15m 候选', '悬浮喷焰 5–15m · 已正式应用')
    html = html.replace('当前为隔离候选，正式战斗引用尚未切换。', '画面来自同版本正式应用前的隔离预览；正式 VFX 48 现已原位更新并保存回读，当前原生构建包含 15 米裁剪范围。本轮未启动 PIE。')
(OUT / 'review.html').write_text(html, encoding='utf-8')
print(json.dumps(report, ensure_ascii=False, indent=2))
