"use strict";
const assert = require("node:assert/strict");
const {createHash} = require("node:crypto");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const {spawn} = require("node:child_process");
const {setTimeout: sleep} = require("node:timers/promises");

const base = "http://127.0.0.1:8195/";
const tmp = fs.realpathSync(os.tmpdir());
const profile = fs.mkdtempSync(path.join(tmp, "guli-card-v7-check-"));
const chrome = spawn("C:/Program Files/Google/Chrome/Application/chrome.exe", [
  "--headless", "--disable-gpu", "--no-first-run", "--no-default-browser-check",
  "--remote-debugging-port=0", "--window-size=1440,1100", "--user-data-dir=" + profile, "about:blank",
], {windowsHide: true, stdio: "ignore"});
const exitChrome = new Promise(resolve => chrome.once("exit", resolve));
let socket;

async function until(check, times = 160) {
  for (let i = 0; i < times; i++) {
    try {
      const value = await check();
      if (value) return value;
    } catch (error) {
      if (!["EBUSY", "ENOENT", "ECONNREFUSED"].includes(error.code ?? error.cause?.code)) throw error;
    }
    await sleep(75);
  }
  throw new Error("Timeout waiting for state");
}
async function channel() {
  const portFile = path.join(profile, "DevToolsActivePort");
  const port = await until(() => fs.existsSync(portFile) && Number(fs.readFileSync(portFile, "utf8").split("\n")[0]));
  const [page] = await until(async () => {
    const response = await fetch("http://127.0.0.1:" + port + "/json/list");
    const tabs = (await response.json()).filter(tab => tab.type === "page");
    return tabs.length ? tabs : false;
  });
  socket = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => {
    socket.addEventListener("open", resolve, {once: true});
    socket.addEventListener("error", reject, {once: true});
  });
  const requests = new Map();
  let id = 0;
  socket.addEventListener("message", ({data}) => {
    const msg = JSON.parse(data);
    const req = requests.get(msg.id);
    if (req) {
      requests.delete(msg.id);
      if (msg.error) req.reject(new Error(JSON.stringify(msg.error)));
      else req.resolve(msg.result);
    }
  });
  return (method, params = {}) => new Promise((resolve, reject) => {
    const key = ++id;
    requests.set(key, {resolve, reject});
    socket.send(JSON.stringify({id:key, method, params}));
  });
}

async function verifyAssets(expected) {
  for (const link of expected) {
    const response = await fetch(base + link.file);
    assert.equal(response.status, 200, link.file);
    const got = createHash("sha256").update(Buffer.from(await response.arrayBuffer())).digest("hex");
    assert.equal(got, link.sha256, link.file);
  }
}

async function main() {
  const checks={tested_at:new Date().toISOString(),result:'passed',visual_validation:false,cards:{}};
  const cdp = await channel();
  await cdp("Page.enable");
  await cdp("Emulation.setEmulatedMedia", {features:[{name:"prefers-reduced-motion", value:"reduce"}]});
  await cdp("Page.addScriptToEvaluateOnNewDocument", {source: "(" + function () {
    const ctx = CanvasRenderingContext2D.prototype;
    const clear = ctx.clearRect;
    const draw = ctx.drawImage;
    window.captureChannels = () => {
      const canvas = document.querySelector("#art");
      return canvas.getContext("2d").getImageData(0,0,canvas.width,canvas.height).data;
    };
    window.measureSsim = (original) => {
      const canvas = document.querySelector("#art");
      const pixels = window.captureChannels();
      const w = canvas.width, h = canvas.height;
      const kernel = Array.from({length:11}, (_,i)=>Math.exp(-.5*((i-5)/1.5)**2));
      const normalizer = kernel.reduce((a,b)=>a+b,0);
      for (let i=0;i<kernel.length;i++) kernel[i]/=normalizer;
      const mirror = (x,n) => x<0?-1-x:x>=n?2*n-x-1:x;
      const p = new Float64Array(w*h*5);
      const c1 = .01**2, c2 = .03**2;
      let total = 0;
      for (let ch=0;ch<3;ch++) {
        for(let y=0;y<h;y++) for(let x=0;x<w;x++) {
          const at=(y*w+x)*5;
          let a=0,b=0,aa=0,bb=0,ab=0;
          for(let k=0;k<11;k++) {
            const px=(y*w+mirror(x+k-5,w))*4+ch;
            const ca=original[px]/255, cb=pixels[px]/255, weight=kernel[k];
            a+=ca*weight; b+=cb*weight; aa+=ca*ca*weight; bb+=cb*cb*weight; ab+=ca*cb*weight;
          }
          p[at]=a; p[at+1]=b; p[at+2]=aa; p[at+3]=bb; p[at+4]=ab;
        }
        for(let y=0;y<h;y++) for(let x=0;x<w;x++) {
          let a=0,b=0,aa=0,bb=0,ab=0;
          for(let k=0;k<11;k++) {
            const at=(mirror(y+k-5,h)*w+x)*5, weight=kernel[k];
            a+=p[at]*weight; b+=p[at+1]*weight; aa+=p[at+2]*weight;
            bb+=p[at+3]*weight; ab+=p[at+4]*weight;
          }
          total+=((2*a*b+c1)*(2*(ab-a*b)+c2))/
            ((a*a+b*b+c1)*(aa+bb-a*a-b*b+c2));
        }
      }
      return total/(3*w*h);
    };
    let draws = [];
    ctx.clearRect = function (...args) {
      if (this.canvas.id === "art") draws = [];
      return clear.apply(this, args);
    };
    ctx.drawImage = function (image, x, y, ...args) {
      if (this.canvas.id === "art") {
        draws.push({url:new URL(image.src).pathname.replace(/^[/]/, ""), revision:new URL(image.src).search,
                    x,y,w:image.naturalWidth,h:image.naturalHeight,alpha:this.globalAlpha});
      }
      return draw.call(this, image, x, y, ...args);
    };
    window.diagnostics = async function(cb) {
      if (cb) cb();
      await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
      const canvas = document.querySelector("#art");
      return {size:[canvas.width,canvas.height],draws,styles:document.querySelector("#card").style.transform,
              download:[...document.querySelectorAll("#layers a")].map(a => new URL(a.href).pathname.slice(1))};
    };
  } + ")();"});
  const evaluate = async expression => {
    const result = await cdp("Runtime.evaluate", {expression,awaitPromise:true,returnByValue:true});
    if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
  };
  for (const card of ["NoSurvivors","ExplosivePackage"]) {
    const report = JSON.parse(fs.readFileSync("manifest_v7.json", "utf8")).cards[card];
    const expected = report.layers.map(layer => ({file:layer.file,sha256:layer.export_sha256,depth:layer.depth,size:layer.size}));
    await verifyAssets(expected);
    await cdp("Page.navigate", {url:base+"?card="+card+"&v=7-edge-2"});
    await until(async () => await evaluate("new URLSearchParams(location.search).get('card')===" + JSON.stringify(card) + " && document.querySelector('#status')?.dataset.state==='ready'"));
    let state = await evaluate("diagnostics()");
    assert.deepEqual(state.size,[1024,1536]);
    assert.deepEqual(state.download,expected.map(row => row.file));
    assert.deepEqual(state.draws.map(row => row.url),expected.map(row => row.file).reverse());
    assert(state.draws.every(row => row.x===-32 && row.y===-48 && row.alpha===1 && row.revision==="?v=7-edge-2"));
    const coreDifference = await evaluate(`(async () => {
      const oldCanvas = document.createElement('canvas');
      oldCanvas.width=1024; oldCanvas.height=1536;
      const oldCtx=oldCanvas.getContext('2d');
      for (const file of ${JSON.stringify(report.layers.map(layer=>layer.base_v6).reverse())}) {
        const texture=new Image(); texture.src=file;
        await texture.decode(); oldCtx.drawImage(texture,0,0);
      }
      const before=oldCtx.getImageData(0,0,1024,1536).data;
      const after=captureChannels(); let changedPixels=0;
      for(let p=0;p<before.length;p+=4) {
        if(before[p]!==after[p] || before[p+1]!==after[p+1] || before[p+2]!==after[p+2] || before[p+3]!==after[p+3]) changedPixels++;
      }
      return changedPixels;
    })()`);
    assert.equal(coreDifference,0,'zero-tilt browser composite must exactly retain the v6 core');
    state = await evaluate('diagnostics(()=>document.querySelector("[data-mode-choice=reference]").click())');
    assert.deepEqual(state.draws.map(row=>row.url),[card+"/Master.png"]);
    await evaluate("window.originalColour = captureChannels().slice(); true");
    state=await evaluate('diagnostics(()=>document.querySelector("[data-mode-choice=stack]").click())');
    const score=await evaluate("measureSsim(window.originalColour)");
    assert(Number.isFinite(score));
    const oldMeasurement=JSON.parse(fs.readFileSync(card+"/browser_measurement_v6.json","utf8"));
    assert(Math.abs(score-oldMeasurement.browserRgbSsim)<1e-12);
    fs.writeFileSync(card+"/browser_measurement_v7.json", JSON.stringify({card,browserRgbSsim:score,minimum:.95,pass:score>=.95,coreChangedPixelsVsV6:coreDifference,original_sha256:report.source_sha256,layer_hashes:expected.map(row=>row.sha256)},null,2)); console.log(JSON.stringify({card,browserRgbSsim:score,coreChangedPixelsVsV6:coreDifference}));
    state = await evaluate('diagnostics(()=>document.querySelector("[data-mode-choice=parallax]").click())');
    const setParallax = 'const strength = document.querySelector("#strength"); strength.value=100;strength.dispatchEvent(new Event("input"));';
    const corners = [[-1,-1],[1,-1],[-1,1],[1,1]];
    const move = (x,y) => 'const c=document.querySelector("#motion-area");const r=c.getBoundingClientRect();c.dispatchEvent(new PointerEvent("pointermove",{pointerType:"mouse",clientX:r.left+r.width/2+r.width*'+x+',clientY:r.top+r.height/2+r.height*'+y+'}));';
    const setEnabled = (index, enabled) => 'const b=document.querySelector(\'[data-layer="'+index+'"] input[type=checkbox]\');b.checked='+enabled+';b.dispatchEvent(new Event("change"));';
    for (const showSky of [true,false]) {
      for (const [x,y] of corners) {
        state = await evaluate('diagnostics(()=>{'+setParallax+setEnabled(6,showSky)+move(x,y)+'})');
        const order = expected.slice(0,showSky?6:5).reverse();
        assert.deepEqual(state.draws.map(row=>row.url),order.map(row=>row.file));
        state.draws.forEach((row,i) => {
          assert(Math.abs(row.x-(-32+x*24*order[i].depth))<1e-8);
          assert(Math.abs(row.y-(-48+y*30*order[i].depth))<1e-8);
          assert(row.x<=-8 && row.y<=-18 && row.x+row.w>=1032 && row.y+row.h>=1554,
                 'maximum travel must keep the PNG boundary outside the visible window');
        });
      }
    }
    for (let i=1;i<=6;i++) {
      for (const [x,y] of corners) {
        const solo = 'const b = document.querySelector(\'[data-layer="'+i+'"] button\');if(b.getAttribute("aria-pressed")==="false")b.click();';
        state = await evaluate('diagnostics(()=>{'+solo+move(x,y)+'})');
        assert.deepEqual(state.size,[1088,1632]);
        assert.equal(state.draws.length,1);
        const [row]=state.draws;
        assert.deepEqual([row.url,row.x,row.y,row.w,row.h],[expected[i-1].file,0,0,...expected[i-1].size]);
      }
    }
    for (let i=1;i<=6;i++) {
      state = await evaluate('diagnostics(()=>{const b=document.querySelector(\'[data-layer="'+i+'"] button\');if(b.getAttribute("aria-pressed")==="false")b.click();const slider=document.querySelector(\'[data-layer="'+i+'"] input[type=range]\');slider.value=50;slider.dispatchEvent(new Event("input"));})');
      assert.equal(state.draws.length,1);
      assert.equal(state.draws[0].url,expected[i-1].file);
      assert.equal(state.draws[0].alpha,.5);
    }
    checks.cards[card]={layer_hashes:expected.map(row=>row.sha256),core_changed_pixels:coreDifference,
      stack_corner_states:8,solo_corner_states:24,opacity_controls:6,
      asset_hashes:'passed',single_draw_per_selected_layer:'passed',
      texture_bounds:'passed',minimum_boundary_margin_px:[8,18]};
    console.log(card,"asset hashes, exact v6 core retention, draw sources/counts, layer toggles, 8 stack corner states with texture bounds, 24 solo corner states and 6 opacity controls passed; no visual validation");
  }
  fs.writeFileSync('technical_checks_v7.json',JSON.stringify(checks,null,2));
}
main().catch(error => { console.error(error); process.exitCode=1; }).finally(async () => {
  socket?.close();
  chrome.kill();
  await Promise.race([exitChrome,sleep(2500)]);
  if (path.dirname(profile) === tmp && path.basename(profile).startsWith("guli-card-v7-check-")) {
    try {
      fs.rmSync(profile,{recursive:true,force:true,maxRetries:3,retryDelay:120});
    } catch (error) {
      console.warn("Profile cleanup:",error.message);
    }
  }
});

