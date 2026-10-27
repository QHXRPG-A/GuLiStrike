/* Foreground-only parallax: the card window rim remains fixed in source UV. */
(()=>{
  class RicochetForegroundRenderer {
    constructor(image,art){
      this.image=image;this.art=art;this.last=[0,0,1];this.ready=false;
      this.front=document.createElement('canvas');this.under=document.createElement('canvas');
      for(const [canvas,part] of [[this.front,'front'],[this.under,'under']]){canvas.className='fg-canvas';canvas.dataset.part=part;canvas.dataset.mode='initializing';canvas.setAttribute('role','img');canvas.setAttribute('aria-label',part==='front'?'近景前方遮挡':'近景后方补全');}
      image.after(this.front);art.querySelector('[data-layer="3"]').before(this.under);
      try{
        this.contexts=[];
        for(const [canvas,part] of [[this.front,1],[this.under,0]]){
        const gl=canvas.getContext('webgl',{alpha:true,premultipliedAlpha:true,antialias:false,depth:false,stencil:false});
        if(!gl)throw Error('WebGL unavailable');
        const vertex='attribute vec2 aPosition;varying vec2 vUV;void main(){vUV=(aPosition+1.0)*0.5;gl_Position=vec4(aPosition,0.0,1.0);}';
        const fragment=`precision highp float;
          varying vec2 vUV;uniform sampler2D uTexture;uniform sampler2D uMask;uniform vec2 uShift;uniform float uPart;
          void main(){
            vec2 low=smoothstep(vec2(0.02),vec2(0.12),vUV);
            vec2 high=smoothstep(vec2(0.02),vec2(0.12),vec2(1.0)-vUV);
            float weight=low.x*low.y*high.x*high.y;
            vec2 source=(vUV-uShift*weight)*(2.0/3.0)+vec2(1.0/6.0);
            float front=step(0.5,texture2D(uMask,source).r);
            float fraction=mix(1.0-front,front,uPart);
            gl_FragColor=texture2D(uTexture,source)*fraction;
          }`;
        const compile=(type,source)=>{const shader=gl.createShader(type);gl.shaderSource(shader,source);gl.compileShader(shader);if(!gl.getShaderParameter(shader,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(shader));return shader;};
        const program=gl.createProgram();gl.attachShader(program,compile(gl.VERTEX_SHADER,vertex));gl.attachShader(program,compile(gl.FRAGMENT_SHADER,fragment));gl.linkProgram(program);
        if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(program));
        gl.useProgram(program);
        const buffer=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,buffer);
        gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,-1,1,1,-1,1,1]),gl.STATIC_DRAW);
        const attribute=gl.getAttribLocation(program,'aPosition');gl.enableVertexAttribArray(attribute);gl.vertexAttribPointer(attribute,2,gl.FLOAT,false,0,0);
        const shift=gl.getUniformLocation(program,'uShift');
        gl.uniform1i(gl.getUniformLocation(program,'uTexture'),0);
        gl.uniform1i(gl.getUniformLocation(program,'uMask'),1);gl.uniform1f(gl.getUniformLocation(program,'uPart'),part);
        const texture=gl.createTexture();gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,texture);
        gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.LINEAR);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);
        gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL,true);
        gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL,true);
        gl.enable(gl.BLEND);gl.blendFuncSeparate(gl.ONE,gl.ONE_MINUS_SRC_ALPHA,gl.ONE,gl.ONE_MINUS_SRC_ALPHA);
        const maskTexture=gl.createTexture();gl.activeTexture(gl.TEXTURE1);gl.bindTexture(gl.TEXTURE_2D,maskTexture);
        gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);
        this.contexts.push({canvas,gl,program,shift,texture,maskTexture});
        canvas.addEventListener('webglcontextlost',event=>{event.preventDefault();this.fallback(Error('WebGL context lost'));});
        }
        const maskImage=new Image();this.maskImage=maskImage;
        const load=()=>{if(!image.complete||!image.naturalWidth||!maskImage.complete||!maskImage.naturalWidth)return;try{for(const context of this.contexts){const {gl,texture,maskTexture,canvas}=context;gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,texture);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,gl.RGBA,gl.UNSIGNED_BYTE,image);gl.activeTexture(gl.TEXTURE1);gl.bindTexture(gl.TEXTURE_2D,maskTexture);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,gl.RGBA,gl.UNSIGNED_BYTE,maskImage);canvas.dataset.mode='webgl-rim-pinned';canvas.dataset.shaderCompiled='true';}this.ready=true;image.style.visibility='hidden';this.draw(...this.last);art.dispatchEvent(new Event('foreground-ready'));}catch(error){this.fallback(error);}};
        image.addEventListener('load',load,{once:true});maskImage.addEventListener('load',load,{once:true});maskImage.addEventListener('error',()=>this.fallback(Error('Foreground occlusion mask unavailable')),{once:true});
        maskImage.src='Masks/ForegroundFront_R3.png?v=completion-r3';load();
        this.resize=new ResizeObserver(()=>this.draw(...this.last));this.resize.observe(art);
      }catch(error){this.fallback(error);}
    }
    fallback(error){
      if(this.static2D)return;this.static2D=true;this.ready=false;
      for(const key of ['front','under']){const old=this[key],canvas=document.createElement('canvas');canvas.className='fg-canvas';canvas.dataset.part=key;canvas.dataset.mode='2d-fixed-rim';canvas.dataset.error=String(error.message||error);old.replaceWith(canvas);this[key]=canvas;}
      if(!this.maskImage){this.maskImage=new Image();this.maskImage.src='Masks/ForegroundFront_R3.png?v=completion-r3';}
      const load=()=>{if(!this.image.complete||!this.image.naturalWidth||!this.maskImage.complete||!this.maskImage.naturalWidth)return;this.ready=true;this.image.style.visibility='hidden';this.draw(...this.last);this.art.dispatchEvent(new Event('foreground-ready'));};
      this.image.addEventListener('load',load,{once:true});this.maskImage.addEventListener('load',load,{once:true});load();
      if(!this.resize){this.resize=new ResizeObserver(()=>this.draw(...this.last));this.resize.observe(this.art);}
    }
    draw(dx,dy,opacity){
      this.last=[dx,dy,opacity];for(const canvas of [this.front,this.under])canvas.style.opacity=String(opacity);
      if(!this.ready)return;
      const width=this.art.clientWidth,height=this.art.clientHeight;
      if(!width||!height)return;
      const ratio=Math.min(window.devicePixelRatio||1,2),w=Math.round(width*ratio),h=Math.round(height*ratio);
      if(this.static2D){for(const [canvas,operation] of [[this.front,'destination-in'],[this.under,'destination-out']]){canvas.width=w;canvas.height=h;const context=canvas.getContext('2d');context.drawImage(this.image,256,256,1024,1024,0,0,w,h);context.globalCompositeOperation=operation;context.drawImage(this.maskImage,256,256,1024,1024,0,0,w,h);context.globalCompositeOperation='source-over';canvas.dataset.requestedShift=JSON.stringify([dx,dy]);canvas.dataset.fixedRim='0.02';}return;}
      for(const context of this.contexts){const {canvas,gl,program,shift,texture,maskTexture}=context;
      if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;}
      gl.viewport(0,0,w,h);gl.useProgram(program);gl.activeTexture(gl.TEXTURE0);gl.bindTexture(gl.TEXTURE_2D,texture);gl.activeTexture(gl.TEXTURE1);gl.bindTexture(gl.TEXTURE_2D,maskTexture);
      gl.uniform2f(shift,dx/width,-dy/height);gl.clearColor(0,0,0,0);gl.clear(gl.COLOR_BUFFER_BIT);gl.drawArrays(gl.TRIANGLES,0,6);
      canvas.dataset.requestedShift=JSON.stringify([dx,dy]);canvas.dataset.fixedRim='0.02';canvas.dataset.rampEnd='0.12';}
    }
  }
  window.RicochetForegroundRenderer=RicochetForegroundRenderer;
})();
