(()=>{
  'use strict';
  const data=window.CARD_PREVIEW_DATA;
  const $=id=>document.getElementById(id);
  const stage=$('stage'),card=$('card'),master=$('master');
  const strength=$('strength'),tilt=$('tilt'),auto=$('auto'),status=$('status');
  const labels={flat:'原位叠合',parallax:'交互视差',original:'原图',compare:'原图对照'};
  const cache=new Map(),states={};
  for(const key of Object.keys(data.cards))states[key]=Array.from({length:6},()=>({visible:true,opacity:1}));
  const requested=new URLSearchParams(location.search).get('card');
  let current=data.cards[requested]?requested:'Burst',mode='flat',px=0,py=0,raf=0,renderer=null,loadSequence=0,solo=null;
  const reduced=matchMedia('(prefers-reduced-motion: reduce)').matches;
  const asset=path=>data.embedded?.[path]||path;
  const load=path=>{
    if(!cache.has(path))cache.set(path,new Promise((resolve,reject)=>{const image=new Image();image.onload=()=>resolve(image);image.onerror=()=>reject(Error('无法读取 '+path));image.src=asset(path);}));
    return cache.get(path);
  };

  class CardRenderer {
    constructor(images,passes,reference){
      this.images=images;this.passes=passes;this.reference=reference;this.ready=false;
      this.nativeCanvas=document.createElement('canvas');this.nativeCanvas.width=this.nativeCanvas.height=1024;
      this.nativeContext=this.nativeCanvas.getContext('2d');
      try{this.initGL();this.kind='WebGL';}catch(error){
        // A replacement canvas can use 2D after a failed WebGL allocation.
        this.error=String(error.message||error);const replacement=document.createElement('canvas');
        replacement.id='composite';replacement.setAttribute('role','img');replacement.setAttribute('aria-label','六层牌面叠合');
        $('composite').replaceWith(replacement);this.surface=replacement;this.ctx=replacement.getContext('2d');
        if(!this.ctx)throw error;this.kind='Canvas2D';
      }
      this.ready=true;this.surface.dataset.ready='true';this.surface.dataset.renderer=this.kind;
      this.surface.dataset.layers='6';this.surface.dataset.nativeResolution='1024x1024';
      this.surface.dataset.sourceContract=data.version;
      this.surface.dataset.motionModel='rigid-translation';
    }
    initGL(){
      this.surface=$('composite');const gl=this.surface.getContext('webgl',{alpha:true,premultipliedAlpha:true,antialias:false,preserveDrawingBuffer:true,depth:false,stencil:false});
      if(!gl)throw Error('WebGL unavailable');this.gl=gl;
      const vs='attribute vec2 aPosition;varying vec2 vUV;void main(){vUV=(aPosition+1.0)*0.5;gl_Position=vec4(aPosition,0.0,1.0);}';
      // Every point of an object receives exactly the same translation. The
      // padded source canvas supplies overscan; a rim warp would deform it.
      const fs=`precision highp float;varying vec2 vUV;uniform sampler2D uImage;uniform vec2 uShift;uniform float uOpacity;
        void main(){vec2 uv=(vUV-uShift)*(2.0/3.0)+vec2(1.0/6.0);
          gl_FragColor=texture2D(uImage,uv)*uOpacity;}`;
      const compile=(type,source)=>{const shader=gl.createShader(type);gl.shaderSource(shader,source);gl.compileShader(shader);if(!gl.getShaderParameter(shader,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(shader));return shader;};
      const program=gl.createProgram();gl.attachShader(program,compile(gl.VERTEX_SHADER,vs));gl.attachShader(program,compile(gl.FRAGMENT_SHADER,fs));gl.bindAttribLocation(program,0,'aPosition');gl.linkProgram(program);
      if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(program));this.program=program;gl.useProgram(program);
      const buffer=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,buffer);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,-1,1,1,-1,1,1]),gl.STATIC_DRAW);
      const attr=gl.getAttribLocation(program,'aPosition');gl.enableVertexAttribArray(attr);gl.vertexAttribPointer(attr,2,gl.FLOAT,false,0,0);
      this.uniforms=Object.fromEntries(['uShift','uOpacity'].map(x=>[x,gl.getUniformLocation(program,x)]));
      gl.uniform1i(gl.getUniformLocation(program,'uImage'),0);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL,true);gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL,true);
      gl.pixelStorei(gl.UNPACK_COLORSPACE_CONVERSION_WEBGL,gl.NONE);gl.disable(gl.DITHER);
      const texture=image=>{const t=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,t);for(const p of [gl.TEXTURE_WRAP_S,gl.TEXTURE_WRAP_T])gl.texParameteri(gl.TEXTURE_2D,p,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.LINEAR);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.LINEAR);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,gl.RGBA,gl.UNSIGNED_BYTE,image);return t;};
      this.textures=this.images.map(texture);
      this.referenceTexture=texture(this.reference);
      // The ring is sampled once as a complete object. Compose its far side
      // behind the actual host and its near side in front, in this one draw.
      const pairFS=`precision highp float;varying vec2 vUV;
        uniform sampler2D uRing;uniform sampler2D uHost;
        uniform vec2 uRingShift;uniform vec2 uHostShift;uniform vec2 uPlane;
        uniform float uRingOpacity;uniform float uHostOpacity;
        void main(){vec2 ur=(vUV-uRingShift)*(2.0/3.0)+vec2(1.0/6.0);
          vec2 uh=(vUV-uHostShift)*(2.0/3.0)+vec2(1.0/6.0);
          vec4 r=texture2D(uRing,ur)*uRingOpacity;vec4 h=texture2D(uHost,uh)*uHostOpacity;
          vec2 p=vec2(ur.x,1.0-ur.y)*1536.0-vec2(256.0);
          bool farSide=p.y<uPlane.x*p.x+uPlane.y;
          gl_FragColor=farSide?h+r*(1.0-h.a):r+h*(1.0-r.a);}`;
      const pair=gl.createProgram();gl.attachShader(pair,compile(gl.VERTEX_SHADER,vs));gl.attachShader(pair,compile(gl.FRAGMENT_SHADER,pairFS));
      gl.bindAttribLocation(pair,0,'aPosition');gl.linkProgram(pair);
      if(!gl.getProgramParameter(pair,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(pair));
      this.pairProgram=pair;this.pairUniforms=Object.fromEntries(['uRingShift','uHostShift','uPlane','uRingOpacity','uHostOpacity'].map(n=>[n,gl.getUniformLocation(pair,n)]));
      gl.useProgram(pair);gl.uniform1i(gl.getUniformLocation(pair,'uRing'),0);gl.uniform1i(gl.getUniformLocation(pair,'uHost'),1);
      this.surface.dataset.ringCompositorCompiled='true';
      // Composite the six layers at native source resolution before any screen
      // filtering. Filtering independently partitioned layers causes edge halos.
      this.nativeTexture=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,this.nativeTexture);
      gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.LINEAR);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);
      gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,1024,1024,0,gl.RGBA,gl.UNSIGNED_BYTE,null);
      this.nativeFramebuffer=gl.createFramebuffer();gl.bindFramebuffer(gl.FRAMEBUFFER,this.nativeFramebuffer);
      gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,this.nativeTexture,0);
      if(gl.checkFramebufferStatus(gl.FRAMEBUFFER)!==gl.FRAMEBUFFER_COMPLETE)throw Error('Native composition framebuffer unavailable');
      gl.bindFramebuffer(gl.FRAMEBUFFER,null);
      this.presentProgram=gl.createProgram();gl.attachShader(this.presentProgram,compile(gl.VERTEX_SHADER,vs));
      gl.attachShader(this.presentProgram,compile(gl.FRAGMENT_SHADER,'precision highp float;varying vec2 vUV;uniform sampler2D uImage;void main(){gl_FragColor=texture2D(uImage,vUV);}'));
      gl.bindAttribLocation(this.presentProgram,0,'aPosition');gl.linkProgram(this.presentProgram);if(!gl.getProgramParameter(this.presentProgram,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(this.presentProgram));
      gl.useProgram(this.presentProgram);gl.uniform1i(gl.getUniformLocation(this.presentProgram,'uImage'),0);
      const presentAttribute=gl.getAttribLocation(this.presentProgram,'aPosition');gl.enableVertexAttribArray(presentAttribute);gl.vertexAttribPointer(presentAttribute,2,gl.FLOAT,false,0,0);
      gl.enable(gl.BLEND);gl.blendFuncSeparate(gl.ONE,gl.ONE_MINUS_SRC_ALPHA,gl.ONE,gl.ONE_MINUS_SRC_ALPHA);
      this.surface.dataset.shaderCompiled='true';
      this.surface.dataset.nativeFramebuffer='complete';
      this.surface.addEventListener('webglcontextlost',event=>{event.preventDefault();status.textContent='预览上下文暂时丢失，请切换卡牌重新载入。';this.ready=false;this.surface.dataset.ready='false';});
    }
    presentGL(width,height){
      const gl=this.gl;gl.bindFramebuffer(gl.FRAMEBUFFER,null);gl.viewport(0,0,width,height);
      gl.disable(gl.BLEND);gl.useProgram(this.presentProgram);gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D,this.nativeTexture);gl.drawArrays(gl.TRIANGLES,0,6);
    }
    draw(x,y,layerState,config){
      if(!this.ready)return;
      const rect=$('art').getBoundingClientRect(),dpr=Math.min(devicePixelRatio||1,2);
      const w=Math.max(1,Math.round(rect.width*dpr)),h=Math.max(1,Math.round(rect.height*dpr));
      if(this.surface.width!==w||this.surface.height!==h){this.surface.width=w;this.surface.height=h;}
      const depth=n=>config.depth[(config.layers[n].motion_owner_layer||n+1)-1];
      const shift=n=>[x*depth(n)/rect.width,-y*depth(n)/rect.height];
      const opacity=n=>layerState[n].visible?layerState[n].opacity:0;
      const motion=config.layers.map((layer,n)=>({layer:n+1,owner:layer.motion_owner_layer||n+1,
        translation:[x*depth(n)*1024/rect.width,y*depth(n)*1024/rect.height],opacity:opacity(n)}));
      if(this.gl){const gl=this.gl;gl.bindFramebuffer(gl.FRAMEBUFFER,this.nativeFramebuffer);gl.viewport(0,0,1024,1024);gl.clearColor(0,0,0,0);gl.clear(gl.COLOR_BUFFER_BIT);
        if(mode==='original'){
          // The reference is sampled only in the explicitly selected Original
          // mode, never as an overlay or substitute for the six-layer image.
          gl.disable(gl.BLEND);gl.useProgram(this.presentProgram);gl.activeTexture(gl.TEXTURE0);
          gl.bindTexture(gl.TEXTURE_2D,this.referenceTexture);gl.drawArrays(gl.TRIANGLES,0,6);
        }else{
        gl.enable(gl.BLEND);
        for(const pass of this.passes){const n=pass.layer-1;
          if(pass.host_layer){const hn=pass.host_layer-1;if(opacity(n)===0&&opacity(hn)===0)continue;
            gl.useProgram(this.pairProgram);gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,this.textures[n]);
            gl.activeTexture(gl.TEXTURE1);gl.bindTexture(gl.TEXTURE_2D,this.textures[hn]);
            gl.uniform2f(this.pairUniforms.uRingShift,...shift(n));gl.uniform2f(this.pairUniforms.uHostShift,...shift(hn));
            gl.uniform2f(this.pairUniforms.uPlane,...pass.occlusion_plane);
            gl.uniform1f(this.pairUniforms.uRingOpacity,opacity(n));gl.uniform1f(this.pairUniforms.uHostOpacity,opacity(hn));
          }else{if(opacity(n)===0)continue;gl.useProgram(this.program);
            gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,this.textures[n]);
            gl.uniform2f(this.uniforms.uShift,...shift(n));gl.uniform1f(this.uniforms.uOpacity,opacity(n));}
          gl.drawArrays(gl.TRIANGLES,0,6);
        }
        }this.presentGL(w,h);
      }else{const ctx=this.nativeContext;ctx.clearRect(0,0,1024,1024);ctx.imageSmoothingEnabled=true;
        if(mode==='original')ctx.drawImage(this.reference,0,0,1024,1024);
        else{
        const drawLayer=n=>{if(opacity(n)===0)return;ctx.globalAlpha=opacity(n);
          ctx.drawImage(this.images[n],-256+motion[n].translation[0],-256+motion[n].translation[1],1536,1536);};
        for(const pass of this.passes){const n=pass.layer-1;
          if(pass.host_layer){
            // Canvas2D uses the same complete ring and rigid translation in
            // one local composite. No stored cut mask or independent parts.
            const [dx,dy]=motion[n].translation,[slope,intercept]=pass.occlusion_plane;
            const at=xx=>slope*(xx-dx)+intercept+dy,edge=4096;
            const ringHalf=front=>{ctx.save();ctx.beginPath();ctx.moveTo(-edge,at(-edge));ctx.lineTo(edge,at(edge));
              ctx.lineTo(edge,front?edge:-edge);ctx.lineTo(-edge,front?edge:-edge);ctx.closePath();ctx.clip();drawLayer(n);ctx.restore();};
            ringHalf(false);drawLayer(pass.host_layer-1);ringHalf(true);
          }else drawLayer(n);
        }ctx.globalAlpha=1;}
        this.ctx.clearRect(0,0,w,h);this.ctx.imageSmoothingEnabled=true;this.ctx.imageSmoothingQuality='high';this.ctx.drawImage(this.nativeCanvas,0,0,w,h);
      }
      this.surface.dataset.mode=mode;this.surface.dataset.activeCard=current;this.surface.dataset.drawCount=String((+this.surface.dataset.drawCount||0)+1);
      this.surface.dataset.layerState=JSON.stringify(layerState);this.surface.dataset.pointer=JSON.stringify([px,py]);
      this.surface.dataset.composition=mode==='original'?'reference':'six-layer-native';
      this.surface.dataset.layerMotion=JSON.stringify(motion);
      this.surface.dataset.spatialUvWarp='false';
      this.surface.dataset.ringObjectComposites=String(this.passes.filter(p=>p.host_layer).length);
    }
    exportNative(){
      if(!this.gl)return this.nativeCanvas.toDataURL('image/png');
      this.surface.width=this.surface.height=1024;this.presentGL(1024,1024);
      const url=this.surface.toDataURL('image/png');queuePaint();return url;
    }
  }

  function paint(){
    const active=mode==='parallax',x=active?px:0,y=active?py:0;
    card.style.transform=`rotateX(${-y*+tilt.value}deg) rotateY(${x*+tilt.value}deg)`;
    $('strength-value').value=strength.value+' px';$('tilt-value').value=tilt.value+'°';
    renderer?.draw(-x*+strength.value,-y*+strength.value,states[current],data.cards[current]);
  }
  function queuePaint(){if(!raf)raf=requestAnimationFrame(()=>{raf=0;paint();});}
  function setMode(value){
    mode=value;px=py=0;auto.checked=false;master.hidden=true;$('composite').hidden=false;
    $('reference-pane').hidden=value!=='compare';stage.classList.toggle('compare',value==='compare');
    $('mode-label').textContent=labels[value];document.querySelectorAll('[data-mode]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.mode===value)));
    $('hint').textContent=value==='parallax'?'移动鼠标或拖动牌面，观察各层相对运动':value==='compare'?'左侧为固定原图，右侧为六层原位叠合':value==='original'?'用户选定的固定原图':'可用右侧开关查看每层；切换交互视差后体验深度';
    queuePaint();
  }
  function layerControls(){
    const config=data.cards[current],list=$('layers');list.replaceChildren();
    for(const [n,layer] of config.layers.entries()){
      const row=document.createElement('div');row.className='layer-row';row.dataset.layer=String(n+1);row.dataset.solo=String(solo===n);
      const check=document.createElement('input');check.type='checkbox';check.id='layer-'+(n+1);check.checked=states[current][n].visible;check.setAttribute('aria-label','显示第'+(n+1)+'层');
      const index=document.createElement('span');index.className='layer-index';index.textContent=String(n+1).padStart(2,'0');
      const label=document.createElement('label');label.htmlFor=check.id;label.textContent=layer.name;
      const only=document.createElement('button');only.className='text-button';only.type='button';only.textContent=solo===n?'还原':'单独';only.setAttribute('aria-label','单独显示'+layer.name);
      const link=document.createElement('a');link.href=asset(layer.file);link.download=layer.file.split('/').pop();link.textContent='PNG';
      const opacity=document.createElement('input');opacity.type='range';opacity.min='0';opacity.max='100';opacity.value=String(Math.round(states[current][n].opacity*100));opacity.setAttribute('aria-label',layer.name+'透明度');
      check.addEventListener('change',()=>{states[current][n].visible=check.checked;solo=null;queuePaint();});
      only.addEventListener('click',()=>{if(solo===n){states[current].forEach(s=>s.visible=true);solo=null;}else{solo=n;states[current].forEach((s,i)=>s.visible=i===n);}layerControls();queuePaint();});
      opacity.addEventListener('input',()=>{states[current][n].opacity=+opacity.value/100;queuePaint();});
      row.append(check,index,label,only,link,opacity);list.append(row);
    }
  }
  async function select(key){
    const sequence=++loadSequence;current=key;solo=null;renderer?.gl?.getExtension('WEBGL_lose_context')?.loseContext();renderer=null;
    // Replace the canvas on every card switch to release the prior GL context.
    const replacement=document.createElement('canvas');replacement.id='composite';replacement.setAttribute('role','img');replacement.setAttribute('aria-label','六层牌面叠合');$('composite').replaceWith(replacement);
    const config=data.cards[key];$('card-title').textContent=config.title;$('footer-name').textContent=config.title;$('card-id').textContent=key==='Burst'?'BURNING BURST':'BURNING BULLET';$('layer-note').textContent=config.note;
    document.querySelectorAll('[data-card]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.card===key)));
    master.src=asset(config.reference);$('compare-reference').src=asset(config.reference);$('download-master').href=asset(config.reference);$('download-master').download=key+'_Reference.png';
    $('loading').hidden=false;status.textContent='正在读取6张图层…';$('export').disabled=true;layerControls();setMode('flat');
    try{
      const images=await Promise.all(config.layers.map(l=>load(l.file)));if(sequence!==loadSequence)return;
      const referenceImage=await load(config.reference);if(sequence!==loadSequence)return;
      renderer=new CardRenderer(images,config.passes,referenceImage);
      $('loading').hidden=true;$('export').disabled=false;status.textContent='6 / 6 图层已载入 · 可逐层查看构图、元素与视差';paint();
      document.body.dataset.ready='true';document.body.dataset.activeCard=key;document.body.dataset.loadedLayers='6';
    }catch(error){$('loading').textContent='图层读取失败，请使用离线HTML或预览入口打开。';status.textContent=String(error.message||error);document.body.dataset.ready='false';}
  }
  function pointer(event){if(mode!=='parallax')return;auto.checked=false;const r=$('art').getBoundingClientRect();px=Math.max(-1,Math.min(1,(event.clientX-r.left)/r.width*2-1));py=Math.max(-1,Math.min(1,(event.clientY-r.top)/r.height*2-1));queuePaint();}
  stage.addEventListener('pointermove',pointer);stage.addEventListener('pointerdown',e=>{if(mode==='parallax'){stage.setPointerCapture(e.pointerId);pointer(e);}});
  const center=()=>{px=py=0;auto.checked=false;queuePaint();};stage.addEventListener('pointerleave',()=>{if(!auto.checked)center();});stage.addEventListener('pointerup',center);$('center').addEventListener('click',center);
  document.querySelectorAll('[data-card]').forEach(b=>b.addEventListener('click',()=>select(b.dataset.card)));
  document.querySelectorAll('[data-mode]').forEach(b=>b.addEventListener('click',()=>setMode(b.dataset.mode)));
  for(const input of [strength,tilt])input.addEventListener('input',queuePaint);
  $('show-all').addEventListener('click',()=>{states[current].forEach(s=>s.visible=true);solo=null;layerControls();queuePaint();});
  $('reset').addEventListener('click',()=>{states[current].forEach(s=>{s.visible=true;s.opacity=1;});strength.value='6';tilt.value='10';solo=null;layerControls();setMode('flat');});
  auto.addEventListener('change',()=>{if(auto.checked){setMode('parallax');auto.checked=true;}else center();});
  $('export').addEventListener('click',()=>{try{center();paint();const link=document.createElement('a');link.href=renderer.exportNative();link.download=current+'_Composite_1024.png';link.click();status.textContent='1024×1024原尺寸PNG已导出；逐层PNG可使用每层右侧下载。';}catch{status.textContent='请打开离线HTML或本地预览入口后，再导出叠合PNG。';}});
  if(reduced){auto.disabled=true;auto.title='系统已启用减少动态效果';}
  if(data.standalone){$('download-html').hidden=true;$('download-zip').hidden=true;$('download-manifest').hidden=true;}
  new ResizeObserver(queuePaint).observe($('art'));window.addEventListener('blur',center);
  function animate(time){if(auto.checked&&mode==='parallax'&&!reduced){px=Math.sin(time/1700)*.85;py=Math.sin(time/2200)*.65;paint();}requestAnimationFrame(animate);}requestAnimationFrame(animate);
  select(current);
})();
