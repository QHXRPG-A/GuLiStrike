/* Technical reference only. No Blender/FBX/UE export. Positions are normalized diagram units. */
(() => {
  'use strict';
  const canvas = document.getElementById('card-demo');
  const info = document.getElementById('card-demo-count');
  const picker = document.getElementById('card-demo-kind');
  const masked = document.getElementById('card-demo-mask');
  const wire = document.getElementById('card-demo-wire');
  const view = document.getElementById('card-demo-view');
  const gl = canvas.getContext('webgl', { antialias: true, alpha: false, preserveDrawingBuffer: true });
  if (!gl) { info.textContent = '此浏览器无法显示 WebGL 技术示意；请查看下方图集与制作规范。'; return; }
  const vertex = 'attribute vec3 p;attribute vec2 uv;attribute vec3 normal;uniform mat4 mvp;varying vec2 vuv;varying vec3 n;void main(){vuv=uv;n=normal;gl_Position=mvp*vec4(p,1.0);}';
  const fragment = 'precision mediump float;uniform sampler2D atlas;uniform bool mask;uniform bool lines;varying vec2 vuv;varying vec3 n;void main(){if(lines){gl_FragColor=vec4(.93,.42,.12,1.);return;}vec4 t=texture2D(atlas,vuv);if(mask && t.a<.4)discard;float d=abs(dot(normalize(n),normalize(vec3(-.25,-.4,.88))));float s=d>.7?1.:d>.35?.79:.55;vec3 c=mask?t.rgb:vec3(.72,.76,.70);gl_FragColor=vec4(c*s,1.);}';
  function shader(type, code) { const s=gl.createShader(type);gl.shaderSource(s,code);gl.compileShader(s);if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw Error(gl.getShaderInfoLog(s));return s; }
  const program=gl.createProgram();gl.attachShader(program,shader(gl.VERTEX_SHADER,vertex));gl.attachShader(program,shader(gl.FRAGMENT_SHADER,fragment));gl.linkProgram(program);
  if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(program));
  gl.useProgram(program);
  const loc={p:gl.getAttribLocation(program,'p'),uv:gl.getAttribLocation(program,'uv'),normal:gl.getAttribLocation(program,'normal'),mvp:gl.getUniformLocation(program,'mvp'),mask:gl.getUniformLocation(program,'mask'),lines:gl.getUniformLocation(program,'lines')};
  const buffer=gl.createBuffer(),lineBuffer=gl.createBuffer(),texture=gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D,texture);
  gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,1,1,0,gl.RGBA,gl.UNSIGNED_BYTE,new Uint8Array([145,183,71,255]));
  gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.LINEAR);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.LINEAR);
  const img=new Image();
  img.onload=()=>{gl.bindTexture(gl.TEXTURE_2D,texture);gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL,true);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,gl.RGBA,gl.UNSIGNED_BYTE,img);canvas.dataset.texture='ready';draw();};
  img.onerror=()=>{info.textContent='技术图集读取失败。请通过本地预览入口打开。';};
  img.src='Design/Foliage_Atlas_Sample_v3.svg';
  let data=[],lineData=[],cards=0,triangleCount=0;
  const sub=(a,b)=>a.map((x,i)=>x-b[i]);
  const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
  const norm=a=>{const d=Math.hypot(...a)||1;return a.map(x=>x/d);};
  function card(rows,tile,upward=false) {
    cards++;
    const col=tile%4,row=Math.floor(tile/4),g=.027;
    function v(pos,u,t,n) { return [...pos,(col+g+u*(1-2*g))/4,1-(row+1-g-t*(1-2*g))/2,...n]; }
    for(let j=0;j<rows.length-1;j++){
      const a=rows[j][0],b=rows[j][1],c=rows[j+1][0],d=rows[j+1][1];
      let n=norm(cross(sub(b,a),sub(c,a)));if(upward)n=norm([n[0]*.2,n[1]*.2,1]);
      const aa=v(a,0,j/(rows.length-1),n),bb=v(b,1,j/(rows.length-1),n),cc=v(c,0,(j+1)/(rows.length-1),n),dd=v(d,1,(j+1)/(rows.length-1),n);
      data.push(...aa,...bb,...cc,...bb,...dd,...cc);
      lineData.push(...aa,...bb,...bb,...cc,...cc,...aa,...bb,...dd,...dd,...cc);
      triangleCount+=2;
    }
  }
  function upright(x,y,angle,width,height,tile,segments=2,bend=.16) {
    const cs=Math.cos(angle),sn=Math.sin(angle),rows=[];
    for(let i=0;i<=segments;i++){const t=i/segments,off=bend*t*t;rows.push([[-width/2*cs+x-off*sn,-width/2*sn+y+off*cs,height*t],[width/2*cs+x-off*sn,width/2*sn+y+off*cs,height*t]]);}
    card(rows,tile,true);
  }
  function build(){
    data=[];lineData=[];cards=0;triangleCount=0;
    if(picker.value==='grass'){
      for(let i=0;i<6;i++){const a=i*Math.PI/3;upright(Math.cos(a)*.33,Math.sin(a)*.33,a+.31,.9,1.6+(i%3)*.11,0,2,.23);}
    }else if(picker.value==='flower'){
      upright(0,0,0,.50,1.32,3,1,.035);upright(0,0,Math.PI/2,.50,1.32,3,1,.035);
      card([[[-.48,-.34,1.11],[.48,-.34,1.11]],[[-.48,0,1.46],[.48,0,1.46]],[[-.48,.34,1.78],[.48,.34,1.78]]],1);
    }else if(picker.value==='fern'){
      for(let i=0;i<5;i++){const a=i*Math.PI*2/5;const cs=Math.cos(a),sn=Math.sin(a),rows=[];
        for(let j=0;j<=2;j++){const t=j/2,r=.1+1.4*t,z=.12+.8*Math.sin(t*Math.PI*.65);rows.push([[cs*r-sn*.37,sn*r+cs*.37,z],[cs*r+sn*.37,sn*r-cs*.37,z]]);}card(rows,2);
      }
    }else{
      for(let i=0;i<4;i++){const x=(i%2-.5)*1.08,y=(Math.floor(i/2)-.5)*1.08;
        card([[[x-.52,y-.48,.08],[x+.52,y-.48,.08]],[[x-.52,y,.19],[x+.52,y,.19]],[[x-.52,y+.48,.12],[x+.52,y+.48,.12]]],4,true);
      }
    }
    gl.bindBuffer(gl.ARRAY_BUFFER,buffer);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(data),gl.STATIC_DRAW);
    gl.bindBuffer(gl.ARRAY_BUFFER,lineBuffer);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array(lineData),gl.STATIC_DRAW);
    canvas.dataset.triangles=String(triangleCount);canvas.dataset.cards=String(cards);
    info.textContent=cards+' 张卡片 · '+triangleCount+' 个三角面（本页示意网格实数，非最终模型统计）';
    draw();
  }
  function bind(b) {
    gl.bindBuffer(gl.ARRAY_BUFFER,b);
    [[loc.p,3,0],[loc.uv,2,12],[loc.normal,3,20]].forEach(([l,n,o])=>{gl.enableVertexAttribArray(l);gl.vertexAttribPointer(l,n,gl.FLOAT,false,32,o);});
  }
  let yaw=.75;
  function draw() {
    const width=Math.round(canvas.clientWidth*Math.min(devicePixelRatio,2)),height=Math.round(canvas.clientHeight*Math.min(devicePixelRatio,2));
    if(canvas.width!==width||canvas.height!==height){canvas.width=width;canvas.height=height;}
    gl.viewport(0,0,width,height);gl.clearColor(.84,.86,.82,1);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);gl.enable(gl.DEPTH_TEST);gl.disable(gl.CULL_FACE);
    const pitch=view.value==='top'?55*Math.PI/180:30*Math.PI/180,ca=Math.cos(yaw),sa=Math.sin(yaw),cp=Math.cos(pitch),sp=Math.sin(pitch),aspect=width/height;
    const right=[ca,-sa,0],up=[sa*sp,ca*sp,cp],back=[-sa*cp,-ca*cp,sp];
    const dot=(a,b)=>a.reduce((s,x,i)=>s+x*b[i],0);
    let minX=Infinity,maxX=-Infinity,minY=Infinity,maxY=-Infinity;
    for(let i=0;i<data.length;i+=8){const p=data.slice(i,i+3),x=dot(right,p),y=dot(up,p);minX=Math.min(minX,x);maxX=Math.max(maxX,x);minY=Math.min(minY,y);maxY=Math.max(maxY,y);}
    const scale=Math.min(2*aspect/(maxX-minX+.3),2/(maxY-minY+.3)),cx=(minX+maxX)/2,cy=(minY+maxY)/2;
    const matrix=new Float32Array([right[0]*scale/aspect,up[0]*scale,-back[0]/8,0,right[1]*scale/aspect,up[1]*scale,-back[1]/8,0,right[2]*scale/aspect,up[2]*scale,-back[2]/8,0,-cx*scale/aspect,-cy*scale,0,1]);
    gl.uniformMatrix4fv(loc.mvp,false,matrix);gl.uniform1i(loc.mask,masked.checked?1:0);gl.uniform1i(loc.lines,0);bind(buffer);
    if(wire.checked){gl.enable(gl.POLYGON_OFFSET_FILL);gl.polygonOffset(1,1);}
    gl.drawArrays(gl.TRIANGLES,0,data.length/8);gl.disable(gl.POLYGON_OFFSET_FILL);
    if(wire.checked){gl.uniform1i(loc.lines,1);bind(lineBuffer);gl.drawArrays(gl.LINES,0,lineData.length/8);}
  }
  picker.addEventListener('change',build);masked.addEventListener('change',draw);wire.addEventListener('change',draw);view.addEventListener('change',draw);
  let dragX=null;canvas.addEventListener('pointerdown',e=>{dragX=e.clientX;canvas.setPointerCapture(e.pointerId);});
  canvas.addEventListener('pointermove',e=>{if(dragX!==null){yaw+=(e.clientX-dragX)*.007;dragX=e.clientX;draw();}});
  canvas.addEventListener('pointerup',()=>{dragX=null;});canvas.addEventListener('pointercancel',()=>{dragX=null;});
  new ResizeObserver(draw).observe(canvas);
  build();
})();
