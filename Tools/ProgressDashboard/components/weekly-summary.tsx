'use client';

import { CalendarRange, ChevronRight, FileText } from 'lucide-react';
import React, { useMemo, useState } from 'react';

import { ScrollArea } from '@/components/ui/scroll-area';
import {
  Sheet,
  SheetContent,
  SheetDescription,
  SheetHeader,
  SheetTitle,
} from '@/components/ui/sheet';
import { KIND_LABELS, readableDate } from '@/lib/labels';
import type { ProgressDocument, RelationValue, Snapshot, WorkItem } from '@/lib/progress-types';

function formatDay(date: Date): string {
  return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')}`;
}

function lastWeekRange(now = new Date()): { start: string; end: string; display: string } {
  // 上一个自然周：周一 00:00 起、下周一 00:00 止（左闭右开）；display 为含首尾的日期区间
  const mondayOffset = (now.getDay() + 6) % 7;
  const start = new Date(now.getFullYear(), now.getMonth(), now.getDate() - mondayOffset - 7);
  const end = new Date(start.getFullYear(), start.getMonth(), start.getDate() + 7);
  const lastDay = new Date(start.getFullYear(), start.getMonth(), start.getDate() + 6);
  return {
    start: formatDay(start),
    end: formatDay(end),
    display: `${formatDay(start).slice(5)} ~ ${formatDay(lastDay).slice(5)}`,
  };
}

const THEME_RULES: Array<{ name: string; areas: string[] }> = [
  { name: '僚机与空战', areas: ['wingman'] },
  { name: '资源经济与采矿', areas: ['economy', 'resources', 'resource'] },
  { name: '指挥官技能与单位', areas: ['commander'] },
  { name: '舰船资产与部件', areas: ['ship', 'assets'] },
  { name: '表现与美术资产', areas: ['vfx', 'presentation', 'art', 'building'] },
  { name: '地图工具与面板', areas: ['map-authoring', 'data-pipeline', 'map', 'ui', 'level'] },
  { name: '网络与同步', areas: ['network', 'networking'] },
];
const FALLBACK_THEME = '其他';

const STAGE_LABELS: Record<string, string> = {
  draft: '草案',
  planned: '规划',
  in_progress: '实施',
  verification: '待验收',
};

function resolveTheme(item: WorkItem): { name: string; ruleAreas: string[] } {
  // 标题点名指挥官的工作项优先归入指挥官板块，避免被 wingman/ship 标签抢走
  if (item.title.includes('指挥官')) {
    const rule = THEME_RULES.find((entry) => entry.name === '指挥官技能与单位');
    return { name: '指挥官技能与单位', ruleAreas: rule?.areas ?? ['commander'] };
  }
  for (const rule of THEME_RULES) {
    if (item.areas.some((area) => rule.areas.includes(area))) return { name: rule.name, ruleAreas: rule.areas };
  }
  return { name: FALLBACK_THEME, ruleAreas: [] };
}

function tokenizeTitle(title: string): string[] {
  const normalized = title.normalize('NFKC');
  const tokens: string[] = [];
  for (const word of normalized.match(/[A-Za-z][A-Za-z0-9_]*|\d+/g) ?? []) {
    tokens.push(word.toLowerCase());
  }
  for (const run of normalized.match(/[\u4e00-\u9fff]+/g) ?? []) {
    for (let index = 0; index + 1 < run.length; index += 1) {
      tokens.push(run.slice(index, index + 2));
    }
  }
  return tokens;
}

interface ItemCluster {
  items: WorkItem[];
}

const RELATION_LINK_KEYS = new Set(['supersedes', 'superseded_by', 'source_requirement', 'related']);

function relationTargets(relations: Record<string, RelationValue>): string[] {
  return Object.entries(relations).flatMap(([key, value]) => {
    if (!RELATION_LINK_KEYS.has(key)) return [];
    const values = Array.isArray(value) ? value : value === null || value === false ? [] : [String(value)];
    return values.filter((item) => /^(?:REQ|DEV)-/.test(item));
  });
}

function clusterItems(
  items: WorkItem[],
  allDone: WorkItem[],
  documentsById: Map<string, ProgressDocument>,
  ruleAreas: string[],
): ItemCluster[] {
  // 高度相近的开发内容聚合为一个要点。三路信号做并查集：
  // 1) 文档关系链（supersedes / source_requirement 等指向本周另一闭环项）；
  // 2) 标题词元：共享 ≥2 个板块内辨识词，或共享 1 个全周仅出现 2 次的词；
  // 3) 模块标签：共享一个板块内少数工作项使用的模块标签（排除板块自身的归属标签）。
  const globalDf = new Map<string, number>();
  const titleTokens = new Map<string, string[]>();
  for (const item of allDone) {
    const tokens = tokenizeTitle(item.title);
    titleTokens.set(item.id, tokens);
    for (const token of new Set(tokens)) globalDf.set(token, (globalDf.get(token) ?? 0) + 1);
  }
  const themeTokenDf = new Map<string, number>();
  const themeAreaDf = new Map<string, number>();
  for (const item of items) {
    for (const token of new Set(titleTokens.get(item.id) ?? [])) {
      themeTokenDf.set(token, (themeTokenDf.get(token) ?? 0) + 1);
    }
    for (const area of item.areas) themeAreaDf.set(area, (themeAreaDf.get(area) ?? 0) + 1);
  }
  const tokenCap = Math.max(2, Math.floor(items.length / 3));
  const areaCap = Math.max(1, Math.min(3, Math.floor(items.length / 2)));
  const parent = new Map<string, string>();
  const find = (id: string): string => {
    let root = parent.get(id) ?? id;
    while (root !== (parent.get(root) ?? root)) root = parent.get(root) ?? root;
    parent.set(id, root);
    return root;
  };
  const union = (left: string, right: string) => {
    const leftRoot = find(left);
    const rightRoot = find(right);
    if (leftRoot !== rightRoot) parent.set(leftRoot, rightRoot);
  };

  for (const item of items) {
    const document = documentsById.get(item.requirement_id ?? '') ?? documentsById.get(item.development_id ?? '');
    for (const target of document ? relationTargets(document.relations) : []) {
      if (items.some((other) => other.id === target || other.development_id === target || other.requirement_id === target)) {
        union(item.id, target);
      }
    }
  }
  for (let outer = 0; outer < items.length; outer += 1) {
    for (let inner = outer + 1; inner < items.length; inner += 1) {
      const left = items[outer];
      const right = items[inner];
      const leftTokens = new Set(titleTokens.get(left.id) ?? []);
      const rightTokens = new Set(titleTokens.get(right.id) ?? []);
      const shared = [...leftTokens].filter((token) => rightTokens.has(token));
      const informative = shared.filter((token) => {
        const df = themeTokenDf.get(token) ?? 0;
        return df >= 2 && df <= tokenCap;
      });
      const rare = shared.some((token) => (globalDf.get(token) ?? 0) === 2 && (themeTokenDf.get(token) ?? 0) >= 2);
      const sharedArea = left.areas.some((area) => {
        if (ruleAreas.includes(area)) return false;
        const df = themeAreaDf.get(area) ?? 0;
        return df >= 2 && df <= areaCap && right.areas.includes(area);
      });
      if (informative.length >= 2 || rare || sharedArea) union(left.id, right.id);
    }
  }

  const groups = new Map<string, WorkItem[]>();
  for (const item of items) {
    const root = find(item.id);
    const bucket = groups.get(root);
    if (bucket) bucket.push(item);
    else groups.set(root, [item]);
  }
  return [...groups.values()]
    .map((bucket) => ({ items: bucket.sort((left, right) => left.updated.localeCompare(right.updated)) }))
    .sort((left, right) => right.items.length - left.items.length || latestOf(right) - latestOf(left));
}

function latestOf(cluster: ItemCluster): number {
  return cluster.items.reduce((max, item) => Math.max(max, Date.parse(item.updated)), 0);
}

interface WeekStats {
  updatedDocs: ProgressDocument[];
  createdDocs: ProgressDocument[];
  archives: ProgressDocument[];
  byArea: Array<{ area: string; docs: ProgressDocument[] }>;
  doneCount: number;
  doneItems: WorkItem[];
  openItems: WorkItem[];
  themes: Array<{ name: string; items: WorkItem[]; clusters: ItemCluster[] }>;
}

function useWeekStats(snapshot: Snapshot, start: string, end: string): WeekStats {
  return useMemo(() => {
    const inWeek = (value: string) => start <= value.slice(0, 10) && value.slice(0, 10) < end;
    const documentsById = new Map(snapshot.documents.map((document) => [document.id, document]));
    const updatedDocs = snapshot.documents
      .filter((document) => inWeek(document.updated))
      .sort((left, right) => right.updated.localeCompare(left.updated));
    const createdDocs = updatedDocs.filter((document) => inWeek(document.created));
    const archives = updatedDocs.filter((document) => document.kind === 'archive');
    const areaMap = new Map<string, ProgressDocument[]>();
    for (const document of updatedDocs) {
      for (const area of document.areas) {
        const bucket = areaMap.get(area);
        if (bucket) bucket.push(document);
        else areaMap.set(area, [document]);
      }
    }
    const byArea = [...areaMap.entries()]
      .map(([area, docs]) => ({ area, docs }))
      .sort((left, right) => right.docs.length - left.docs.length || left.area.localeCompare(right.area));
    const weekItems = snapshot.work_items.filter((item) => inWeek(item.updated));
    const doneItems = weekItems.filter((item) => item.stage === 'done');
    const openItems = weekItems.filter((item) => item.stage !== 'done');
    const themeMap = new Map<string, { items: WorkItem[]; ruleAreas: string[] }>();
    for (const item of doneItems) {
      const theme = resolveTheme(item);
      const bucket = themeMap.get(theme.name);
      if (bucket) bucket.items.push(item);
      else themeMap.set(theme.name, { items: [item], ruleAreas: theme.ruleAreas });
    }
    const themes = [...themeMap.entries()]
      .map(([name, { items, ruleAreas }]) => ({
        name,
        items,
        clusters: clusterItems(items, doneItems, documentsById, ruleAreas),
      }))
      .sort((left, right) => right.items.length - left.items.length || left.name.localeCompare(right.name));
    return {
      updatedDocs,
      createdDocs,
      archives,
      byArea,
      doneCount: doneItems.length,
      doneItems,
      openItems,
      themes,
    };
  }, [snapshot, start, end]);
}

function WeekMetric({ label, value }: { label: string; value: number }) {
  return (
    <div className="rounded-lg border border-slate-800 bg-slate-950/40 px-3 py-2.5">
      <p className="font-mono text-xl leading-none text-cyan-300">{value}</p>
      <p className="mt-1.5 text-[11px] text-slate-500">{label}</p>
    </div>
  );
}

function SummaryRow({
  document: item,
  onOpen,
}: {
  document: ProgressDocument;
  onOpen: (id: string) => void;
}) {
  return (
    <button
      type="button"
      onClick={() => onOpen(item.id)}
      className="group flex w-full items-center gap-3 px-1 py-2.5 text-left transition-colors hover:bg-slate-800/30"
    >
      <FileText className="size-3.5 shrink-0 text-slate-600 group-hover:text-cyan-300" />
      <span className="min-w-0 flex-1">
        <span className="line-clamp-1 text-sm text-slate-300 group-hover:text-cyan-200">{item.title}</span>
        <span className="mt-0.5 line-clamp-1 text-xs text-slate-600">{item.summary}</span>
      </span>
      <span className="shrink-0 text-[10px] text-slate-600">{KIND_LABELS[item.kind] ?? item.kind}</span>
      <span className="shrink-0 font-mono text-[10px] text-slate-600">{readableDate(item.updated)}</span>
    </button>
  );
}

function TopLevelSummary({
  stats,
  snapshot,
  onOpenDocument,
}: {
  stats: WeekStats;
  snapshot: Snapshot;
  onOpenDocument: (id: string) => void;
}) {
  const docIds = useMemo(() => new Set(snapshot.documents.map((document) => document.id)), [snapshot.documents]);
  const targetOf = (item: WorkItem) => {
    const id = item.development_id ?? item.requirement_id;
    return id && docIds.has(id) ? id : null;
  };
  return (
    <section className="mt-6 overflow-hidden rounded-lg border border-cyan-500/20 bg-slate-950/50">
      <header className="border-b border-slate-800 bg-slate-950/70 px-4 py-3">
        <p className="console-label">TOP LEVEL · 上周开发总览</p>
        <p className="mt-2 text-sm leading-6 text-slate-300">
          上周共闭环 <span className="font-mono text-cyan-300">{stats.doneCount}</span> 项开发，
          归入 <span className="font-mono text-cyan-300">{stats.themes.length}</span> 个板块；
          另有 <span className="font-mono text-amber-300">{stats.openItems.length}</span> 项仍在推进。
          板块内高度相近的工作已聚合为一个要点。
        </p>
      </header>
      <div className="divide-y divide-slate-800/80">
        {stats.themes.map((theme) => (
          <div key={theme.name} className="px-4 py-3.5">
            <div className="flex items-center justify-between gap-3">
              <h3 className="text-sm font-medium text-slate-200">{theme.name}</h3>
              <span className="font-mono text-[11px] text-slate-500">{theme.items.length} 项闭环</span>
            </div>
            <ul className="mt-2.5 space-y-1.5">
              {theme.clusters.map((cluster, index) => {
                const latest = cluster.items[cluster.items.length - 1];
                return (
                  <li
                    key={`${theme.name}-${index}`}
                    className="flex items-start gap-2.5 rounded-lg border border-slate-800/70 bg-slate-950/35 px-3 py-2"
                  >
                    <span className="mt-[9px] size-1.5 shrink-0 rounded-full bg-cyan-400/70" />
                    <span className="min-w-0 flex-1 divide-y divide-slate-800/50">
                      {cluster.items.map((item) => {
                        const target = targetOf(item);
                        return target ? (
                          <button
                            key={item.id}
                            type="button"
                            onClick={() => onOpenDocument(target)}
                            className="block w-full py-1 text-left text-[13px] leading-6 text-slate-400 transition-colors hover:text-cyan-100"
                          >
                            {item.title}
                          </button>
                        ) : (
                          <p key={item.id} className="py-1 text-[13px] leading-6 text-slate-400">
                            {item.title}
                          </p>
                        );
                      })}
                    </span>
                    <span className="mt-1 flex shrink-0 flex-col items-end gap-1">
                      {cluster.items.length > 1 && (
                        <span className="rounded border border-cyan-500/25 bg-cyan-400/8 px-1.5 py-0.5 font-mono text-[10px] text-cyan-300">
                          {cluster.items.length} 项
                        </span>
                      )}
                      <span className="font-mono text-[10px] text-slate-600">{readableDate(latest.updated)}</span>
                    </span>
                  </li>
                );
              })}
            </ul>
          </div>
        ))}
      </div>
      {stats.openItems.length > 0 && (
        <footer className="border-t border-slate-800 bg-slate-950/40 px-4 py-3">
          <p className="console-label">仍在推进（未闭环）</p>
          <div className="mt-2 flex flex-wrap gap-1.5">
            {stats.openItems.map((item) => {
              const target = targetOf(item);
              const chip = (
                <>
                  <span className="text-slate-600">{STAGE_LABELS[item.stage] ?? item.stage}</span>
                  <span className="group-hover/chip:text-cyan-200">{item.title}</span>
                </>
              );
              return target ? (
                <button
                  key={item.id}
                  type="button"
                  onClick={() => onOpenDocument(target)}
                  className="relation-chip group/chip"
                >
                  {chip}
                </button>
              ) : (
                <span key={item.id} className="relation-chip cursor-default">
                  {chip}
                </span>
              );
            })}
          </div>
        </footer>
      )}
    </section>
  );
}

export function WeeklySummarySheet({
  snapshot,
  open,
  onOpenChange,
  onOpenDocument,
}: {
  snapshot: Snapshot;
  open: boolean;
  onOpenChange: (open: boolean) => void;
  onOpenDocument: (id: string) => void;
}) {
  const range = useMemo(() => lastWeekRange(), []);
  const stats = useWeekStats(snapshot, range.start, range.end);
  const openDoc = (id: string) => {
    onOpenChange(false);
    onOpenDocument(id);
  };

  return (
    <Sheet open={open} onOpenChange={onOpenChange}>
      <SheetContent
        className="data-[side=right]:w-[min(94vw,860px)] data-[side=right]:sm:max-w-[860px] border-slate-800 bg-[#08111f] p-0"
        showCloseButton
      >
        <SheetHeader className="border-b border-slate-800 bg-slate-950/45 px-6 py-5 pr-14">
          <div className="mb-2 flex items-center gap-2">
            <CalendarRange className="size-4 text-cyan-300" />
            <span className="font-mono text-[10px] tracking-[0.18em] text-slate-500">WEEKLY RETROSPECT</span>
          </div>
          <SheetTitle className="text-xl text-slate-50">上周总结 · {range.display}</SheetTitle>
          <SheetDescription className="text-sm text-slate-500">
            按自然周（周一至周日）从 Markdown 元数据自动汇总，只读；点击条目查看原文档。
          </SheetDescription>
        </SheetHeader>
        <ScrollArea className="min-h-0 flex-1">
          <div className="px-6 py-6 sm:px-8">
            <div className="grid grid-cols-2 gap-2.5 sm:grid-cols-4">
              <WeekMetric label="更新文档" value={stats.updatedDocs.length} />
              <WeekMetric label="新增文档" value={stats.createdDocs.length} />
              <WeekMetric label="归档事实" value={stats.archives.length} />
              <WeekMetric label="完成闭环" value={stats.doneCount} />
            </div>

            {stats.themes.length > 0 ? (
              <TopLevelSummary stats={stats} snapshot={snapshot} onOpenDocument={openDoc} />
            ) : (
              <p className="mt-6 text-sm text-slate-500">上周没有闭环的开发工作项。</p>
            )}

            {stats.byArea.length ? (
              <div className="mt-6 space-y-5">
                <p className="console-label">BY MODULE · 按模块明细</p>
                {stats.byArea.map(({ area, docs }) => (
                  <section key={area} className="overflow-hidden rounded-lg border border-slate-800 bg-slate-900/35">
                    <header className="flex items-center justify-between gap-3 border-b border-slate-800 bg-slate-950/50 px-4 py-2.5">
                      <span className="area-chip">{area}</span>
                      <span className="font-mono text-xs text-slate-500">{docs.length} 篇</span>
                    </header>
                    <div className="divide-y divide-slate-800/70 px-3 py-1">
                      {docs.map((item) => (
                        <SummaryRow key={`${area}-${item.id}`} document={item} onOpen={openDoc} />
                      ))}
                    </div>
                  </section>
                ))}
              </div>
            ) : (
              <p className="mt-6 text-sm text-slate-500">上周没有文档更新记录。</p>
            )}
          </div>
        </ScrollArea>
      </SheetContent>
    </Sheet>
  );
}

export function WeeklySummaryTrigger({
  snapshot,
  onOpenDocument,
}: {
  snapshot: Snapshot;
  onOpenDocument: (id: string) => void;
}) {
  const [open, setOpen] = useState(false);
  const range = useMemo(() => lastWeekRange(), []);
  const stats = useWeekStats(snapshot, range.start, range.end);

  return (
    <>
      <button
        type="button"
        onClick={() => setOpen(true)}
        aria-label="查看上周总结"
        title="查看上周总结"
        className="group flex w-full items-center gap-3 rounded-lg border border-slate-800 bg-slate-950/45 px-4 py-3 text-left transition-colors hover:border-cyan-400/35 hover:bg-slate-800/40"
      >
        <CalendarRange className="size-5 shrink-0 text-cyan-300" />
        <span className="min-w-0 flex-1">
          <span className="block text-sm font-medium text-slate-200 group-hover:text-cyan-200">上周总结</span>
          <span className="mt-0.5 block truncate font-mono text-[11px] text-slate-500">
            {range.display} · 更新 {stats.updatedDocs.length} 篇 · 归档 {stats.archives.length} 条 · 闭环 {stats.doneCount} 项
          </span>
        </span>
        <ChevronRight className="size-4 shrink-0 text-slate-600 transition-transform group-hover:translate-x-0.5 group-hover:text-cyan-300" />
      </button>
      <WeeklySummarySheet
        snapshot={snapshot}
        open={open}
        onOpenChange={setOpen}
        onOpenDocument={onOpenDocument}
      />
    </>
  );
}
