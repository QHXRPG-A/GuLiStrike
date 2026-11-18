"use strict";
const data=window.cardData;
const workspace=document.querySelector("#workspace"),card=document.querySelector("#card"),canvas=document.querySelector("#art"),ctx=canvas.getContext("2d"),surface=document.querySelector("#motion-area");
const status=document.querySelector("#status"),strength=document.querySelector("#strength"),hint=document.querySelector("#hint");
const layers=new Map(data.layers.map(x=>[x.index,{...x,enabled:true,opacity:1}]));
let reference,ready=false,mode="stack",solo=null,frame=0,drag=null;
const target={x:0,y:0},position={x:0,y:0};
const reduced=matchMedia("(prefers-reduced-motion: reduce)");
let drawLog=[];
const hints={reference:"用户原图，按同一固定窗口显示。",stack:"六层原位叠合，可开关、独看或调整透明度。",parallax:"移动鼠标或拖动牌面查看视差；移出后自动回正。",compare:"左侧为用户原图，右侧为六层原位叠合。"};
function asset(p){return p+"?v="+data.revision}
function loadImage(p){return new Promise((resolve,reject)=>{const im=new Image();im.onload=()=>resolve(im);im.onerror=()=>reject(Error(p));im.src=asset(p)})}
function viewport(full=false){return full?[0,0,...data.canvas]:data.window}
function setup(c,full=false){const [x0,y0,x1,y1]=viewport(full);const w=x1-x0,h=y1-y0;const rw=Math.ceil(w),rh=Math.ceil(h);if(c.width!==rw)c.width=rw;if(c.height!==rh)c.height=rh;const cc=c.getContext("2d");cc.setTransform(1,0,0,1,0,0);cc.clearRect(0,0,c.width,c.height);cc.setTransform(rw/w,0,0,rh/h,0,0);return {cc,x0,y0,w,h}}
function paint(c,kind,{full=false,interactive=false}={}){
 const view=setup(c,full),cctx=view.cc;const calls=[];
 if(kind==="reference"){cctx.drawImage(reference,data.sourceCore[0]-view.x0,data.sourceCore[1]-view.y0);calls.push({kind:"reference"});return calls}
 const amount=interactive?Number(strength.value)/100:0;
 for(const id of data.drawOrder){const l=layers.get(id);if(solo!==null?id!==solo:!l.enabled)continue;
  const d=data.motionOwners[l.motion_owner];const dx=position.x*amount*data.travel[0]*d,dy=position.y*amount*data.travel[1]*d;
  cctx.globalAlpha=l.opacity;cctx.drawImage(l.image,-view.x0+dx,-view.y0+dy);
  calls.push({layer:id,dx,dy,opacity:l.opacity,file:l.file,draws:1});
 }
 cctx.globalAlpha=1;return calls;
}
function draw(){if(!ready)return;
 const full=solo!==null;
 card.style.aspectRatio=full?data.canvas[0]/data.canvas[1]:data.apertureRatio;
 const f=mode==="parallax"&&solo===null?Number(strength.value)/100:0;
 card.style.transform="rotateX("+(-position.y*f*data.maxTilt)+"deg) rotateY("+(position.x*f*data.maxTilt)+"deg)";
 if(mode==="compare"){drawLog=[...paint(document.querySelector("#reference-view"),"reference"),...paint(document.querySelector("#stack-view"),"stack")]}
 else{drawLog=paint(canvas,mode==="reference"?"reference":"stack",{full,interactive:mode==="parallax"})}
}
function schedule(){if(!frame)frame=requestAnimationFrame(animate)}
function animate(){frame=0;const ease=reduced.matches?1:.22;const distance=Math.max(Math.abs(target.x-position.x),Math.abs(target.y-position.y));if(distance<.001){Object.assign(position,target)}else{position.x+=(target.x-position.x)*ease;position.y+=(target.y-position.y)*ease;schedule()}draw()}
function center(immediate=true){target.x=target.y=0;if(immediate)position.x=position.y=0;schedule()}
function updateSolo(){for(const l of layers.values()){l.soloButton.textContent=l.index===solo?"退出独看":"独看";l.soloButton.setAttribute("aria-pressed",String(l.index===solo))}hint.textContent=solo!==null?layers.get(solo).name+" · 完整图层，含外扩内容":hints[mode]}
function setMode(next){mode=next;if(next==="reference"||next==="compare")solo=null;workspace.dataset.mode=next;for(const b of document.querySelectorAll(".modes button"))b.setAttribute("aria-pressed",String(b.dataset.mode===next));strength.disabled=next!=="parallax";updateSolo();center()}
function layerControl(l){const li=document.createElement("li");li.className="layer";li.dataset.layer=l.index;li.innerHTML='<div class="layer-main"><label><input type="checkbox" checked><span class="index"></span><span class="name"></span></label><button aria-pressed="false">独看</button><a class="download" download>PNG</a></div><div class="opacity"><input type="range" min="0" max="100" value="100"><output>100%</output></div>';
 li.querySelector(".index").textContent=String(l.index).padStart(2,"0");li.querySelector(".name").textContent=l.name;
 const check=li.querySelector("input[type=checkbox]"),slider=li.querySelector("input[type=range]"),out=li.querySelector("output"),link=li.querySelector("a");l.soloButton=li.querySelector("button");l.checkbox=check;l.slider=slider;l.output=out;
 slider.setAttribute("aria-label",l.name+"不透明度");link.href=asset(l.file);link.download=l.file.split("/").at(-1);
 check.addEventListener("change",()=>{l.enabled=check.checked;solo=null;if(mode==="reference")setMode("stack");updateSolo();schedule()});
 slider.addEventListener("input",()=>{l.opacity=Number(slider.value)/100;out.textContent=slider.value+"%";if(mode==="reference")setMode("stack");schedule()});
 l.soloButton.addEventListener("click",()=>{const wanted=solo===l.index?null:l.index;if(mode==="reference"||mode==="compare")setMode("stack");solo=wanted;center();updateSolo();schedule()});return li;
}
document.querySelector("#layers").append(...[...layers.values()].map(layerControl));
document.querySelectorAll(".modes button").forEach(b=>b.addEventListener("click",()=>setMode(b.dataset.mode)));
document.querySelector("#reset").addEventListener("click",()=>{solo=null;for(const l of layers.values()){l.enabled=true;l.opacity=1;l.checkbox.checked=true;l.slider.value=100;l.output.textContent="100%"}updateSolo();center()});
strength.addEventListener("input",()=>{document.querySelector("#strength-value").textContent=strength.value+"%";schedule()});
function pointer(e){if(mode!=="parallax"||(e.pointerType!=="mouse"&&drag!==e.pointerId))return;const r=card.getBoundingClientRect();target.x=Math.max(-1,Math.min(1,(e.clientX-r.left-r.width/2)*2/r.width));target.y=Math.max(-1,Math.min(1,(e.clientY-r.top-r.height/2)*2/r.height));schedule()}
surface.addEventListener("pointermove",pointer);surface.addEventListener("pointerdown",e=>{if(mode!=="parallax")return;drag=e.pointerId;surface.setPointerCapture(e.pointerId);pointer(e)});
surface.addEventListener("pointerup",e=>{if(surface.hasPointerCapture(e.pointerId))surface.releasePointerCapture(e.pointerId)});
surface.addEventListener("pointerleave",()=>{if(drag===null)center(false)});surface.addEventListener("lostpointercapture",()=>{drag=null;center(false)});
document.querySelector("#geometry-note").textContent="卡框图片区 "+data.apertureRatio.toFixed(8)+"∶1 · 左右各裁 "+data.cropEachSide.toFixed(4)+" 源像素。";
document.querySelector("#metric-note").textContent="原位 RGB SSIM "+(data.ssim*100).toFixed(2)+"% · 数值要求 ≥80%。";
window.cardPreview={getState:()=>({ready,mode,solo,position:{...position},drawLog:structuredClone(drawLog),layers:[...layers.values()].map(l=>({index:l.index,enabled:l.enabled,opacity:l.opacity,file:l.file,width:l.image?.naturalWidth,height:l.image?.naturalHeight})),apertureRatio:data.apertureRatio,window:data.window,canvas:data.canvas}),setPose(x,y){target.x=position.x=x;target.y=position.y=y;draw()},setMode,renderNow:draw};
(async()=>{try{[reference]=await Promise.all([loadImage(data.source),...[...layers.values()].map(async l=>{l.image=await loadImage(l.file);if(l.image.naturalWidth!==data.canvas[0]||l.image.naturalHeight!==data.canvas[1])throw Error("图层尺寸不一致："+l.name)})]);ready=true;status.dataset.state="ready";status.textContent="6 层已载入";draw()}catch(e){status.dataset.state="error";status.textContent="载入失败 · "+e.message;console.error(e)}})();

