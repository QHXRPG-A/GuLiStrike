"use strict";

const requestedCard = new URLSearchParams(window.location.search).get("card");
const cardId = Object.hasOwn(window.cardCollection, requestedCard) ? requestedCard : "NoSurvivors";
const data = window.cardCollection[cardId];
document.title = data.name + " · 六层牌面";
document.querySelector("#card-title").textContent = data.name;
document.querySelector('[data-card="' + cardId + '"]').setAttribute("aria-current", "page");
const similarity = data.reference_similarity;
const similarityNote = similarity.value >= similarity.minimum ? "" : " · 未达 95%";
document.querySelector("#similarity").textContent =
  "原位叠合结构相似度 " + (similarity.value * 100).toFixed(3) + "%" + similarityNote;
const showProductionStatus = ["edge-review", "done"].includes(data.status);
document.querySelector("#production-status").hidden = !showProductionStatus;
if (data.bundle) {
  const download = document.querySelector("#card-bundle");
  download.href = data.bundle;
  download.textContent = "下载六层素材包";
}
const workspace = document.querySelector(".workspace");
const surface = document.querySelector("#motion-area");
const card = document.querySelector("#card");
const canvas = document.querySelector("#art");
const ctx = canvas.getContext("2d");
const loadStatus = document.querySelector("#status");
const hint = document.querySelector("#hint");
const strength = document.querySelector("#strength");
const strengthValue = document.querySelector("#strength-value");
const choices = [...document.querySelectorAll("[data-mode-choice]")];
const [cropLeft, cropTop, cropRight, cropBottom] = data.window;
const [maxX, maxY] = data.travel_px;
const layers = new Map(data.layers.map(layer => [layer.index, {
  ...layer, enabled: true, opacity: 1,
}]));
const reducedMotion = matchMedia("(prefers-reduced-motion: reduce)");
const hints = {
  reference: "原始参考图。切换到原位叠合可比较六层装配。",
  stack: "六层原位叠合。可隐藏或单独查看任意一层。",
  parallax: "移动鼠标或拖动牌面，检查外沿补绘与遮挡处的接合。",
};

canvas.width = cropRight - cropLeft;
canvas.height = cropBottom - cropTop;
document.querySelector("#canvas-size").textContent =
  "共用画布 " + data.canvas.join(" × ") + " · 原图窗口 " + canvas.width + " × " + canvas.height;
let reference;
let splitPasses;
let ready = false;
let solo = null;
let mode = "stack";
let frame = 0;
let dragPointer = null;
const target = {x: 0, y: 0};
const position = {x: 0, y: 0};

function assetURL(path) {
  return `${path}?v=${data.revision}`;
}

function image(path) {
  return new Promise((resolve, reject) => {
    const texture = new Image();
    texture.onload = () => resolve(texture);
    texture.onerror = () => reject(new Error(path));
    texture.src = assetURL(path);
  });
}

function applyMask(texture, mask, front) {
  const pass = document.createElement("canvas");
  [pass.width, pass.height] = data.canvas;
  const painter = pass.getContext("2d");
  painter.drawImage(texture, 0, 0);
  painter.globalCompositeOperation = front ? "destination-in" : "destination-out";
  painter.drawImage(mask, 0, 0);
  return pass;
}

function draw() {
  if (!ready) return;
  const visible = solo === null
    ? new Set([...layers.values()].filter(layer => layer.enabled).map(layer => layer.index))
    : new Set([solo]);
  const wholeLayer = mode !== "reference" && visible.size === 1;
  const [width, height] = wholeLayer ? data.canvas : [cropRight - cropLeft, cropBottom - cropTop];
  if (canvas.width !== width) canvas.width = width;
  if (canvas.height !== height) canvas.height = height;
  ctx.clearRect(0, 0, width, height);
  ctx.imageSmoothingQuality = "high";
  hint.textContent = wholeLayer ? "单层全画布，包含生成的外沿。" : hints[mode];
  const move = mode === "parallax" ? Number(strength.value) / 100 : 0;
  const x = position.x * move;
  const y = position.y * move;
  card.style.transform = `rotateX(${-y * 10}deg) rotateY(${x * 10}deg)`;
  if (mode === "reference") {
    if (data.background) {
      ctx.fillStyle = data.background;
      ctx.fillRect(0, 0, width, height);
    }
    ctx.drawImage(reference, 0, 0);
    return;
  }
  for (const pass of wholeLayer ? visible : data.draw_order) {
    const index = typeof pass === "number" ? pass : data.split_layer.index;
    if (!visible.has(index)) continue;
    const layer = layers.get(index);
    ctx.globalAlpha = layer.opacity;
    const left = wholeLayer ? 0 : -cropLeft + x * maxX * layer.depth;
    const top = wholeLayer ? 0 : -cropTop + y * maxY * layer.depth;
    const texture = typeof pass === "number" ? layer.texture : splitPasses[pass];
    ctx.drawImage(texture, left, top, data.canvas[0], data.canvas[1]);
  }
  ctx.globalAlpha = 1;
}

function animate() {
  frame = 0;
  const easing = reducedMotion.matches ? 1 : 0.23;
  const remaining = Math.max(Math.abs(target.x - position.x), Math.abs(target.y - position.y));
  if (remaining < 0.002) {
    Object.assign(position, target);
  } else {
    position.x += (target.x - position.x) * easing;
    position.y += (target.y - position.y) * easing;
    schedule();
  }
  draw();
}

function schedule() {
  if (!frame) frame = requestAnimationFrame(animate);
}

function recenter(immediate = false) {
  target.x = 0;
  target.y = 0;
  if (immediate) {
    position.x = 0;
    position.y = 0;
  }
  schedule();
}

function setMode(next) {
  mode = next;
  workspace.dataset.mode = mode;
  for (const button of choices) {
    button.setAttribute("aria-pressed", String(button.dataset.modeChoice === mode));
  }
  strength.disabled = mode !== "parallax";
  hint.textContent = hints[mode];
  recenter(true);
}

function refreshSolo() {
  for (const layer of layers.values()) {
    const active = layer.index === solo;
    layer.soloButton.textContent = active ? "退出独看" : "独看";
    layer.soloButton.setAttribute("aria-pressed", String(active));
  }
  schedule();
}

function setTarget(event) {
  if (mode !== "parallax" || (event.pointerType !== "mouse" && dragPointer !== event.pointerId)) return;
  const rect = surface.getBoundingClientRect();
  const x = (event.clientX - rect.left - rect.width / 2) * 2 / card.offsetWidth;
  const y = (event.clientY - rect.top - rect.height / 2) * 2 / card.offsetHeight;
  target.x = Math.max(-1, Math.min(1, x));
  target.y = Math.max(-1, Math.min(1, y));
  schedule();
}

function layerControl(layer) {
  const row = document.createElement("li");
  row.className = "layer";
  row.dataset.layer = String(layer.index);
  row.innerHTML = `
    <div class="layer-main">
      <label>
        <input type="checkbox" checked>
        <span class="layer-index">${String(layer.index).padStart(2, "0")}</span>
        <span class="layer-name"></span>
      </label>
      <button type="button" aria-pressed="false">独看</button>
      <a class="download" download>PNG</a>
    </div>
    <div class="opacity">
      <input type="range" min="0" max="100" value="100">
      <output>100%</output>
    </div>`;
  row.querySelector(".layer-name").textContent = layer.name;
  const enabled = row.querySelector("input[type=checkbox]");
  const opacity = row.querySelector("input[type=range]");
  const output = row.querySelector("output");
  const download = row.querySelector(".download");
  download.href = assetURL(layer.file);
  download.download = layer.file.split("/").at(-1);
  download.setAttribute("aria-label", `下载${layer.name} PNG`);
  opacity.setAttribute("aria-label", `${layer.name}不透明度`);
  layer.soloButton = row.querySelector("button");
  enabled.addEventListener("change", () => {
    layer.enabled = enabled.checked;
    if (mode === "reference") setMode("stack");
    if (solo !== null) {
      solo = null;
      refreshSolo();
    }
    schedule();
  });
  opacity.addEventListener("input", () => {
    layer.opacity = Number(opacity.value) / 100;
    output.textContent = `${opacity.value}%`;
    if (mode === "reference") setMode("stack");
    schedule();
  });
  layer.soloButton.addEventListener("click", () => {
    solo = solo === layer.index ? null : layer.index;
    if (mode === "reference") setMode("stack");
    refreshSolo();
  });
  return row;
}

document.querySelector("#layers").append(...Array.from(layers.values(), layerControl));
for (const button of choices) {
  button.addEventListener("click", () => setMode(button.dataset.modeChoice));
}
document.querySelector("#reset").addEventListener("click", () => recenter(true));
strength.addEventListener("input", () => {
  strengthValue.textContent = `${strength.value}%`;
  schedule();
});
surface.addEventListener("pointermove", setTarget);
surface.addEventListener("pointerdown", event => {
  if (mode !== "parallax") return;
  dragPointer = event.pointerId;
  surface.setPointerCapture(event.pointerId);
  setTarget(event);
});
surface.addEventListener("pointerleave", () => {
  if (dragPointer === null) recenter();
});
surface.addEventListener("lostpointercapture", () => {
  dragPointer = null;
  recenter();
});

async function init() {
  try {
    const layerImages = Array.from(layers.values(), async layer => {
      layer.texture = await image(layer.file);
      if (layer.texture.naturalWidth !== layer.size[0] || layer.texture.naturalHeight !== layer.size[1]) {
        throw new Error(layer.file);
      }
    });
    [reference] = await Promise.all([image(data.source), ...layerImages]);
    if (data.split_layer) {
      const mask = await image(data.split_layer.mask);
      const texture = layers.get(data.split_layer.index).texture;
      splitPasses = {
        "3-back": applyMask(texture, mask, false),
        "3-front": applyMask(texture, mask, true),
      };
    }
    ready = true;
    loadStatus.dataset.state = "ready";
    loadStatus.textContent = edgeReview ? "6 层已载入 · 外沿补绘版" : "6 层已载入";
    draw();
  } catch (error) {
    loadStatus.dataset.state = "error";
    loadStatus.textContent = `载入失败 · ${error.message}`;
    console.error(error);
  }
}

init();

