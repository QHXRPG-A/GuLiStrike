"""Build a self-contained player for the actual start/peak/stop PIE PNG sequences."""
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'outputs/performance/20261009-implementation/visual-review'
takes=json.loads((OUT/'takes.json').read_text(encoding='utf-8'))
publication=json.loads((OUT.parent/'formal-reference-proposal.json').read_text(encoding='utf-8'))
adopted=publication.get('approval')=='approved_by_user' and publication.get('applied') is True
approval_text=('视觉：用户已认可，ID36/45/5/52 已正式接入，原生目录和消费者回读通过。'
               if adopted else '视觉：待用户验收，正式战斗引用尚未切换。')
assert len(takes)==20 and all(len(x['frames'])==21 for x in takes)
assert all((OUT/f['file']).is_file() and f['success'] for x in takes for f in x['frames'])
html=r'''<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>PIE 光束与闪光对照</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#131c21;color:#e4eee9;font:16px/1.5 system-ui,"Microsoft YaHei",sans-serif}
main{max-width:1540px;margin:auto;padding:24px}h1{font-size:26px;margin:0 0 6px}p{color:#b8c9c0;margin:6px 0 16px}
button,select{font:inherit;color:inherit;background:#2c3735;border:1px solid #52625d;border-radius:7px;padding:7px 12px;cursor:pointer}
button.active{background:#456258}label{display:flex;gap:10px;align-items:center}.controls{display:flex;gap:14px;flex-wrap:wrap;align-items:center;margin:15px 0}
.players{display:grid;grid-template-columns:1fr 1fr;gap:18px}.card{background:#1a262c;border:1px solid #3b4c4c;border-radius:10px;overflow:hidden}
.card h2{font-size:17px;margin:0;padding:12px 16px}.card img{display:block;width:100%;aspect-ratio:16/9;object-fit:contain;background:#000}
.card small{display:block;padding:10px 16px;overflow-wrap:anywhere;color:#b8c9c0}input{flex:1;min-width:200px;accent-color:#7da083}.time{min-width:85px;font-variant-numeric:tabular-nums}
.notice{border-left:3px solid #827399;padding-left:12px}a{color:#a0c5bc}footer{font-size:14px;color:#aabcb6;margin-top:22px}
@media(max-width:850px){.players{grid-template-columns:1fr}main{padding:14px}}
</style><main>
<h1>采矿、建造与机枪闪光 · PIE 对照</h1>
<p>真实客户端画面，每组 4 秒、21 帧。可暂停、逐帧查看开始、持续和停止；左上角瞬时帧率不作为性能结论。</p>
<div class="controls"><label>用途 <select id="role"><option value="Mining">采矿 · ID 36</option><option value="Construction">建造 · ID 45</option><option value="Flash">机枪枪口／命中</option></select></label>
<label>左侧 <select id="left"></select></label><label>右侧 <select id="right"></select></label></div>
<div class="players"><section class="card"><h2 id="lt"></h2><img id="li" alt="左侧 PIE 实际帧"><small id="lc"></small></section>
<section class="card"><h2 id="rt"></h2><img id="ri" alt="右侧 PIE 实际帧"><small id="rc"></small></section></div>
<div class="controls"><button id="play">播放</button><button id="prev">上一帧</button><button id="next">下一帧</button>
<input id="frame" type="range" min="0" max="20" value="5" step="1" aria-label="帧位置"><span id="time" class="time"></span></div>
<div class="controls"><button data-frame="0">开始</button><button data-frame="5">持续 / 峰值</button><button data-frame="15">停止后</button></div>
<p id="contract" class="notice"></p><p id="stamp"></p>
<footer>技术：候选已编译并保存。性能结论见 <a href="../implementation-results.md">实施与采样结果</a>。
__APPROVAL__ Opaque 保留原落点火花碰撞；NoCollision 取消 Spark 碰撞，会改变端点行为。
闪光随机布局未固定，不能用单个火花的位置判断差异。
<br><a href="takes.json">原始录制时间与资源引用</a> · <a href="../final-saved-scene-readback.json">现有地图中的验证区域</a></footer>
</main><script>
const takes=__TAKES__, $=id=>document.getElementById(id);
const names={Source:'现有资源',Wide:'加宽 1.5× · 原透明材质',Opaque:'加宽 1.5× · 不透明 · 80 节点',Nodes16:'不透明 · 16 节点',Nodes8:'不透明 · 8 节点',NoCollision:'不透明 · Spark 无碰撞（改变端点）',NoSolver:'不透明 · 光束求解器候选',GPU:'不透明 · 光束 GPU 候选',Muzzle:'独立枪口副本',Impact:'独立命中副本 · 缩放 2',MuzzleNoCollision:'独立枪口 · 无碰撞候选'};
let timer=null;
function selected(id){return takes.find(t=>t.role===$('role').value&&t.variant===$(id).value)}
function stop(){if(timer)clearInterval(timer);timer=null;$('play').textContent='播放'}
function draw(){let i=+$('frame').value;for(const [key,prefix] of [['left','l'],['right','r']]){const t=selected(key),f=t.frames[i];$(prefix+'t').textContent=names[t.variant];$(prefix+'i').src=f.file;$(prefix+'c').textContent=t.setup.system.replace(/ \(0x[0-9A-F]+\)/gi,'');}
$('time').textContent=(i*.2).toFixed(1)+' s / '+i;$('stamp').textContent='实际游戏时间：左 '+(selected('left').frames[i].game_seconds-selected('left').frames[0].game_seconds).toFixed(3)+' s，右 '+(selected('right').frames[i].game_seconds-selected('right').frames[0].game_seconds).toFixed(3)+' s';}
function populate(){stop();const ts=takes.filter(t=>t.role===$('role').value);for(const key of ['left','right']){$(key).replaceChildren(...ts.map(t=>{let o=document.createElement('option');o.value=t.variant;o.textContent=names[t.variant];return o;}));}
$('left').value='Source';$('right').value=$('role').value==='Flash'?'Muzzle':'Opaque';$('contract').textContent=$('role').value==='Mining'?'采矿：启用光束原宽 5 cm → 7.5 cm，保留绿色、Ribbon 结构与端点火花。':$('role').value==='Construction'?'建造：启用光束原宽 8 cm → 12 cm，使用独立 ID 45，保留紫色与扫描端点。':'枪口基础缩放 1；命中基础缩放 2。六层与事件关联均保留。无碰撞仅为评估候选。';draw();}
$('role').onchange=populate;for(const k of ['left','right'])$(k).onchange=()=>{stop();draw();};$('frame').oninput=()=>{stop();draw();};
$('play').onclick=()=>{if(timer)return stop();$('play').textContent='暂停';timer=setInterval(()=>{$('frame').value=(+$('frame').value+1)%21;draw();},200);};
for(const [id,delta] of [['prev',-1],['next',1]])$(id).onclick=()=>{stop();$('frame').value=Math.max(0,Math.min(20,+$('frame').value+delta));draw();};
for(const b of document.querySelectorAll('[data-frame]'))b.onclick=()=>{stop();$('frame').value=b.dataset.frame;draw();};populate();
</script></html>'''
(OUT/'index.html').write_text(html.replace('__APPROVAL__',approval_text).replace('__TAKES__',json.dumps(takes,ensure_ascii=False).replace('</','<\\/')),encoding='utf-8')
print(json.dumps({'success':True,'takes':len(takes),'frames':sum(len(x['frames']) for x in takes),'page':str(OUT/'index.html')}))
