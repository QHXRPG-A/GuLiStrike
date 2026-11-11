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

let state;
let graph;
let savedGraph;
let saving = false;
let seat = "Commander";
let mode = "requires";
let selected;
let pending;
let gesture;
let editing;
const views = new Map();
const undoStack = [];
const nodeElements = new Map();
const edgeElements = new Map();

const cardsHere = () => graph.cards.filter((card) => card.seat === seat);
const cardById = (id) => graph.cards.find((card) => card.id === id);
const linkKey = (link) => [link.kind, link.source, link.target].join("|");
const isDirty = () => graph && JSON.stringify(graph) !== savedGraph;
const clamp = (value, min, max) => Math.min(max, Math.max(min, value));

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

function curve(source, target) {
    const x1 = source.x + nodeWidth / 2;
    const y1 = source.y + nodeHeight / 2;
    const x2 = target.x + nodeWidth / 2;
    const y2 = target.y + nodeHeight / 2;
    if (Math.abs(x2 - x1) >= nodeWidth * .9 || Math.abs(x2 - x1) > Math.abs(y2 - y1)) {
        const direction = x2 >= x1 ? 1 : -1;
        const a = x1 + direction * (nodeWidth / 2 + 4);
        const b = x2 - direction * (nodeWidth / 2 + 6);
        const bend = direction * Math.max(30, Math.abs(b - a) * .5);
        return `M${a} ${y1} C${a + bend} ${y1} ${b - bend} ${y2} ${b} ${y2}`;
    }
    const direction = y2 >= y1 ? 1 : -1;
    const a = y1 + direction * (nodeHeight / 2 + 4);
    const b = y2 - direction * (nodeHeight / 2 + 6);
    const bend = direction * Math.max(30, Math.abs(b - a) * .5);
    return `M${x1} ${a} C${x1} ${a + bend} ${x2} ${b - bend} ${x2} ${b}`;
}

function positionEdges() {
    for (const { link, paths } of edgeElements.values()) {
        const path = curve(cardById(link.source), cardById(link.target));
        for (const node of paths) node.setAttribute("d", path);
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
        : mode === "requires"
            ? "从前置卡右侧的圆点拖到需要它的卡。"
            : "从卡片右侧的圆点拖到另一张卡，两张卡互斥。";
    $("guide").textContent = guide;
}

function select(value) {
    selected = value;
    refreshSelection();
    renderDetails();
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
        label.append(element("span", card.title));
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
    for (const link of graph.links) {
        if (!nodeElements.has(link.source) || !nodeElements.has(link.target)) continue;
        const key = linkKey(link);
        const group = document.createElementNS(svgNamespace, "g");
        group.setAttribute("class", `edge ${link.kind}`);
        group.dataset.edge = key;
        group.setAttribute("role", "button");
        group.setAttribute("tabindex", "0");
        const left = cardById(link.source).title;
        const right = cardById(link.target).title;
        group.setAttribute("aria-label", link.kind === "requires" ? `${left}是${right}的前置` : `${left}与${right}互斥`);
        const line = document.createElementNS(svgNamespace, "path");
        line.setAttribute("class", "edge-stroke");
        if (link.kind === "requires") line.setAttribute("marker-end", "url(#require-arrow)");
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
        edgeLayer.append(group);
        edgeElements.set(key, { group, link, paths: [line, hit] });
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
        item.append(button(cardById(other).title, () => select({ edge: linkKey(link) })));
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
        inspector.append(element("h3", "说明"));
        const descriptions = details && card.description === original.description
            ? details.previews : [card.description];
        for (const description of descriptions) {
            inspector.append(element("p", description || "还没有填写描述。", "description"));
        }
        if (details) {
            const limit = details.maxAcquisitions || "不限";
            inspector.append(element("p", `获取上限　${limit}`, "identity"));
            if (details.note) {
                const note = element("details");
                note.append(element("summary", "机制备注"), element("p", details.note, "note"));
                inspector.append(note);
            }
        }
        const linked = graph.links.filter((entry) => entry.source === card.id || entry.target === card.id);
        appendRelations("前置卡", linked.filter((entry) => entry.kind === "requires" && entry.target === card.id), card.id);
        appendRelations("后继卡", linked.filter((entry) => entry.kind === "requires" && entry.source === card.id), card.id);
        appendRelations("互斥卡", linked.filter((entry) => entry.kind === "excludes"), card.id);
        const actions = element("div", undefined, "detail-actions");
        actions.append(button("编辑卡牌", () => openCardDialog(card)), button("删除卡牌", deleteSelection, "danger quiet"));
        inspector.append(actions);
    } else if (link) {
        const left = cardById(link.source).title;
        const right = cardById(link.target).title;
        const requires = link.kind === "requires";
        inspector.append(element("p", requires ? "前置关系" : "互斥关系", "caption"));
        inspector.append(element("h2", requires ? `${left} → ${right}` : `${left} ↔ ${right}`));
        inspector.append(element("p", requires ? `取得「${right}」前须先取得「${left}」。` : "本局两张卡只能取得其中一张。", "description"));
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
        else {
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
    notify(kind === "requires" ? "已添加前置关系。" : "已添加互斥关系。");
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

function openCardDialog(card) {
    if (!state) return;
    editing = card?.id;
    $("dialog-title").textContent = card ? "编辑卡牌" : `新增${seatNames[seat]}卡牌`;
    $("card-title").value = card?.title || "";
    $("card-title").setCustomValidity("");
    $("card-description").value = card?.description || "";
    dialog.showModal();
    $("card-title").focus();
}

function addCard(title, description) {
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
    editGraph(() => graph.cards.push({ id, seat, title, description, x, y }));
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
                graph: JSON.parse(snapshot), revision: state.revision, baseFingerprint: state.base.fingerprint,
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
$("card-form").addEventListener("submit", (event) => {
    event.preventDefault();
    const title = $("card-title").value.trim();
    const description = $("card-description").value.trim();
    if (!title) {
        $("card-title").setCustomValidity("请输入卡名。");
        $("card-title").reportValidity();
        return;
    }
    dialog.close();
    if (editing) {
        const card = cardById(editing);
        if (title === card.title && description === card.description) return;
        editGraph(() => Object.assign(card, { title, description }));
        notify("已修改卡牌。");
    } else {
        addCard(title, description);
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
    if (modifier && event.key.toLowerCase() === "s") {
        event.preventDefault();
        save();
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
        const response = await fetch("/api/graph");
        const result = await response.json();
        if (!response.ok) throw new Error(result.error);
        state = result;
        graph = state.graph;
        savedGraph = JSON.stringify(graph);
        setMode("requires");
        render();
        fit();
    } catch (error) {
        $("empty-title").textContent = "卡表读取失败";
        $("empty-text").textContent = error.message;
        $("save-state").textContent = "读取失败";
    }
}

load();
