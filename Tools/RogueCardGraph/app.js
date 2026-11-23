"use strict";

const $ = (id) => document.getElementById(id);
const canvas = $("canvas");
const nodeLayer = $("nodes");
const edgeLayer = $("edges");
const preview = $("link-preview");
const inspector = $("inspector");
const dialog = $("card-dialog");
const tabs = [...document.querySelectorAll("[data-seat]")];
const modeButtons = [...document.querySelectorAll("[data-mode][aria-pressed]")];
const seatNames = { Commander: "指挥官", Ground: "地面", Air: "空中" };
const nodeWidth = parseFloat(getComputedStyle(canvas).getPropertyValue("--node-width"));
const nodeHeight = parseFloat(getComputedStyle(canvas).getPropertyValue("--node-height"));
const svgNamespace = "http://www.w3.org/2000/svg";
const maxLevel = 2147483647;
const levelsPerPage = 5;
const relationNames = { requires: "前置", excludes: "互斥", disables: "单向禁用" };
const linkGuides = {
    requires: "从前置卡右侧的圆点拖到后继卡，选中连线可设解锁等级。",
    excludes: "从卡片右侧的圆点拖到另一张卡，两张卡互斥。",
    disables: "从触发禁用的卡拖到被停用的卡，箭头指向被停用方。",
};

let state;
let artwork;
let graph;
let savedGraph;
let saving = false;
let seat = "Commander";
let mode = "requires";
let selected;
let pending;
let gesture;
let editing;
let editingEffects;
let firstEffectLevel = 1;
const views = new Map();
const undoStack = [];
const nodeElements = new Map();
const edgeElements = new Map();

const cardsHere = () => graph.cards.filter((card) => card.seat === seat);
const cardById = (id) => graph.cards.find((card) => card.id === id);
const linkKey = (link) => [link.kind, link.source, link.target].join("|");
const isDirty = () => graph && JSON.stringify(graph) !== savedGraph;
const clamp = (value, min, max) => Math.min(max, Math.max(min, value));
const limitText = (card) => card.maxLevel ? `${card.maxLevel} 级` : "不限等级";

function relationText(link) {
    const source = cardById(link.source).title;
    const target = cardById(link.target).title;
    if (link.kind === "requires") return `「${source}」达到 ${link.minLevel || 1} 级解锁「${target}」`;
    if (link.kind === "excludes") return `「${source}」与「${target}」互斥`;
    return link.futureOnly ? `「${source}」停止「${target}」的后续获取` : `「${source}」单向禁用「${target}」`;
}

function markDirection(path, kind) {
    if (kind === "excludes") path.removeAttribute("marker-end");
    else path.setAttribute("marker-end", kind === "disables" ? "url(#disable-arrow)" : "url(#require-arrow)");
}

function element(tag, text, className) {
    const node = document.createElement(tag);
    if (text !== undefined) node.textContent = text;
    if (className) node.className = className;
    return node;
}

function button(text, action, className) {
    const node = element("button", text, className);
    node.type = "button";
    node.addEventListener("click", action);
    return node;
}

function artImage(url, alt, className) {
    const image = element("img", undefined, className);
    image.alt = alt;
    image.draggable = false;
    image.src = url;
    return image;
}

function cardFace(card, media) {
    const frame = element("div", undefined, "card-face");
    const surface = element("div", undefined, "card-face-surface");
    const [x, y, width, height] = media.window;
    frame.style.aspectRatio = `${width} / ${height}`;
    const poster = artImage(media.face, `${card.title}牌面`, "card-face-poster");
    surface.append(poster);
    frame.append(surface);
    if (!media.layers) return frame;

    const canvas = element("canvas");
    canvas.width = Math.round(width);
    canvas.height = Math.round(height);
    canvas.setAttribute("aria-hidden", "true");
    canvas.hidden = true;
    surface.append(canvas);

    const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
    const [tileWidth, tileHeight] = media.motion?.tile ?? artwork.tile;
    const shifts = media.motion?.shifts ?? artwork.depth.map((depth) => [artwork.travel * depth, artwork.travel * depth]);
    const tilt = media.motion?.tilt ?? artwork.tilt;
    let atlas, pose, animation = 0;
    function draw() {
        animation = 0;
        if (!pose || !frame.isConnected || !atlas?.naturalWidth) return;
        const ctx = canvas.getContext("2d");
        if (!ctx) return;
        ctx.setTransform(canvas.width / width, 0, 0, canvas.height / height, 0, 0);
        ctx.clearRect(0, 0, width, height);
        for (let i = shifts.length - 1; i >= 0; i--) {
            const [dx, dy] = shifts[i];
            ctx.drawImage(atlas, (i % 3) * tileWidth, Math.floor(i / 3) * tileHeight, tileWidth, tileHeight,
                pose[0] * dx - x, pose[1] * dy - y, tileWidth, tileHeight);
        }
        canvas.hidden = false;
        surface.style.transform = `perspective(800px) rotateX(${-pose[1] * tilt}deg) rotateY(${pose[0] * tilt}deg)`;
    }
    function schedule() {
        if (!animation && pose && atlas?.naturalWidth) animation = requestAnimationFrame(draw);
    }
    function reset() {
        pose = null;
        canvas.hidden = true;
        surface.style.removeProperty("transform");
    }
    frame.addEventListener("pointermove", (event) => {
        if (event.pointerType === "touch" || reducedMotion.matches) return reset();
        const rect = frame.getBoundingClientRect();
        pose = [clamp(2 * (event.clientX - rect.left) / rect.width - 1, -1, 1),
            clamp(2 * (event.clientY - rect.top) / rect.height - 1, -1, 1)];
        if (!atlas) {
            atlas = new Image();
            atlas.onload = schedule;
            atlas.src = media.layers;
        }
        schedule();
    });
    frame.addEventListener("pointerleave", reset);
    frame.addEventListener("pointercancel", reset);
    return frame;
}

function notify(text, error = false) {
    $("message").textContent = text;
    $("message").classList.toggle("error", error);
}

function refreshStatus() {
    const dirty = Boolean(isDirty());
    const indicator = $("save-state");
    indicator.dataset.dirty = dirty;
    if (!state) indicator.textContent = "正在读取";
    else if (saving) indicator.textContent = "正在保存草稿";
    else if (dirty) indicator.textContent = "有未保存的改动";
    else if (state.savedAt) indicator.textContent = "草稿已保存";
    else indicator.textContent = "当前正式卡表";
    indicator.title = "草稿文件：data/Design/RogueCardGraph.json";
    $("save").disabled = !state || saving || (!dirty && Boolean(state.revision));
    $("add").disabled = !state;
    $("undo").disabled = !undoStack.length;
}

function remember(previous) {
    undoStack.push(previous);
    if (undoStack.length > 50) undoStack.shift();
}

function editGraph(change) {
    const previous = structuredClone(graph);
    change();
    remember(previous);
    render();
}

function undo() {
    if (!undoStack.length || gesture) return;
    graph = undoStack.pop();
    selected = pending = null;
    render();
    notify("已撤销上一次编辑。");
}

function getView() {
    if (!views.has(seat)) views.set(seat, { x: 0, y: 0, scale: 1 });
    return views.get(seat);
}

function updateView() {
    const { x, y, scale } = getView();
    $("world").style.transform = `translate(${x}px, ${y}px) scale(${scale})`;
    $("zoom-level").textContent = `${Math.round(scale * 100)}%`;
}

function fit() {
    if (!graph) return;
    const cards = cardsHere();
    const width = canvas.clientWidth;
    const height = canvas.clientHeight;
    if (!cards.length) {
        views.set(seat, { x: 0, y: 0, scale: 1 });
    } else {
        const left = Math.min(...cards.map((card) => card.x));
        const top = Math.min(...cards.map((card) => card.y));
        const right = Math.max(...cards.map((card) => card.x + nodeWidth));
        const bottom = Math.max(...cards.map((card) => card.y + nodeHeight));
        const scale = clamp(Math.min((width - 84) / (right - left), (height - 72) / (bottom - top)), .12, 1);
        views.set(seat, {
            x: width / 2 - (left + right) / 2 * scale,
            y: height / 2 - (top + bottom) / 2 * scale,
            scale,
        });
    }
    updateView();
}

function zoom(factor, x = canvas.clientWidth / 2, y = canvas.clientHeight / 2) {
    if (gesture || !graph) return;
    const view = getView();
    const scale = clamp(view.scale * factor, .12, 2.5);
    const ratio = scale / view.scale;
    view.x = x - (x - view.x) * ratio;
    view.y = y - (y - view.y) * ratio;
    view.scale = scale;
    updateView();
}

function positionNode(card) {
    nodeElements.get(card.id).style.transform = `translate(${card.x}px, ${card.y}px)`;
}

function curve(source, target, lane) {
    const x1 = source.x + nodeWidth / 2;
    const y1 = source.y + nodeHeight / 2;
    const x2 = target.x + nodeWidth / 2;
    const y2 = target.y + nodeHeight / 2;
    if (Math.abs(x2 - x1) >= nodeWidth * .9 || Math.abs(x2 - x1) > Math.abs(y2 - y1)) {
        const direction = x2 >= x1 ? 1 : -1;
        const a = x1 + direction * (nodeWidth / 2 + 4);
        const b = x2 - direction * (nodeWidth / 2 + 6);
        const bend = direction * Math.max(30, Math.abs(b - a) * .5);
        return `M${a} ${y1 + lane} C${a + bend} ${y1 + lane} ${b - bend} ${y2 + lane} ${b} ${y2 + lane}`;
    }
    const direction = y2 >= y1 ? 1 : -1;
    const a = y1 + direction * (nodeHeight / 2 + 4);
    const b = y2 - direction * (nodeHeight / 2 + 6);
    const bend = direction * Math.max(30, Math.abs(b - a) * .5);
    return `M${x1 + lane} ${a} C${x1 + lane} ${a + bend} ${x2 + lane} ${b - bend} ${x2 + lane} ${b}`;
}

function positionEdges() {
    for (const { link, paths, label, lane } of edgeElements.values()) {
        const source = cardById(link.source), target = cardById(link.target);
        const path = curve(source, target, lane);
        for (const node of paths) node.setAttribute("d", path);
        if (label) {
            const point = paths[0].getPointAtLength(paths[0].getTotalLength() / 2);
            label.setAttribute("x", point.x);
            label.setAttribute("y", point.y - 7);
        }
    }
}

function refreshSelection() {
    for (const [id, node] of nodeElements) {
        node.classList.toggle("selected", id === selected?.card);
        node.classList.toggle("link-source", id === (gesture?.type === "link" ? gesture.id : pending?.source));
        node.classList.toggle("drop-target", id === gesture?.target && id !== gesture?.id);
    }
    for (const [key, { group }] of edgeElements) {
        group.classList.toggle("selected", key === selected?.edge);
    }
    const guide = pending
        ? `已选「${cardById(pending.source).title}」，点选另一张卡完成连线。`
        : linkGuides[mode];
    $("guide").textContent = guide;
}

function select(value) {
    const changed = selected?.card !== value?.card || selected?.edge !== value?.edge;
    selected = value;
    refreshSelection();
    renderDetails();
    if (changed) inspector.scrollTop = 0;
}

function selectCard(id) {
    if (pending) {
        addLink(pending.source, id, pending.kind);
    } else {
        select({ card: id });
    }
}

function startLink(id) {
    pending = pending?.source === id ? null : { source: id, kind: mode };
    refreshSelection();
}

function renderCanvas() {
    nodeElements.clear();
    edgeElements.clear();
    nodeLayer.replaceChildren();
    edgeLayer.replaceChildren();
    const cards = cardsHere();
    for (const card of cards) {
        const node = element("div", undefined, "graph-node");
        node.dataset.card = card.id;
        node.classList.toggle("draft-node", card.id.startsWith("draft-"));
        const label = button(undefined, (event) => {
            if (event.detail === 0) selectCard(card.id);
        }, "node-label");
        label.title = card.title;
        const media = artwork?.cards[card.id];
        if (media?.icon) {
            label.classList.add("has-icon");
            label.append(artImage(media.icon, "", "node-icon"));
        }
        label.append(element("span", card.title), element("small", limitText(card)));
        const port = button(undefined, (event) => {
            if (event.detail === 0) startLink(card.id);
        }, "port");
        port.setAttribute("aria-label", `从${card.title}连线`);
        port.title = "拖出连线，也可点一下再选目标卡";
        node.append(label, port);
        nodeElements.set(card.id, node);
        nodeLayer.append(node);
        positionNode(card);
    }
    const links = graph.links.filter((link) => nodeElements.has(link.source) && nodeElements.has(link.target));
    const pairKey = (link) => [link.source, link.target].sort().join("|");
    const pairs = new Map();
    for (const link of links) {
        const key = pairKey(link);
        if (!pairs.has(key)) pairs.set(key, []);
        pairs.get(key).push(link);
    }
    for (const link of links) {
        const key = linkKey(link);
        const group = document.createElementNS(svgNamespace, "g");
        group.setAttribute("class", `edge ${link.kind}`);
        group.dataset.edge = key;
        group.setAttribute("role", "button");
        group.setAttribute("tabindex", "0");
        group.setAttribute("aria-label", relationText(link));
        const tooltip = document.createElementNS(svgNamespace, "title");
        tooltip.textContent = relationText(link);
        group.append(tooltip);
        const line = document.createElementNS(svgNamespace, "path");
        line.setAttribute("class", "edge-stroke");
        markDirection(line, link.kind);
        const hit = document.createElementNS(svgNamespace, "path");
        hit.setAttribute("class", "edge-hit");
        const showLink = () => {
            pending = null;
            select({ edge: key });
        };
        group.addEventListener("click", showLink);
        group.addEventListener("keydown", (event) => {
            if (event.key === "Enter" || event.key === " ") {
                event.preventDefault();
                showLink();
            }
        });
        group.append(line, hit);
        let label;
        if (link.minLevel > 1) {
            label = document.createElementNS(svgNamespace, "text");
            label.setAttribute("class", "edge-level");
            label.setAttribute("text-anchor", "middle");
            label.textContent = `${link.minLevel}级`;
            group.append(label);
        }
        edgeLayer.append(group);
        const pair = pairs.get(pairKey(link));
        const lane = (pair.indexOf(link) - (pair.length - 1) / 2) * 18;
        edgeElements.set(key, { group, link, paths: [line, hit], label, lane });
    }
    positionEdges();
    const empty = !cards.length;
    $("empty").hidden = !empty;
    $("empty-title").textContent = `${seatNames[seat]}席位暂无卡牌`;
    $("empty-text").textContent = "新增一张卡，开始设计这个卡池。";
    $("empty-add").hidden = !empty;
    $("counts").textContent = `${cards.length} 张卡 · ${edgeElements.size} 条关系`;
    updateView();
}

function appendRelations(title, links, cardId) {
    if (!links.length) return;
    inspector.append(element("h3", title));
    const list = element("ul", undefined, "relation-list");
    for (const link of links) {
        const other = link.source === cardId ? link.target : link.source;
        const item = element("li");
        const threshold = link.kind === "requires" ? `（前置 ${link.minLevel || 1} 级）`
            : link.futureOnly ? "（仅停止获取）" : "";
        item.append(button(cardById(other).title + threshold, () => select({ edge: linkKey(link) })));
        list.append(item);
    }
    inspector.append(list);
}

function renderDetails() {
    inspector.replaceChildren();
    const card = selected?.card && cardById(selected.card);
    const link = selected?.edge && graph.links.find((entry) => linkKey(entry) === selected.edge);
    if (card && card.seat === seat) {
        const details = state.base.details[card.id];
        const original = state.base.cards.find((entry) => entry.id === card.id);
        inspector.append(element("p", details ? `${seatNames[seat]} · ${card.id}` : `${seatNames[seat]} · 草稿`, "caption"));
        inspector.append(element("h2", card.title));
        inspector.append(element("p", `总等级　${limitText(card)}`, "identity"));
        const media = artwork?.cards[card.id];
        if (media?.face) {
            inspector.append(cardFace(card, media));
            if (media.caption) inspector.append(element("p", media.caption, "caption"));
        }
        const actions = element("div", undefined, "detail-actions");
        actions.append(button("编辑卡牌", () => openCardDialog(card)), button("删除卡牌", deleteSelection, "danger quiet"));
        inspector.append(actions);
        const effects = Object.entries(card.levelEffects);
        const formalTemplate = original?.description === card.description && /\{\d+\}/.test(card.description);
        if (card.description && !formalTemplate && !effects.some(([, text]) => text === card.description)) {
            inspector.append(element("h3", "通用说明"), element("p", card.description, "description"));
        }
        inspector.append(element("h3", "每级效果"));
        if (effects.length) {
            const list = element("dl", undefined, "effect-list");
            for (const [level, effect] of effects) {
                list.append(element("dt", `${level} 级`), element("dd", effect));
            }
            inspector.append(list);
        } else {
            inspector.append(element("p", "尚未填写逐级效果。", "muted"));
        }
        if (details?.note) {
            const note = element("details");
            note.append(element("summary", "正式卡表备注"), element("p", details.note, "note"));
            inspector.append(note);
        }
        const linked = graph.links.filter((entry) => entry.source === card.id || entry.target === card.id);
        appendRelations("前置卡", linked.filter((entry) => entry.kind === "requires" && entry.target === card.id), card.id);
        appendRelations("后继卡", linked.filter((entry) => entry.kind === "requires" && entry.source === card.id), card.id);
        appendRelations("互斥卡", linked.filter((entry) => entry.kind === "excludes"), card.id);
        appendRelations("本牌禁用", linked.filter((entry) => entry.kind === "disables" && entry.source === card.id), card.id);
        appendRelations("被以下卡牌禁用", linked.filter((entry) => entry.kind === "disables" && entry.target === card.id), card.id);
    } else if (link) {
        const left = cardById(link.source).title;
        const right = cardById(link.target).title;
        const requires = link.kind === "requires";
        inspector.append(element("p", relationNames[link.kind], "caption"));
        inspector.append(element("h2", link.kind === "excludes" ? `${left} ↔ ${right}` : `${left} → ${right}`));
        const explanation = requires ? `「${left}」达到 ${link.minLevel || 1} 级后，满足这条前置。`
            : link.kind === "excludes" ? "本局两张卡只能取得其中一张。"
            : `先取得「${right}」仍可选择「${left}」。取得「${left}」时，「${right}」退出后续候选。`;
        const relationshipDescription = element("p", explanation, "description");
        inspector.append(relationshipDescription);
        if (requires) {
            const label = element("label", "前置等级 ");
            const input = document.createElement("input");
            input.type = "number"; input.min = "1"; input.required = true;
            input.max = String(cardById(link.source).maxLevel || maxLevel);
            input.value = String(link.minLevel || 1);
            let recorded = false;
            function updateThreshold() {
                const level = input.valueAsNumber;
                if (!input.validity.valid || level === (link.minLevel || 1)) return;
                if (!recorded) remember(structuredClone(graph));
                recorded = true;
                if (level === 1) delete link.minLevel;
                else link.minLevel = level;
                relationshipDescription.textContent = relationText(link) + "。";
                renderCanvas();
                refreshSelection();
                refreshStatus();
            }
            input.addEventListener("input", updateThreshold);
            input.addEventListener("change", () => {
                if (!input.validity.valid) {
                    notify(`前置等级须为 1 至 ${input.max} 的整数。`, true);
                    input.value = String(link.minLevel || 1);
                } else updateThreshold();
                recorded = false;
            });
            label.append(input); inspector.append(label);
        } else if (link.kind === "disables") {
            const label = element("label", "禁用方式 ");
            const select = element("select");
            select.append(new Option("停用已有效果并停止获取", "all"), new Option("保留已有等级，仅停止后续获取", "future"));
            select.value = link.futureOnly ? "future" : "all";
            select.addEventListener("change", () => editGraph(() => {
                if (select.value === "future") link.futureOnly = true;
                else delete link.futureOnly;
            }));
            label.append(select); inspector.append(label);
        }
        const actions = element("div", undefined, "detail-actions");
        actions.append(button("删除连线", deleteSelection, "danger quiet"));
        inspector.append(actions);
    } else {
        inspector.append(element("p", "选择卡牌查看说明，选择连线查看关系。", "muted"));
    }
}

function render() {
    const focusCard = document.activeElement?.closest(".graph-node")?.dataset.card;
    for (const tab of tabs) {
        const active = tab.dataset.seat === seat;
        tab.setAttribute("aria-selected", active);
        tab.tabIndex = active ? 0 : -1;
        tab.querySelector("span").textContent = graph.cards.filter((card) => card.seat === tab.dataset.seat).length;
    }
    $("workspace").setAttribute("aria-labelledby", `tab-${seat}`);
    $("source-notice").hidden = !state.sourceChanged;
    renderCanvas();
    refreshSelection();
    renderDetails();
    refreshStatus();
    if (focusCard) nodeElements.get(focusCard)?.querySelector(".node-label").focus({ preventScroll: true });
}

function validateLinks(links) {
    const parents = new Map(graph.cards.map((card) => [card.id, []]));
    const conflicts = new Map(graph.cards.map((card) => [card.id, new Set()]));
    for (const { kind, source, target } of links) {
        if (kind === "requires") parents.get(target).push(source);
        else if (kind === "excludes") {
            conflicts.get(source).add(target);
            conflicts.get(target).add(source);
        }
    }
    const visiting = new Set();
    const closures = new Map();
    function closure(id) {
        if (visiting.has(id)) throw new Error("这条连线会形成循环前置。");
        if (closures.has(id)) return closures.get(id);
        visiting.add(id);
        const members = new Set([id]);
        for (const parent of parents.get(id)) {
            for (const member of closure(parent)) members.add(member);
        }
        visiting.delete(id);
        for (const member of members) {
            for (const other of conflicts.get(member)) {
                if (members.has(other)) {
                    throw new Error(`「${cardById(member).title}」和「${cardById(other).title}」必须在同一条前置链中取得，不能互斥。`);
                }
            }
        }
        closures.set(id, members);
        return members;
    }
    for (const card of graph.cards) closure(card.id);
}

function addLink(source, target, kind) {
    pending = null;
    refreshSelection();
    if (source === target) {
        notify("请连接两张不同的卡。", true);
        return;
    }
    if (kind === "excludes" && source > target) [source, target] = [target, source];
    const link = { kind, source, target };
    const key = linkKey(link);
    if (graph.links.some((entry) => linkKey(entry) === key)) {
        select({ edge: key });
        notify("这条关系已经存在。");
        return;
    }
    try {
        validateLinks([...graph.links, link]);
    } catch (error) {
        notify(error.message, true);
        return;
    }
    selected = { edge: key };
    editGraph(() => graph.links.push(link));
    notify(`已添加${relationNames[kind]}关系。`);
}

function deleteSelection() {
    const current = selected;
    if (!current) return;
    selected = pending = null;
    editGraph(() => {
        if (current.card) {
            graph.cards = graph.cards.filter((card) => card.id !== current.card);
            graph.links = graph.links.filter((link) => link.source !== current.card && link.target !== current.card);
        } else {
            graph.links = graph.links.filter((link) => linkKey(link) !== current.edge);
        }
    });
    notify(current.card ? "已移除卡牌及其连线，可撤销。" : "已删除连线，可撤销。");
}

function renderEffectInputs() {
    const capInput = $("card-max-level");
    const cap = capInput.valueAsNumber;
    const limit = cap === 0 ? maxLevel : Number.isInteger(cap) && cap > 0 ? Math.min(cap, maxLevel) : 1;
    firstEffectLevel = 1 + levelsPerPage * Math.floor((clamp(firstEffectLevel, 1, limit) - 1) / levelsPerPage);
    const last = Math.min(limit, firstEffectLevel + levelsPerPage - 1);
    const fields = $("level-inputs");
    fields.replaceChildren();
    for (let level = firstEffectLevel; level <= last; level++) {
        const label = element("label", `第 ${level} 级`);
        const input = element("textarea");
        input.id = `effect-${level}`;
        label.htmlFor = input.id;
        input.rows = 2;
        input.maxLength = 2000;
        input.value = editingEffects[level] || "";
        input.addEventListener("input", () => {
            if (input.value.trim()) editingEffects[level] = input.value;
            else delete editingEffects[level];
        });
        fields.append(label, input);
    }
    $("level-pages").hidden = limit <= levelsPerPage;
    $("prev-levels").disabled = firstEffectLevel === 1;
    $("next-levels").disabled = last === limit;
    $("jump-level").max = String(limit);
    $("jump-level").value = String(firstEffectLevel);

    const conflict = editing && cap > 0 && graph.links.find((link) =>
        link.kind === "requires" && link.source === editing && (link.minLevel || 1) > cap);
    const error = conflict ? `「${cardById(conflict.target).title}」需要本牌 ${conflict.minLevel} 级，请先调整该前置线。` : "";
    capInput.setCustomValidity(error);
    const removed = Object.keys(editingEffects).filter((level) => Number(level) > limit).length;
    const message = error || (removed ? `确定后将移除超出总等级的 ${removed} 条效果，可撤销。` : "");
    $("level-notice").hidden = !message;
    $("level-notice").textContent = message;
}

function openCardDialog(card) {
    if (!state) return;
    editing = card?.id;
    $("dialog-title").textContent = card ? "编辑卡牌" : `新增${seatNames[seat]}卡牌`;
    $("card-title").value = card?.title || "";
    $("card-title").setCustomValidity("");
    $("card-description").value = card?.description || "";
    $("card-max-level").value = String(card?.maxLevel ?? 1);
    editingEffects = { ...card?.levelEffects };
    firstEffectLevel = 1;
    renderEffectInputs();
    dialog.showModal();
    $("card-title").focus();
}

function addCard(values) {
    const view = getView();
    const x = Math.round((canvas.clientWidth / 2 - view.x) / view.scale - nodeWidth / 2);
    let y = Math.round((canvas.clientHeight / 2 - view.y) / view.scale - nodeHeight / 2);
    const cards = cardsHere();
    while (cards.some((card) => Math.abs(card.x - x) < nodeWidth + 14 && Math.abs(card.y - y) < nodeHeight + 14)) {
        y += nodeHeight + 22;
    }
    const id = `draft-${crypto.randomUUID()}`;
    selected = { card: id };
    pending = null;
    editGraph(() => graph.cards.push({ id, seat, ...values, x, y }));
    view.y = canvas.clientHeight / 2 - (y + nodeHeight / 2) * view.scale;
    updateView();
    nodeElements.get(id).querySelector(".node-label").focus({ preventScroll: true });
}

function setMode(next) {
    mode = next;
    pending = null;
    canvas.dataset.mode = mode;
    for (const control of modeButtons) control.setAttribute("aria-pressed", control.dataset.mode === mode);
    refreshSelection();
}

function switchSeat(next) {
    if (!graph || next === seat) return;
    seat = next;
    pending = selected = null;
    const hasView = views.has(seat);
    render();
    if (!hasView) fit();
    notify("");
}

function beginGesture(event) {
    if (!graph || gesture || (event.button !== 0 && event.button !== 1)) return;
    if (event.target.closest(".zoom, .empty button, [data-edge]")) return;
    const id = event.target.closest(".graph-node")?.dataset.card;
    const view = getView();
    const start = { pointer: event.pointerId, clientX: event.clientX, clientY: event.clientY, moved: false };
    if (event.button === 1 || !id) {
        gesture = { ...start, type: "pan", x: view.x, y: view.y };
    } else if (event.target.closest(".port")) {
        gesture = { ...start, type: "link", id, kind: mode };
        pending = null;
    } else if (pending) {
        gesture = { ...start, type: "target", id };
    } else {
        const card = cardById(id);
        gesture = { ...start, type: "node", id, x: card.x, y: card.y };
    }
    event.preventDefault();
    canvas.setPointerCapture(event.pointerId);
    refreshSelection();
}

function moveGesture(event) {
    if (gesture?.pointer !== event.pointerId) return;
    const dx = event.clientX - gesture.clientX;
    const dy = event.clientY - gesture.clientY;
    if (!gesture.moved && Math.hypot(dx, dy) < 4) return;
    gesture.moved = true;
    const view = getView();
    if (gesture.type === "pan") {
        canvas.classList.add("panning");
        view.x = gesture.x + dx;
        view.y = gesture.y + dy;
        updateView();
    } else if (gesture.type === "node") {
        const card = cardById(gesture.id);
        card.x = clamp(Math.round(gesture.x + dx / view.scale), -100000, 100000);
        card.y = clamp(Math.round(gesture.y + dy / view.scale), -100000, 100000);
        positionNode(card);
        positionEdges();
    } else if (gesture.type === "link") {
        const rect = canvas.getBoundingClientRect();
        const x = (event.clientX - rect.left - view.x) / view.scale;
        const y = (event.clientY - rect.top - view.y) / view.scale;
        const card = cardById(gesture.id);
        const from = card.x + nodeWidth;
        const center = card.y + nodeHeight / 2;
        preview.setAttribute("d", `M${from} ${center} C${from + 38} ${center} ${x - 30} ${y} ${x} ${y}`);
        preview.classList.toggle("excludes", gesture.kind === "excludes");
        preview.classList.toggle("disables", gesture.kind === "disables");
        markDirection(preview, gesture.kind);
        preview.setAttribute("visibility", "visible");
        gesture.target = document.elementFromPoint(event.clientX, event.clientY)?.closest(".graph-node")?.dataset.card;
        refreshSelection();
    }
}

function finishGesture(event, canceled = false) {
    if (gesture?.pointer !== event.pointerId) return;
    if (!canceled) moveGesture(event);
    const action = gesture;
    gesture = null;
    preview.setAttribute("visibility", "hidden");
    canvas.classList.remove("panning");
    if (action.type === "node") {
        const card = cardById(action.id);
        if (canceled) {
            card.x = action.x;
            card.y = action.y;
            positionNode(card);
            positionEdges();
        } else {
            if (card.x !== action.x || card.y !== action.y) {
                const before = structuredClone(graph);
                const previous = before.cards.find((entry) => entry.id === action.id);
                previous.x = action.x;
                previous.y = action.y;
                remember(before);
            }
            selectCard(action.id);
            nodeElements.get(action.id).querySelector(".node-label").focus({ preventScroll: true });
        }
    } else if (!canceled && action.type === "link") {
        if (!action.moved) startLink(action.id);
        else if (action.target) addLink(action.id, action.target, action.kind);
    } else if (!canceled && action.type === "target" && !action.moved) {
        selectCard(action.id);
    } else if (!canceled && action.type === "pan" && !action.moved) {
        pending = null;
        select(null);
    }
    refreshSelection();
    refreshStatus();
}

async function save() {
    if (!state || saving || gesture || $("save").disabled) return;
    const snapshot = JSON.stringify(graph);
    saving = true;
    refreshStatus();
    try {
        const response = await fetch("/api/graph", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                schema: state.schema, graph: JSON.parse(snapshot), revision: state.revision, baseFingerprint: state.base.fingerprint,
            }),
        });
        const result = await response.json();
        if (!response.ok) throw new Error(result.error);
        state.revision = result.revision;
        state.savedAt = result.savedAt;
        state.sourceChanged = result.sourceChanged;
        savedGraph = snapshot;
        $("source-notice").hidden = !state.sourceChanged;
        const { added, removed, updated } = result.changes;
        notify(`已保存草稿 · 新增 ${added.length} · 删除 ${removed.length} · 修改 ${updated.length}`);
    } catch (error) {
        notify(error.message, true);
    } finally {
        saving = false;
        refreshStatus();
    }
}

for (const tab of tabs) {
    tab.addEventListener("click", () => switchSeat(tab.dataset.seat));
    tab.addEventListener("keydown", (event) => {
        if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
        event.preventDefault();
        const step = event.key === "ArrowRight" ? 1 : -1;
        const next = tabs[(tabs.indexOf(tab) + step + tabs.length) % tabs.length];
        switchSeat(next.dataset.seat);
        next.focus();
    });
}
for (const control of modeButtons) control.addEventListener("click", () => setMode(control.dataset.mode));
$("add").addEventListener("click", () => openCardDialog());
$("empty-add").addEventListener("click", () => openCardDialog());
$("cancel-card").addEventListener("click", () => dialog.close());
$("undo").addEventListener("click", undo);
$("save").addEventListener("click", save);
$("fit").addEventListener("click", fit);
$("zoom-in").addEventListener("click", () => zoom(1.18));
$("zoom-out").addEventListener("click", () => zoom(1 / 1.18));
$("card-title").addEventListener("input", () => $("card-title").setCustomValidity(""));
$("card-max-level").addEventListener("input", renderEffectInputs);
$("prev-levels").addEventListener("click", () => { firstEffectLevel -= levelsPerPage; renderEffectInputs(); });
$("next-levels").addEventListener("click", () => { firstEffectLevel += levelsPerPage; renderEffectInputs(); });
function jumpToLevel(event) {
    const input = event.target;
    if (!Number.isInteger(input.valueAsNumber) || !input.checkValidity()) { input.reportValidity(); return; }
    const level = input.valueAsNumber;
    firstEffectLevel = level;
    renderEffectInputs();
    $(`effect-${level}`).focus();
}
$("jump-level").addEventListener("change", jumpToLevel);
$("jump-level").addEventListener("keydown", (event) => {
    if (event.key === "Enter") {
        event.preventDefault();
        jumpToLevel(event);
    }
});
$("card-form").addEventListener("submit", (event) => {
    event.preventDefault();
    const title = $("card-title").value.trim();
    const description = $("card-description").value.trim();
    if (!title) {
        $("card-title").setCustomValidity("请输入卡名。");
        $("card-title").reportValidity();
        return;
    }
    const cap = $("card-max-level").valueAsNumber;
    const effects = Object.fromEntries(Object.entries(editingEffects)
        .filter(([level]) => cap === 0 || Number(level) <= cap)
        .map(([level, text]) => [level, text.trim()]));
    const values = { title, description, maxLevel: cap, levelEffects: effects };
    dialog.close();
    if (editing) {
        const card = cardById(editing);
        if (title === card.title && description === card.description && cap === card.maxLevel &&
            JSON.stringify(effects) === JSON.stringify(card.levelEffects)) return;
        editGraph(() => Object.assign(card, values));
        notify("已修改卡牌。");
    } else {
        addCard(values);
        notify("已新增草稿卡牌。");
    }
});
canvas.addEventListener("pointerdown", beginGesture);
canvas.addEventListener("pointermove", moveGesture);
canvas.addEventListener("pointerup", (event) => finishGesture(event));
canvas.addEventListener("pointercancel", (event) => finishGesture(event, true));
canvas.addEventListener("lostpointercapture", (event) => finishGesture(event, true));
canvas.addEventListener("wheel", (event) => {
    if (!graph) return;
    event.preventDefault();
    const rect = canvas.getBoundingClientRect();
    zoom(Math.exp(-clamp(event.deltaY, -160, 160) * .0025), event.clientX - rect.left, event.clientY - rect.top);
}, { passive: false });
window.addEventListener("keydown", (event) => {
    if (!graph || dialog.open || gesture) return;
    const modifier = event.ctrlKey || event.metaKey;
    const field = event.target.closest?.("input, textarea, select, [contenteditable=true]");
    if (modifier && event.key.toLowerCase() === "s") {
        event.preventDefault();
        if (field?.reportValidity && !field.reportValidity()) return;
        if (field) field.blur();
        save();
    } else if (field) {
        return;
    } else if (modifier && !event.shiftKey && event.key.toLowerCase() === "z") {
        event.preventDefault();
        undo();
    } else if (event.key === "Delete" && selected) {
        event.preventDefault();
        deleteSelection();
    } else if (event.key === "Escape") {
        pending = null;
        select(null);
    }
});
window.addEventListener("beforeunload", (event) => {
    if (isDirty()) {
        event.preventDefault();
        event.returnValue = "";
    }
});

async function load() {
    try {
        const [response, media] = await Promise.all([
            fetch("/api/graph"),
            fetch("/artwork/catalog.json").then((r) => r.ok ? r.json() : null).catch(() => null),
        ]);
        const result = await response.json();
        if (!response.ok) throw new Error(result.error);
        if (result.schema !== 2) throw new Error("正在更新等级编辑器，请刷新页面。");
        state = result;
        artwork = media;
        graph = state.graph;
        savedGraph = JSON.stringify(graph);
        setMode("requires");
        render();
        fit();
        if (!artwork) notify("暂时无法载入卡牌美术。", true);
    } catch (error) {
        $("empty-title").textContent = "卡表读取失败";
        $("empty-text").textContent = error.message;
        $("save-state").textContent = "读取失败";
    }
}

load();
