const byId = id => document.getElementById(id);
const building = byId('building');
const feature = byId('feature');
const previous = byId('previous');
const next = byId('next');
const mode = byId('mode');
const guides = byId('guides');
const notes = byId('notes');
const status = byId('status');
const sources = new Map();
let update = 0;

function load(url) {
  if (!sources.has(url)) {
    const pending = new Promise((resolve, reject) => {
      const image = new Image();
      image.onload = () => resolve(image);
      image.onerror = () => reject(new Error(url));
      image.src = url;
    });
    sources.set(url, pending);
    pending.catch(() => sources.delete(url));
  }
  return sources.get(url);
}

function cropRegion(rect, zoom) {
  if (!zoom) return rect;
  const [x0, y0, x1, y1] = rect;
  const insetX = (x1 - x0) * .16;
  const insetY = (y1 - y0) * .16;
  return [x0 + insetX, y0 + insetY, x1 - insetX, y1 - insetY];
}

function paint(canvas, image, rect, coordinateScale, paths, points, ink) {
  const density = Math.min(window.devicePixelRatio || 1, 2);
  canvas.width = Math.max(1, Math.round(canvas.clientWidth * density));
  canvas.height = Math.max(1, Math.round(canvas.clientHeight * density));
  const [x0, y0, x1, y1] = rect;
  const width = x1 - x0;
  const height = y1 - y0;
  const scale = Math.min(canvas.width / width, canvas.height / height);
  const left = (canvas.width - width * scale) / 2;
  const top = (canvas.height - height * scale) / 2;
  const context = canvas.getContext('2d');
  context.imageSmoothingQuality = 'high';
  context.drawImage(image, x0 * coordinateScale, y0 * coordinateScale,
    width * coordinateScale, height * coordinateScale, left, top, width * scale, height * scale);
  if (!guides.checked) return;

  context.save();
  context.beginPath();
  context.rect(left, top, width * scale, height * scale);
  context.clip();
  context.strokeStyle = ink;
  context.lineWidth = 1.4 * density;
  context.fillStyle = ink;
  for (const path of paths) {
    context.beginPath();
    path.forEach(([x, y], index) => {
      const u = left + (x - x0) * scale;
      const v = top + (y - y0) * scale;
      if (index === 0) context.moveTo(u, v);
      else context.lineTo(u, v);
    });
    context.stroke();
  }
  for (const [x, y] of points) {
    context.beginPath();
    context.arc(left + (x - x0) * scale, top + (y - y0) * scale, 2.2 * density, 0, Math.PI * 2);
    context.fill();
  }
  context.restore();
}

function refreshNotes(model, zone) {
  notes.replaceChildren();
  const description = document.createElement('p');
  description.textContent = `ModelId ${model.id} · LOD0 ${model.triangles.toLocaleString('zh-CN')} 三角面 · ${zone.name}`;
  notes.append(description);
  if (guides.checked) {
    const legend = document.createElement('p');
    legend.textContent = zone.guides.length
      ? '琥珀线标出 A5 笔画，青线标出对应的模型原边，青点标出已回读的表面贴图采样点。'
      : '当前为关键部件放大图。选择具体分件可查看定位线。';
    notes.append(legend);
  }
  for (const detail of zone.differences) {
    const message = document.createElement('p');
    message.className = 'difference';
    message.textContent = detail;
    notes.append(message);
  }
}

async function draw() {
  const token = ++update;
  const model = REVIEW_DATA.models[Number(building.value)];
  const index = Number(feature.value);
  const zone = model.features[index];
  const whole = mode.value === 'full';
  const zoom = mode.value === 'zoom';
  previous.disabled = index === 0;
  next.disabled = index === model.features.length - 1;
  refreshNotes(model, zone);
  const anchor = `#${model.key}/${index}`;
  if (window.location.hash !== anchor) window.history.replaceState(null, '', anchor);
  const sourceUrl = `../../References_FeatureConcepts_A5/${model.source}`;
  const modelBlueUrl = model.model_images.Blue.file;
  const modelRedUrl = model.model_images.Red.file;
  const [sourceWidth, sourceHeight] = model.source_size;
  const redOffset = model.red_source_offset;
  const blueSourceCrop = whole ? [0, 0, redOffset, sourceHeight] : cropRegion(zone.source_crop, zoom);
  const redSourceCrop = whole ? [redOffset, 0, sourceWidth, sourceHeight] : cropRegion(zone.source_red_crop, zoom);
  const modelCrop = whole ? [0, 0, ...REVIEW_DATA.model_size] : cropRegion(zone.model_crop, zoom);
  const sourcePaths = zone.guides.map(path => path.source);
  const redPaths = sourcePaths.map(path => path.map(([x, y]) => [x + redOffset, y]));
  const edgePaths = zone.guides.flatMap(path => path.edges);
  const texelPoints = zone.guides.flatMap(path => path.texture);
  const panels = [
    ['sourceBlue', sourceUrl, blueSourceCrop, 1, sourcePaths, [], '#E4964E'],
    ['sourceRed', sourceUrl, redSourceCrop, 1, redPaths, [], '#E4964E'],
    ['modelBlue', modelBlueUrl, modelCrop, 2, edgePaths, texelPoints, '#2CD4D4'],
    ['modelRed', modelRedUrl, modelCrop, 2, edgePaths, texelPoints, '#2CD4D4'],
  ];
  for (const [id, url] of panels) byId(`${id}Link`).href = url;
  try {
    const images = await Promise.all(panels.map(([, url]) => load(url)));
    if (token !== update) return;
    panels.forEach(([id, , rect, scale, paths, points, ink], i) => {
      paint(byId(id), images[i], rect, scale, paths, points, ink);
    });
    status.textContent = '';
  } catch (error) {
    if (token === update) status.textContent = `图像未能加载 ${error.message}`;
  }
}

function options(select, entries) {
  select.replaceChildren(...entries.map((entry, index) => new Option(entry.name, index)));
}

function setBuilding(index, zone = 0) {
  building.value = index;
  const model = REVIEW_DATA.models[index];
  options(feature, model.features);
  feature.value = zone < model.features.length ? zone : 0;
  draw();
}

function followHash() {
  const [key, selection] = window.location.hash.slice(1).split('/');
  const index = REVIEW_DATA.models.findIndex(model => model.key === key);
  const selected = Number(selection);
  setBuilding(index >= 0 ? index : 3, Number.isInteger(selected) && selected >= 0 ? selected : 0);
}

options(building, REVIEW_DATA.models);
building.addEventListener('change', () => setBuilding(Number(building.value)));
for (const select of [feature, mode, guides]) select.addEventListener('change', draw);
previous.addEventListener('click', () => { feature.value = Number(feature.value) - 1; draw(); });
next.addEventListener('click', () => { feature.value = Number(feature.value) + 1; draw(); });
window.addEventListener('hashchange', followHash);
window.addEventListener('resize', draw);
followHash();
