"use strict";
const data=window.REFERENCE_REVIEW;
if(!data)throw new Error("Reference manifest unavailable");
const categoryNames={Mass:"Mass 兵种",Building:"玩法建筑",SSF:"SSF 建筑"};
const esc=value=>String(value).replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));
const overview=document.body.dataset.mode==="overview";
const modeLink=document.getElementById("mode-link");
if(overview){modeLink.href="index.html";modeLink.textContent="进入逐模型审核 ↗";}
document.getElementById("palette").innerHTML=["fixed_armor","blue","red","mechanisms"].map(key=>{
 const p=data.palette[key];
 return '<div class="chip"><i style="background:'+esc(p.hex)+'"></i><div><strong>'+esc(p.label)+'</strong><code>'+esc(p.hex)+'</code></div></div>';
}).join("");
document.getElementById("palette-cards").innerHTML=data.palette_evidence.map((p,i)=>'<figure class="palette-card"><a href="'+esc(p.file)+'" target="_blank" rel="noopener"><img src="'+esc(p.file)+'" alt="用户色卡 '+(i+1)+'" loading="lazy"></a><figcaption>色卡 '+(i+1)+' · '+p.colors.map(esc).join(" / ")+'</figcaption></figure>').join("");
let filter="all",query="",lastFocus=null;
const gallery=document.getElementById("gallery");
const boardList=data.models.flatMap((m,index)=>[{index,team:"blue"},{index,team:"red"}]);
const colorLabel=team=>team==="blue"?"蓝方":"红方";
function boardMarkup(m,index,team){
 const b=m.boards[team],hex=data.palette[team].hex;
 return '<figure class="board '+team+'"><figcaption><strong>'+colorLabel(team)+' · '+hex+'</strong><span class="size">'+b.width+' × '+b.height+' · 点击放大</span></figcaption><button data-index="'+index+'" data-team="'+team+'" aria-label="放大 '+esc(m.name)+' '+colorLabel(team)+' 图板"><img src="'+esc(b.path)+'" alt="'+esc(m.name)+' '+colorLabel(team)+'，三分之四效果图、正视、左侧视、后视" loading="lazy" decoding="async" width="'+b.width+'" height="'+b.height+'"></button></figure>';
}
function zoneMarkup(label,className,lines){return '<div class="'+className+'"><h3><i class="dot"></i>'+label+'</h3><p>'+lines.map(esc).join("<br>")+'</p></div>';}
function render(){
 const shown=data.models.map((m,index)=>({m,index})).filter(({m})=>(filter==="all"||m.category===filter)&&(!query||[m.name,m.id,m.title].some(s=>s.toLowerCase().includes(query))));
 gallery.innerHTML=shown.map(({m,index})=>'<article class="model" id="'+esc(m.id)+'"><div class="model-head"><div><h2><span>'+String(index+1).padStart(2,"0")+'</span>'+esc(m.name)+'</h2><small>'+categoryNames[m.category]+' · '+esc(m.title)+' · A_v2</small></div><span class="pending">A 审核待决定</span></div><div class="pair">'+boardMarkup(m,index,"blue")+boardMarkup(m,index,"red")+'</div><div class="zone-note">'+zoneMarkup("固定区","fixed",m.fixed)+zoneMarkup("可变队色区","team",m.team)+zoneMarkup("固定功能区","function",m.function)+'</div><div class="structure">'+esc(m.structure)+' <a class="details-link" href="Configs/'+esc(m.id)+'.json" target="_blank" rel="noopener">配色说明 ↗</a></div><details><summary>原模型视图与来源</summary>'+m.source.map((p,i)=>'<a href="../../'+esc(p)+'" target="_blank" rel="noopener">原模型参考 '+(i+1)+' ↗</a>').join("")+'<p>原模型决定实际几何和活动关系。生成图板用于配色审核，固定色以配置中的 HEX 为准。</p></details></article>').join("");
 document.getElementById("result-count").textContent=shown.length+" 个模型 / "+shown.length*2+" 张";
 document.getElementById("empty").hidden=shown.length!==0;
 for(const b of document.querySelectorAll("[data-filter]"))b.setAttribute("aria-pressed",String(b.dataset.filter===filter));
}
document.getElementById("filters").addEventListener("click",event=>{
 const target=event.target.closest("[data-filter]");if(!target)return;
 filter=target.dataset.filter;render();
});
document.getElementById("search").addEventListener("input",event=>{query=event.target.value.trim().toLowerCase();render();});
gallery.addEventListener("click",event=>{
 const button=event.target.closest("button[data-index]");
 if(button){lastFocus=button;openZoom(Number(button.dataset.index),button.dataset.team);}
});
const dialog=document.getElementById("zoom-dialog"),zoomImage=document.getElementById("zoom-image"),stage=document.getElementById("zoom-stage");
let currentIndex=0,currentTeam="blue",scale=0;
function paintZoom(){
 const m=data.models[currentIndex],b=m.boards[currentTeam];
 document.getElementById("zoom-title").textContent=m.name+" · "+colorLabel(currentTeam)+" · A_v2";
 document.getElementById("zoom-meta").textContent=b.width+" × "+b.height+" · 参考设计 A 待审核";
 zoomImage.src=b.path;zoomImage.alt=m.name+" "+colorLabel(currentTeam)+" 完整图板";
 document.getElementById("zoom-original").href=b.path;
 document.getElementById("zoom-blue").setAttribute("aria-pressed",String(currentTeam==="blue"));
 document.getElementById("zoom-red").setAttribute("aria-pressed",String(currentTeam==="red"));
 zoomImage.classList.toggle("fit",scale===0);
 zoomImage.style.width=scale===0?"auto":b.width*scale+"px";
 stage.scrollTop=0;stage.scrollLeft=0;
}
function openZoom(index,team){currentIndex=index;currentTeam=team;scale=0;paintZoom();dialog.showModal();document.body.style.overflow="hidden";}
function switchBoard(step){
 const position=boardList.findIndex(b=>b.index===currentIndex&&b.team===currentTeam);
 const b=boardList[(position+step+boardList.length)%boardList.length];
 currentIndex=b.index;currentTeam=b.team;paintZoom();
}
document.getElementById("zoom-prev").onclick=()=>switchBoard(-1);
document.getElementById("zoom-next").onclick=()=>switchBoard(1);
document.getElementById("zoom-blue").onclick=()=>{currentTeam="blue";paintZoom();};
document.getElementById("zoom-red").onclick=()=>{currentTeam="red";paintZoom();};
document.getElementById("zoom-fit").onclick=()=>{scale=0;paintZoom();};
document.getElementById("zoom-native").onclick=()=>{scale=1;paintZoom();};
document.getElementById("zoom-plus").onclick=()=>{scale=Math.min(3,(scale||1)+.5);paintZoom();};
document.getElementById("zoom-close").onclick=()=>dialog.close();
dialog.addEventListener("close",()=>{document.body.style.overflow="";if(lastFocus?.isConnected)lastFocus.focus();});
dialog.addEventListener("keydown",event=>{
 if(event.key==="ArrowLeft"){event.preventDefault();switchBoard(-1);}
 if(event.key==="ArrowRight"){event.preventDefault();switchBoard(1);}
 if(event.key.toLowerCase()==="b"){currentTeam="blue";paintZoom();}
 if(event.key.toLowerCase()==="r"){currentTeam="red";paintZoom();}
});
render();
if(location.hash){
 const target=document.getElementById(decodeURIComponent(location.hash.slice(1)));
 if(target)requestAnimationFrame(()=>target.scrollIntoView());
}

