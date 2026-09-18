'use client';

import {
  type ColumnDef,
  type SortingState,
  flexRender,
  getCoreRowModel,
  getFilteredRowModel,
  getSortedRowModel,
  useReactTable,
} from '@tanstack/react-table';
import {
  AlertTriangle,
  Archive,
  Boxes,
  CheckCircle2,
  ChevronRight,
  CircleDashed,
  ClipboardCheck,
  Clock3,
  FileQuestion,
  FileText,
  Film,
  Folder,
  FolderKanban,
  Gamepad2,
  Image as ImageIcon,
  Inbox,
  LayoutDashboard,
  ListChecks,
  LoaderCircle,
  Palette,
  RefreshCw,
  Search,
  ShieldAlert,
  Wrench,
  XCircle,
} from 'lucide-react';
import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import ReactMarkdown from 'react-markdown';
import remarkGfm from 'remark-gfm';

import { Badge } from '@/components/ui/badge';
import { Button } from '@/components/ui/button';
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from '@/components/ui/card';
import { Input } from '@/components/ui/input';
import { ScrollArea } from '@/components/ui/scroll-area';
import {
  Sheet,
  SheetContent,
  SheetDescription,
  SheetHeader,
  SheetTitle,
} from '@/components/ui/sheet';
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from '@/components/ui/table';
import type {
  ArtSourceEntry,
  ArtSourceListing,
  BacklogItem,
  Diagnostic,
  ProgressDocument,
  ProgressDocumentDetail,
  SearchResult,
  Snapshot,
  WorkflowStage,
  WorkItem,
} from '@/lib/progress-types';
import { CATEGORY_LABELS, CATEGORY_ORDER, KIND_LABELS, StatusBadge, readableDate } from '@/lib/labels';
import type { CategoryKey } from '@/lib/labels';
import { cn } from '@/lib/utils';
import { WeeklySummaryTrigger } from '@/components/weekly-summary';

type ViewKey =
  | 'overview'
  | 'search'
  | 'requirements'
  | 'development'
  | 'archive'
  | 'gameplay'
  | 'art'
  | 'backlog'
  | 'quality'
  | 'stage'
  | 'tasks';

const NAVIGATION: Array<{
  key: ViewKey;
  label: string;
  icon: React.ComponentType<{ className?: string }>;
}> = [
  { key: 'overview', label: '战情总览', icon: LayoutDashboard },
  { key: 'search', label: '全文搜索', icon: Search },
  { key: 'requirements', label: '需求', icon: FileQuestion },
  { key: 'development', label: '开发', icon: Wrench },
  { key: 'archive', label: '归档时间线', icon: Archive },
  { key: 'gameplay', label: '玩法模块', icon: Gamepad2 },
  { key: 'art', label: '美术相关', icon: Palette },
  { key: 'backlog', label: '月度 Backlog', icon: Inbox },
  { key: 'quality', label: '文档质量', icon: ShieldAlert },
];

const WORKFLOW: Array<{
  key: WorkflowStage;
  label: string;
  hint: string;
  icon: React.ComponentType<{ className?: string }>;
}> = [
  { key: 'draft', label: '草案', hint: '等待确认', icon: CircleDashed },
  { key: 'planned', label: '规划', hint: '方案与排期', icon: FolderKanban },
  { key: 'in_progress', label: '实施', hint: '正在推进', icon: Wrench },
  { key: 'verification', label: '验收', hint: '等待证据', icon: ClipboardCheck },
  { key: 'done', label: '完成', hint: '已闭环', icon: CheckCircle2 },
];

function stageClass(stage: WorkflowStage): string {
  if (stage === 'in_progress') return 'border-cyan-400/35 bg-cyan-400/8 text-cyan-200';
  if (stage === 'verification') return 'border-amber-400/35 bg-amber-400/8 text-amber-200';
  if (stage === 'done') return 'border-emerald-400/25 bg-emerald-400/7 text-emerald-200';
  return 'border-slate-600/70 bg-slate-800/55 text-slate-300';
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  return `${(bytes / 1024).toFixed(1)} KB`;
}

function documentCategories(document: ProgressDocument): string[] {
  return (document.categories ?? []).filter((category) => CATEGORY_LABELS[category]);
}

function matchesCategories(document: ProgressDocument, selected: string[]): boolean {
  if (!selected.length) return true;
  const categories = documentCategories(document);
  return selected.some((category) => categories.includes(category));
}

function CategoryTag({ category }: { category: string }) {
  return <span className={`category-tag category-tag-${category}`}>{CATEGORY_LABELS[category]}</span>;
}

function CategoryFilterBar({
  documents,
  selected,
  onChange,
}: {
  documents: ProgressDocument[];
  selected: string[];
  onChange: (next: string[]) => void;
}) {
  const counts = useMemo(() => {
    const result: Record<string, number> = {};
    for (const key of CATEGORY_ORDER) result[key] = 0;
    for (const document of documents) {
      for (const category of documentCategories(document)) {
        if (category in result) result[category] += 1;
      }
    }
    return result;
  }, [documents]);
  const toggle = (key: CategoryKey) =>
    onChange(selected.includes(key) ? selected.filter((item) => item !== key) : [...selected, key]);
  return (
    <fieldset className="flex flex-wrap items-center gap-2 border-0 p-0">
      <legend className="console-label sr-only">按分类筛选（可多选）</legend>
      <span aria-hidden="true" className="console-label">分类</span>
      {CATEGORY_ORDER.map((key) => {
        const active = selected.includes(key);
        return (
          <button
            key={key}
            type="button"
            aria-pressed={active}
            onClick={() => toggle(key)}
            title={active ? '点击取消该分类筛选' : '点击仅叠加该分类（可多选）'}
            className={cn('category-chip', active && 'category-chip-active')}
          >
            {CATEGORY_LABELS[key]}
            <span className="font-mono text-[10px] opacity-75">{counts[key]}</span>
          </button>
        );
      })}
      {selected.length > 0 && (
        <button
          type="button"
          onClick={() => onChange([])}
          className="ml-1 font-mono text-[10px] text-slate-500 underline decoration-dotted underline-offset-4 hover:text-cyan-300"
        >
          清除
        </button>
      )}
    </fieldset>
  );
}

function SectionHeading({ eyebrow, title, description }: { eyebrow: string; title: string; description: string }) {
  return (
    <div className="mb-5 flex flex-col gap-1.5">
      <p className="console-label">{eyebrow}</p>
      <h2 className="text-xl font-semibold tracking-tight text-slate-50 sm:text-2xl">{title}</h2>
      <p className="max-w-3xl text-sm leading-6 text-slate-400">{description}</p>
    </div>
  );
}

function EmptyState({ label }: { label: string }) {
  return (
    <div className="flex min-h-44 flex-col items-center justify-center gap-3 rounded-lg border border-dashed border-slate-700 bg-slate-900/35 text-center">
      <Boxes className="size-6 text-slate-600" />
      <p className="text-sm text-slate-500">{label}</p>
    </div>
  );
}

function WorkflowRail({
  snapshot,
  onSelectStage,
}: {
  snapshot: Snapshot;
  onSelectStage: (stage: WorkflowStage) => void;
}) {
  return (
    <div className="workflow-rail">
      {WORKFLOW.map((stage, index) => {
        const Icon = stage.icon;
        const count = snapshot.stats.by_stage[stage.key] ?? 0;
        return (
          <React.Fragment key={stage.key}>
            <button
              type="button"
              onClick={() => onSelectStage(stage.key)}
              aria-label={`查看${stage.label}阶段工作项（${count} 个）`}
              title={`查看${stage.label}阶段的 ${count} 个工作项`}
              className={cn('workflow-node workflow-node-clickable', stageClass(stage.key))}
            >
              <div className="flex items-start justify-between gap-3">
                <Icon className="size-4" />
                <span className="font-mono text-2xl font-semibold leading-none">{count}</span>
              </div>
              <div>
                <p className="font-medium text-slate-100">{stage.label}</p>
                <p className="mt-0.5 text-[11px] text-slate-500">{stage.hint}</p>
              </div>
            </button>
            {index < WORKFLOW.length - 1 && <ChevronRight className="workflow-arrow" />}
          </React.Fragment>
        );
      })}
    </div>
  );
}

function TaskMeter({
  done,
  total,
  onOpenDetails,
}: {
  done: number;
  total: number;
  onOpenDetails: () => void;
}) {
  const value = total ? Math.round((done / total) * 100) : 0;
  return (
    <div>
      <div className="mb-2 flex items-end justify-between gap-4">
        <div>
          <p className="text-3xl font-semibold tracking-tight text-slate-50">{value}%</p>
          <p className="mt-1 text-xs text-slate-500">已完成 {done} / {total} 项</p>
        </div>
        <button
          type="button"
          onClick={onOpenDetails}
          aria-label="查看任务明细"
          title="查看任务明细"
          className="task-meter-toggle"
        >
          <ListChecks className="size-6 text-cyan-300" />
        </button>
      </div>
      <div className="h-2 overflow-hidden rounded-full bg-slate-800">
        <div className="h-full rounded-full bg-cyan-400 transition-[width]" style={{ width: `${value}%` }} />
      </div>
    </div>
  );
}

function WorkList({
  items,
  documents,
  openDocument,
  empty,
}: {
  items: WorkItem[];
  documents: Map<string, ProgressDocument>;
  openDocument: (id: string, anchor?: string) => void;
  empty: string;
}) {
  if (!items.length) return <EmptyState label={empty} />;
  return (
    <div className="divide-y divide-slate-800/90">
      {items.map((item) => {
        const target = item.development_id ?? item.requirement_id;
        return (
          <button
            key={item.id}
            type="button"
            onClick={() => target && documents.has(target) && openDocument(target)}
            className="group flex w-full items-start gap-3 px-1 py-3 text-left transition-colors hover:bg-slate-800/30"
          >
            <span className={cn('mt-1.5 size-2 shrink-0 rounded-full', item.stage === 'verification' ? 'bg-amber-400' : 'bg-cyan-400')} />
            <span className="min-w-0 flex-1">
              <span className="line-clamp-1 text-sm font-medium text-slate-200 group-hover:text-cyan-200">{item.title}</span>
              <span className="mt-1 line-clamp-1 text-xs text-slate-500">{item.next_action || item.summary}</span>
            </span>
            <span className="shrink-0 font-mono text-[10px] text-slate-600">{readableDate(item.updated)}</span>
          </button>
        );
      })}
    </div>
  );
}

function Overview({
  snapshot,
  openDocument,
  onSelectStage,
  onOpenTasks,
}: {
  snapshot: Snapshot;
  openDocument: (id: string, anchor?: string) => void;
  onSelectStage: (stage: WorkflowStage) => void;
  onOpenTasks: () => void;
}) {
  const documents = useMemo(() => new Map(snapshot.documents.map((document) => [document.id, document])), [snapshot.documents]);
  const rootDevelopment = snapshot.documents.filter(
    (document) => document.kind === 'development' && document.role === 'root',
  );
  const tasks = rootDevelopment.reduce(
    (accumulator, document) => ({
      done: accumulator.done + document.tasks_done,
      total: accumulator.total + document.tasks_total,
    }),
    { done: 0, total: 0 },
  );
  const signalText = (item: WorkItem) => {
    const target = documents.get(item.development_id ?? item.requirement_id ?? '');
    return `${item.next_action} ${target?.status_note ?? ''}`;
  };
  const blocked = snapshot.work_items
    .filter((item) => /(阻塞|失败|未通过|blocked)/i.test(signalText(item)) || item.verification === 'failed')
    .slice(0, 5);
  const pending = snapshot.work_items
    .filter((item) => item.stage === 'draft' || /(待确认|待用户|确认后)/.test(signalText(item)))
    .slice(0, 5);
  const active = snapshot.work_items.filter((item) => item.stage !== 'done');

  return (
    <div>
      <SectionHeading
        eyebrow="LIVE OPERATIONS"
        title="项目战情总览"
        description="状态直接来自 Markdown 元数据；索引、页面与 AI 目录共享同一份事实源。"
      />
      <WorkflowRail snapshot={snapshot} onSelectStage={onSelectStage} />

      <div className="mt-5 grid gap-4 xl:grid-cols-[0.9fr_1.6fr]">
        <Card className="control-card">
          <CardHeader>
            <CardDescription className="console-label">DELIVERY LOAD</CardDescription>
            <CardTitle className="text-slate-100">开发任务完成率</CardTitle>
          </CardHeader>
          <CardContent className="space-y-6">
            <TaskMeter done={tasks.done} total={tasks.total} onOpenDetails={onOpenTasks} />
            <div className="grid grid-cols-3 gap-2 border-t border-slate-800 pt-4">
              <Metric label="活跃工作" value={active.length} />
              <Metric label="待验收" value={snapshot.stats.by_stage.verification ?? 0} tone="amber" />
              <Metric label="文档总数" value={snapshot.stats.documents} />
            </div>
            <WeeklySummaryTrigger snapshot={snapshot} onOpenDocument={openDocument} />
          </CardContent>
        </Card>

        <div className="grid gap-4 md:grid-cols-3">
          <SignalCard
            label="当前阻塞"
            icon={XCircle}
            tone="error"
            count={blocked.length}
          >
            <WorkList items={blocked} documents={documents} openDocument={openDocument} empty="未识别到明确阻塞" />
          </SignalCard>
          <SignalCard label="待确认" icon={Clock3} tone="amber" count={pending.length}>
            <WorkList items={pending} documents={documents} openDocument={openDocument} empty="暂无待确认项" />
          </SignalCard>
          <SignalCard label="最近更新" icon={RefreshCw} tone="cyan" count={Math.min(5, snapshot.work_items.length)}>
            <WorkList
              items={snapshot.work_items.slice(0, 5)}
              documents={documents}
              openDocument={openDocument}
              empty="暂无工作项"
            />
          </SignalCard>
        </div>
      </div>

      <div className="mt-5 grid gap-4 lg:grid-cols-[1.45fr_0.55fr]">
        <Card className="control-card">
          <CardHeader>
            <CardDescription className="console-label">ACTIVE QUEUE</CardDescription>
            <CardTitle className="text-slate-100">当前推进队列</CardTitle>
          </CardHeader>
          <CardContent>
            <WorkQueue items={active.slice(0, 14)} documents={documents} openDocument={openDocument} />
          </CardContent>
        </Card>
        <Card className="control-card">
          <CardHeader>
            <CardDescription className="console-label">SYSTEM HEALTH</CardDescription>
            <CardTitle className="text-slate-100">文档健康</CardTitle>
          </CardHeader>
          <CardContent className="space-y-3">
            <HealthRow label="结构错误" value={snapshot.stats.errors} error={snapshot.stats.errors > 0} />
            <HealthRow label="维护提示" value={snapshot.stats.warnings} />
            <HealthRow label="工作项" value={snapshot.stats.work_items} />
            <HealthRow label="Backlog" value={snapshot.backlog.length} />
            <p className="border-t border-slate-800 pt-3 font-mono text-[10px] leading-5 text-slate-600">
              REV {snapshot.revision}<br />
              {snapshot.generated_at.replace('T', ' ')}
            </p>
          </CardContent>
        </Card>
      </div>
    </div>
  );
}

function Metric({ label, value, tone = 'cyan' }: { label: string; value: number; tone?: 'cyan' | 'amber' }) {
  return (
    <div>
      <p className={cn('font-mono text-xl', tone === 'amber' ? 'text-amber-300' : 'text-cyan-300')}>{value}</p>
      <p className="mt-0.5 text-[11px] text-slate-500">{label}</p>
    </div>
  );
}

function SignalCard({
  label,
  icon: Icon,
  tone,
  count,
  children,
}: {
  label: string;
  icon: React.ComponentType<{ className?: string }>;
  tone: 'cyan' | 'amber' | 'error';
  count: number;
  children: React.ReactNode;
}) {
  const colors = {
    cyan: 'text-cyan-300 border-cyan-400/20',
    amber: 'text-amber-300 border-amber-400/20',
    error: 'text-red-300 border-red-400/20',
  };
  return (
    <Card className="control-card min-w-0">
      <CardHeader className="border-b border-slate-800 pb-3">
        <div className="flex items-center justify-between gap-3">
          <div className={cn('flex items-center gap-2', colors[tone])}>
            <Icon className="size-4" />
            <CardTitle className="text-sm text-slate-200">{label}</CardTitle>
          </div>
          <span className="font-mono text-lg text-slate-400">{count}</span>
        </div>
      </CardHeader>
      <CardContent className="min-h-44 pt-0">{children}</CardContent>
    </Card>
  );
}

function HealthRow({ label, value, error = false }: { label: string; value: number; error?: boolean }) {
  return (
    <div className="flex items-center justify-between border-b border-slate-800/80 pb-3">
      <span className="text-sm text-slate-400">{label}</span>
      <span className={cn('font-mono text-base', error ? 'text-red-300' : 'text-slate-200')}>{value}</span>
    </div>
  );
}

function WorkQueue({
  items,
  documents,
  openDocument,
  empty = '当前没有活跃工作',
}: {
  items: WorkItem[];
  documents: Map<string, ProgressDocument>;
  openDocument: (id: string, anchor?: string) => void;
  empty?: string;
}) {
  if (!items.length) return <EmptyState label={empty} />;
  return (
    <div className="overflow-hidden rounded-lg border border-slate-800">
      <Table>
        <TableHeader>
          <TableRow className="border-slate-800 bg-slate-950/65 hover:bg-slate-950/65">
            <TableHead>工作项</TableHead>
            <TableHead>阶段</TableHead>
            <TableHead>任务</TableHead>
            <TableHead>更新</TableHead>
          </TableRow>
        </TableHeader>
        <TableBody>
          {items.map((item) => {
            const target = item.development_id ?? item.requirement_id;
            return (
              <TableRow key={item.id} className="border-slate-800/80 hover:bg-slate-800/45">
                <TableCell className="max-w-[520px] whitespace-normal">
                  <button
                    type="button"
                    onClick={() => target && documents.has(target) && openDocument(target)}
                    className="text-left"
                  >
                    <span className="line-clamp-1 font-medium text-slate-200 hover:text-cyan-200">{item.title}</span>
                    <span className="mt-1 block line-clamp-1 text-xs text-slate-500">{item.next_action || item.summary}</span>
                  </button>
                </TableCell>
                <TableCell><Badge variant="outline" className={stageClass(item.stage)}>{WORKFLOW.find((stage) => stage.key === item.stage)?.label}</Badge></TableCell>
                <TableCell className="font-mono text-xs text-slate-400">
                  {item.tasks_total ? `${item.tasks_done}/${item.tasks_total}` : '—'}
                </TableCell>
                <TableCell className="font-mono text-xs text-slate-500">{readableDate(item.updated)}</TableCell>
              </TableRow>
            );
          })}
        </TableBody>
      </Table>
    </div>
  );
}

function StageView({
  stage,
  snapshot,
  openDocument,
}: {
  stage: WorkflowStage;
  snapshot: Snapshot;
  openDocument: (id: string, anchor?: string) => void;
}) {
  const documents = useMemo(() => new Map(snapshot.documents.map((document) => [document.id, document])), [snapshot.documents]);
  const config = WORKFLOW.find((item) => item.key === stage);
  const items = snapshot.work_items.filter((item) => item.stage === stage);
  return (
    <div>
      <SectionHeading
        eyebrow="STAGE FOCUS"
        title={`${config?.label ?? stage} · ${items.length} 个工作项`}
        description={`${config?.hint ?? ''}。点击条目打开关联文档；数据与总览流水线卡片共享同一份快照。`}
      />
      <WorkQueue
        items={items}
        documents={documents}
        openDocument={openDocument}
        empty={`没有处于「${config?.label ?? stage}」阶段的工作项`}
      />
    </div>
  );
}

function TasksView({ snapshot, openDocument }: { snapshot: Snapshot; openDocument: (id: string, anchor?: string) => void }) {
  const documents = snapshot.documents.filter(
    (document) => document.kind === 'development' && document.role === 'root',
  );
  const tasks = documents.reduce(
    (accumulator, document) => ({
      done: accumulator.done + document.tasks_done,
      total: accumulator.total + document.tasks_total,
    }),
    { done: 0, total: 0 },
  );
  const rows = [...documents].sort((left, right) => {
    const leftHas = left.tasks_total > 0 ? 1 : 0;
    const rightHas = right.tasks_total > 0 ? 1 : 0;
    if (leftHas !== rightHas) return rightHas - leftHas;
    if (leftHas) return (left.task_progress ?? 0) - (right.task_progress ?? 0);
    return right.updated.localeCompare(left.updated);
  });
  return (
    <div>
      <SectionHeading
        eyebrow="TASK BREAKDOWN"
        title="任务明细"
        description={`根开发文档共 ${documents.length} 篇，任务完成 ${tasks.done} / ${tasks.total} 项；按完成率升序排列，点击标题打开文档。`}
      />
      {rows.length ? (
        <div className="overflow-hidden rounded-lg border border-slate-800 bg-slate-900/45">
          <Table>
            <TableHeader>
              <TableRow className="border-slate-800 bg-slate-950/70 hover:bg-slate-950/70">
                <TableHead>文档</TableHead>
                <TableHead>状态</TableHead>
                <TableHead>任务</TableHead>
                <TableHead>进度</TableHead>
                <TableHead>更新</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {rows.map((document) => {
                const percent = document.tasks_total
                  ? Math.round((document.tasks_done / document.tasks_total) * 100)
                  : null;
                return (
                  <TableRow key={document.id} className="border-slate-800/80 hover:bg-slate-800/45">
                    <TableCell className="max-w-[520px] whitespace-normal">
                      <button type="button" onClick={() => openDocument(document.id)} className="text-left">
                        <span className="line-clamp-1 font-medium text-slate-200 hover:text-cyan-200">{document.title}</span>
                        <span className="mt-1 block font-mono text-[10px] text-slate-600">{document.id}</span>
                      </button>
                    </TableCell>
                    <TableCell><StatusBadge value={document.status} /></TableCell>
                    <TableCell className="font-mono text-xs text-slate-400">
                      {document.tasks_total ? `${document.tasks_done}/${document.tasks_total}` : '—'}
                    </TableCell>
                    <TableCell>
                      {percent === null ? (
                        <span className="font-mono text-xs text-slate-600">—</span>
                      ) : (
                        <div className="flex items-center gap-2">
                          <div className="h-1.5 w-24 overflow-hidden rounded-full bg-slate-800">
                            <div className="h-full rounded-full bg-cyan-400" style={{ width: `${percent}%` }} />
                          </div>
                          <span className="font-mono text-[10px] text-slate-500">{percent}%</span>
                        </div>
                      )}
                    </TableCell>
                    <TableCell className="font-mono text-xs text-slate-500">{readableDate(document.updated)}</TableCell>
                  </TableRow>
                );
              })}
            </TableBody>
          </Table>
        </div>
      ) : (
        <EmptyState label="没有根开发文档" />
      )}
    </div>
  );
}

function DocumentTable({
  documents,
  openDocument,
  placeholder,
  enableCategoryFilter = false,
}: {
  documents: ProgressDocument[];
  openDocument: (id: string, anchor?: string) => void;
  placeholder: string;
  enableCategoryFilter?: boolean;
}) {
  const [sorting, setSorting] = useState<SortingState>([{ id: 'updated', desc: true }]);
  const [filter, setFilter] = useState('');
  const [selectedCategories, setSelectedCategories] = useState<string[]>([]);
  const visibleDocuments = useMemo(
    () =>
      enableCategoryFilter && selectedCategories.length
        ? documents.filter((document) => matchesCategories(document, selectedCategories))
        : documents,
    [documents, selectedCategories, enableCategoryFilter],
  );
  const columns = useMemo<ColumnDef<ProgressDocument>[]>(
    () => [
      {
        accessorKey: 'title',
        header: '文档',
        cell: ({ row }) => (
          <button type="button" onClick={() => openDocument(row.original.id)} className="max-w-xl text-left">
            <span className="line-clamp-1 font-medium text-slate-200 hover:text-cyan-200">{row.original.title}</span>
            <span className="mt-1 block font-mono text-[10px] text-slate-600">{row.original.id}</span>
          </button>
        ),
      },
      {
        accessorKey: 'role',
        header: '层级',
        cell: ({ row }) => <span className="text-xs text-slate-500">{row.original.role === 'root' ? '总览' : '子页'}</span>,
      },
      {
        accessorKey: 'status',
        header: '状态',
        cell: ({ row }) => <StatusBadge value={row.original.status} />,
      },
      {
        id: 'areas',
        accessorFn: (row) => row.areas.join(' '),
        header: '模块',
        cell: ({ row }) => (
          <div className="flex max-w-[240px] flex-wrap gap-1">
            {documentCategories(row.original).map((category) => <CategoryTag key={category} category={category} />)}
            {row.original.areas.slice(0, 3).map((area) => (
              <span key={area} className="area-chip">{area}</span>
            ))}
          </div>
        ),
      },
      {
        accessorKey: 'task_progress',
        header: '任务',
        cell: ({ row }) => (
          <span className="font-mono text-xs text-slate-400">
            {row.original.tasks_total ? `${row.original.tasks_done}/${row.original.tasks_total}` : '—'}
          </span>
        ),
      },
      {
        accessorKey: 'bytes',
        header: '大小',
        cell: ({ row }) => <span className="font-mono text-xs text-slate-500">{formatBytes(row.original.bytes)}</span>,
      },
      {
        accessorKey: 'updated',
        header: '更新',
        cell: ({ row }) => <span className="font-mono text-xs text-slate-500">{readableDate(row.original.updated)}</span>,
      },
    ],
    [openDocument],
  );
  const table = useReactTable({
    data: visibleDocuments,
    columns,
    state: { sorting, globalFilter: filter },
    onSortingChange: setSorting,
    onGlobalFilterChange: setFilter,
    getCoreRowModel: getCoreRowModel(),
    getFilteredRowModel: getFilteredRowModel(),
    getSortedRowModel: getSortedRowModel(),
  });

  return (
    <div>
      <div className="mb-3 flex flex-col gap-3">
        <div className="flex items-center justify-between gap-3">
          <div className="relative w-full max-w-sm">
            <Search className="absolute left-3 top-1/2 size-4 -translate-y-1/2 text-slate-600" />
            <Input
              value={filter}
              onChange={(event) => setFilter(event.target.value)}
              placeholder={placeholder}
              className="border-slate-700 bg-slate-950/60 pl-9 text-slate-200 placeholder:text-slate-600"
            />
          </div>
          <span className="shrink-0 font-mono text-xs text-slate-600">{table.getFilteredRowModel().rows.length} DOCS</span>
        </div>
        {enableCategoryFilter && (
          <CategoryFilterBar documents={documents} selected={selectedCategories} onChange={setSelectedCategories} />
        )}
      </div>
      <div className="overflow-hidden rounded-lg border border-slate-800 bg-slate-900/45">
        <Table>
          <TableHeader>
            {table.getHeaderGroups().map((headerGroup) => (
              <TableRow key={headerGroup.id} className="border-slate-800 bg-slate-950/70 hover:bg-slate-950/70">
                {headerGroup.headers.map((header) => (
                  <TableHead key={header.id}>
                    {header.isPlaceholder ? null : (
                      <button
                        type="button"
                        onClick={header.column.getToggleSortingHandler()}
                        className="text-xs uppercase tracking-wider text-slate-500 hover:text-slate-300"
                      >
                        {flexRender(header.column.columnDef.header, header.getContext())}
                        {{ asc: ' ↑', desc: ' ↓' }[header.column.getIsSorted() as string] ?? ''}
                      </button>
                    )}
                  </TableHead>
                ))}
              </TableRow>
            ))}
          </TableHeader>
          <TableBody>
            {table.getRowModel().rows.length ? (
              table.getRowModel().rows.map((row) => (
                <TableRow key={row.id} className="border-slate-800/80 hover:bg-slate-800/45">
                  {row.getVisibleCells().map((cell) => (
                    <TableCell key={cell.id}>{flexRender(cell.column.columnDef.cell, cell.getContext())}</TableCell>
                  ))}
                </TableRow>
              ))
            ) : (
              <TableRow className="border-0 hover:bg-transparent">
                <TableCell colSpan={columns.length} className="h-36 text-center text-slate-600">没有匹配文档</TableCell>
              </TableRow>
            )}
          </TableBody>
        </Table>
      </div>
    </div>
  );
}

function SearchView({ openDocument }: { openDocument: (id: string, anchor?: string) => void }) {
  const [query, setQuery] = useState('');
  const [kind, setKind] = useState('');
  const [results, setResults] = useState<SearchResult[]>([]);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    const controller = new AbortController();
    if (!query.trim()) {
      return () => controller.abort();
    }
    const timeout = window.setTimeout(async () => {
      setLoading(true);
      try {
        const params = new URLSearchParams({ q: query, limit: '80' });
        if (kind) params.set('kind', kind);
        const response = await fetch(`/api/search?${params}`, { signal: controller.signal });
        if (!response.ok) throw new Error(`搜索失败：HTTP ${response.status}`);
        const payload = (await response.json()) as { results: SearchResult[] };
        setResults(payload.results);
      } catch (error) {
        if ((error as Error).name !== 'AbortError') setResults([]);
      } finally {
        if (!controller.signal.aborted) setLoading(false);
      }
    }, 220);
    return () => {
      window.clearTimeout(timeout);
      controller.abort();
    };
  }, [query, kind]);

  return (
    <div>
      <SectionHeading
        eyebrow="FULL TEXT RETRIEVAL"
        title="全文搜索"
        description="搜索标题、摘要、状态说明、下一步和 Markdown 正文；结果始终只读。"
      />
      <div className="search-console">
        <Search className="size-5 text-cyan-300" />
        <Input
          value={query}
          onChange={(event) => setQuery(event.target.value)}
          placeholder="输入功能、类名、验证结论或归档事实……"
          className="h-12 flex-1 border-0 bg-transparent px-0 text-base text-slate-100 shadow-none focus-visible:ring-0"
        />
        <select value={kind} onChange={(event) => setKind(event.target.value)} className="filter-select">
          <option value="">全部类型</option>
          {Object.entries(KIND_LABELS).map(([value, label]) => <option key={value} value={value}>{label}</option>)}
        </select>
        {loading && query.trim() && <LoaderCircle className="size-4 animate-spin text-slate-500" />}
      </div>
      {!query.trim() ? (
        <EmptyState label="输入关键词开始检索 120 篇核心文档及嵌套参考资料" />
      ) : results.length ? (
        <div className="mt-4 grid gap-3 lg:grid-cols-2">
          {results.map((result) => (
            <button key={result.id} type="button" onClick={() => openDocument(result.id)} className="search-result">
              <div className="flex items-center justify-between gap-3">
                <div className="flex items-center gap-2">
                  <Badge variant="outline" className="border-slate-700 text-slate-400">{KIND_LABELS[result.kind] ?? result.kind}</Badge>
                  <StatusBadge value={result.status} />
                </div>
                <span className="font-mono text-[10px] text-slate-600">{readableDate(result.updated)}</span>
              </div>
              <h3 className="mt-3 line-clamp-1 text-left font-medium text-slate-100">{result.title}</h3>
              <p className="mt-2 line-clamp-3 text-left text-sm leading-6 text-slate-500">{result.excerpt || result.summary}</p>
              <p className="mt-3 truncate text-left font-mono text-[10px] text-cyan-500/70">{result.path}</p>
            </button>
          ))}
        </div>
      ) : (
        <div className="mt-4"><EmptyState label={loading ? '正在检索……' : '没有匹配结果'} /></div>
      )}
    </div>
  );
}

function ArchiveTimeline({ documents, openDocument }: { documents: ProgressDocument[]; openDocument: (id: string, anchor?: string) => void }) {
  const [selectedCategories, setSelectedCategories] = useState<string[]>([]);
  const archives = useMemo(
    () =>
      documents
        .filter((document) => document.kind === 'archive' && document.role === 'root')
        .sort((left, right) => right.created.localeCompare(left.created) || right.updated.localeCompare(left.updated)),
    [documents],
  );
  const visible = useMemo(
    () => archives.filter((document) => matchesCategories(document, selectedCategories)),
    [archives, selectedCategories],
  );
  return (
    <div>
      <SectionHeading
        eyebrow="IMMUTABLE RECORDS"
        title="归档时间线"
        description="增量事实按时间倒序排列；勘误和里程碑通过关系链接历史，不覆盖原记录。"
      />
      <div className="mb-4">
        <CategoryFilterBar documents={archives} selected={selectedCategories} onChange={setSelectedCategories} />
      </div>
      {visible.length ? (
        <div className="relative ml-2 border-l border-slate-800 pl-6">
          {visible.map((document) => (
            <button key={document.id} type="button" onClick={() => openDocument(document.id)} className="timeline-entry group">
              <span className="timeline-dot" />
              <div className="flex flex-wrap items-center gap-2">
                <span className="font-mono text-xs text-cyan-400/80">{document.created}</span>
                <StatusBadge value={document.status} />
                {documentCategories(document).map((category) => <CategoryTag key={category} category={category} />)}
              </div>
              <h3 className="mt-2 text-left font-medium text-slate-200 group-hover:text-cyan-200">{document.title}</h3>
              <p className="mt-1 line-clamp-2 text-left text-sm leading-6 text-slate-500">{document.summary}</p>
            </button>
          ))}
        </div>
      ) : (
        <EmptyState label="当前分类组合下没有归档" />
      )}
    </div>
  );
}

function BacklogView({
  items,
  documents,
  openDocument,
}: {
  items: BacklogItem[];
  documents: ProgressDocument[];
  openDocument: (id: string, anchor?: string) => void;
}) {
  const documentsById = useMemo(() => new Map(documents.map((document) => [document.id, document])), [documents]);
  const documentsByPath = useMemo(() => new Map(documents.map((document) => [document.path, document])), [documents]);
  const resolveTarget = (item: BacklogItem): { id: string; anchor?: string } | null => {
    const requirementMatch = /REQ-[A-Za-z0-9-]+/.exec(item.requirement);
    if (requirementMatch && documentsById.has(requirementMatch[0])) return { id: requirementMatch[0] };
    const source = documentsByPath.get(item.path);
    if (source) return { id: source.id, anchor: item.id };
    return null;
  };
  const openItem = (item: BacklogItem) => {
    const target = resolveTarget(item);
    if (target) openDocument(target.id, target.anchor);
  };
  return (
    <div>
      <SectionHeading
        eyebrow="IDEA INBOX"
        title="月度 Backlog"
        description="未确认点子先停在这里；提升为正式需求后仍保留原条目和追溯关系。点击卡片打开正式需求或原始条目。"
      />
      {items.length ? (
        <div className="grid gap-3 md:grid-cols-2 xl:grid-cols-3">
          {items.map((item) => {
            const target = resolveTarget(item);
            const interactive = Boolean(target);
            return (
              <Card
                key={item.id}
                className={cn('control-card', interactive && 'backlog-card')}
                role={interactive ? 'button' : undefined}
                tabIndex={interactive ? 0 : undefined}
                aria-label={interactive ? `打开 ${item.id} ${item.title}` : undefined}
                onClick={interactive ? () => openItem(item) : undefined}
                onKeyDown={
                  interactive
                    ? (event: React.KeyboardEvent<HTMLDivElement>) => {
                        if (event.key === 'Enter' || event.key === ' ') {
                          event.preventDefault();
                          openItem(item);
                        }
                      }
                    : undefined
                }
              >
                <CardHeader>
                  <div className="flex items-center justify-between gap-3">
                    <span className="font-mono text-xs text-cyan-400">{item.id}</span>
                    <StatusBadge value={item.status} />
                  </div>
                  <CardTitle className="pt-2 text-slate-100 group-hover/card:text-cyan-200">{item.title}</CardTitle>
                </CardHeader>
                <CardContent className="space-y-3 text-sm text-slate-400">
                  <p><span className="text-slate-600">价值 / </span>{item.value || '—'}</p>
                  <p><span className="text-slate-600">待确认 / </span>{item.question || '—'}</p>
                  <div className="flex flex-wrap gap-1">{item.areas.map((area) => <span key={area} className="area-chip">{area}</span>)}</div>
                </CardContent>
              </Card>
            );
          })}
        </div>
      ) : (
        <EmptyState label="本月暂无正式编号的 Backlog 条目" />
      )}
    </div>
  );
}

function DiagnosticList({ title, items, error }: { title: string; items: Diagnostic[]; error: boolean }) {
  return (
    <Card className="control-card">
      <CardHeader className="border-b border-slate-800">
        <div className="flex items-center justify-between gap-3">
          <CardTitle className="text-slate-100">{title}</CardTitle>
          <span className={cn('font-mono text-xl', error ? 'text-red-300' : 'text-amber-300')}>{items.length}</span>
        </div>
      </CardHeader>
      <CardContent className="divide-y divide-slate-800/80">
        {items.length ? items.map((item, index) => (
          <div key={`${item.path}-${item.code}-${index}`} className="py-3">
            <div className="flex flex-wrap items-center gap-2">
              {error ? <XCircle className="size-4 text-red-300" /> : <AlertTriangle className="size-4 text-amber-300" />}
              <code className="text-xs text-slate-300">{item.code}</code>
            </div>
            <p className="mt-2 text-sm text-slate-400">{item.message}</p>
            <p className="mt-1 break-all font-mono text-[10px] text-slate-600">{item.path}</p>
          </div>
        )) : <div className="py-10 text-center text-sm text-slate-600">无</div>}
      </CardContent>
    </Card>
  );
}

function QualityView({ snapshot }: { snapshot: Snapshot }) {
  return (
    <div>
      <SectionHeading
        eyebrow="DOCUMENT GATE"
        title="文档质量"
        description="错误会阻断维护流程；尺寸和任务数量属于拆分提示，不等同于验证失败。"
      />
      <div className="grid gap-4 lg:grid-cols-2">
        <DiagnosticList title="结构错误" items={snapshot.diagnostics.errors} error />
        <DiagnosticList title="维护提示" items={snapshot.diagnostics.warnings} error={false} />
      </div>
    </div>
  );
}

function artMediaUrl(path: string): string {
  return `/api/artsource/file?p=${encodeURIComponent(path)}`;
}

function ArtFolderNode({
  name,
  path,
  depth,
  listings,
  expanded,
  selectedDir,
  onOpen,
}: {
  name: string;
  path: string;
  depth: number;
  listings: Record<string, ArtSourceListing | null>;
  expanded: ReadonlySet<string>;
  selectedDir: string;
  onOpen: (dir: string) => void;
}) {
  const listing = listings[path];
  const isOpen = expanded.has(path);
  const active = selectedDir === path;
  const fileCount = listing ? listing.entries.filter((entry) => entry.type === 'file').length : null;
  const childDirs = listing ? listing.entries.filter((entry) => entry.type === 'dir') : [];
  return (
    <div>
      <button
        type="button"
        onClick={() => onOpen(path)}
        aria-expanded={isOpen}
        title={path}
        className={cn('art-tree-row', active && 'art-tree-row-active')}
        style={{ paddingLeft: `${0.5 + depth * 0.85}rem` }}
      >
        <ChevronRight className={cn('size-3.5 shrink-0 text-slate-600 transition-transform', isOpen && 'rotate-90')} />
        <Folder className="size-3.5 shrink-0 text-slate-500" />
        <span className="truncate">{name}</span>
        {fileCount !== null && fileCount > 0 && (
          <span className="ml-auto shrink-0 font-mono text-[9px] text-slate-600">{fileCount}</span>
        )}
      </button>
      {isOpen &&
        (childDirs.length ? (
          childDirs.map((child) => (
            <ArtFolderNode
              key={child.path}
              name={child.name}
              path={child.path}
              depth={depth + 1}
              listings={listings}
              expanded={expanded}
              selectedDir={selectedDir}
              onOpen={onOpen}
            />
          ))
        ) : (
          <p className="art-tree-empty" style={{ paddingLeft: `${1.6 + depth * 0.85}rem` }}>
            {listing ? '无子目录' : '加载中……'}
          </p>
        ))}
    </div>
  );
}

function ArtSourceBrowser() {
  const [listings, setListings] = useState<Record<string, ArtSourceListing | null>>({});
  const [expanded, setExpanded] = useState<ReadonlySet<string>>(() => new Set());
  const [selectedDir, setSelectedDir] = useState('');
  const [preview, setPreview] = useState<ArtSourceEntry | null>(null);
  const [error, setError] = useState('');
  const requestedRef = useRef<Set<string>>(new Set());

  const loadDir = useCallback((dir: string) => {
    if (requestedRef.current.has(dir)) return;
    requestedRef.current.add(dir);
    setListings((current) => (dir in current ? current : { ...current, [dir]: null }));
    fetch(`/api/artsource?dir=${encodeURIComponent(dir)}`)
      .then((response) => {
        if (!response.ok) throw new Error(`目录读取失败：HTTP ${response.status}`);
        return response.json() as Promise<ArtSourceListing>;
      })
      .then((listing) => {
        setListings((current) => ({ ...current, [listing.dir]: listing }));
        setError('');
      })
      .catch((reason: Error) => {
        requestedRef.current.delete(dir);
        setListings((current) => {
          if (current[dir]) return current;
          const next = { ...current };
          delete next[dir];
          return next;
        });
        setError(reason.message);
      });
  }, []);

  useEffect(() => {
    loadDir('');
  }, [loadDir]);

  useEffect(() => {
    if (!preview) return;
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') setPreview(null);
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [preview]);

  const selectFolder = useCallback(
    (dir: string) => {
      loadDir(dir);
      setSelectedDir(dir);
    },
    [loadDir],
  );
  const openFolder = useCallback(
    (dir: string) => {
      selectFolder(dir);
      setExpanded((current) => {
        const next = new Set(current);
        if (next.has(dir)) next.delete(dir);
        else next.add(dir);
        return next;
      });
    },
    [selectFolder],
  );

  const selectedListing = listings[selectedDir];
  const childDirs = (selectedListing?.entries ?? []).filter((entry) => entry.type === 'dir');
  const mediaEntries = (selectedListing?.entries ?? []).filter(
    (entry) => entry.type === 'file' && (entry.media === 'image' || entry.media === 'video'),
  );
  const otherEntries = (selectedListing?.entries ?? []).filter(
    (entry) => entry.type === 'file' && entry.media !== 'image' && entry.media !== 'video',
  );
  const breadcrumbs = selectedDir ? selectedDir.split('/') : [];

  return (
    <div className="grid gap-4 lg:grid-cols-[240px_minmax(0,1fr)]">
      <div className="rounded-lg border border-slate-800 bg-slate-950/40 p-2">
        <p className="console-label mb-2 px-2 pt-1">ArtSource</p>
        {listings[''] === undefined ? (
          <div className="flex justify-center py-6">
            <LoaderCircle className="size-4 animate-spin text-cyan-300" />
          </div>
        ) : (
          (listings['']?.entries ?? [])
            .filter((entry) => entry.type === 'dir')
            .map((entry) => (
              <ArtFolderNode
                key={entry.path}
                name={entry.name}
                path={entry.path}
                depth={0}
                listings={listings}
                expanded={expanded}
                selectedDir={selectedDir}
                onOpen={openFolder}
              />
            ))
        )}
      </div>

      <div className="min-w-0">
        {error && <p className="mb-3 text-xs text-red-300">{error}</p>}
        <div className="mb-3 flex flex-wrap items-center gap-2 text-xs text-slate-500">
          <button type="button" onClick={() => selectFolder('')} className="hover:text-cyan-200">ArtSource</button>
          {breadcrumbs.map((segment, index) => {
            const dir = breadcrumbs.slice(0, index + 1).join('/');
            return (
              <span key={dir} className="flex items-center gap-2">
                <ChevronRight className="size-3 text-slate-700" />
                <button type="button" onClick={() => selectFolder(dir)} className="hover:text-cyan-200">{segment}</button>
              </span>
            );
          })}
          {selectedListing && (
            <span className="ml-auto font-mono text-[10px] text-slate-600">
              {childDirs.length} 目录 · {mediaEntries.length} 媒体 · {otherEntries.length} 文件
            </span>
          )}
        </div>

        {selectedDir === '' ? (
          <div className="grid gap-3 sm:grid-cols-2 xl:grid-cols-3">
            {childDirs.map((entry) => (
              <button key={entry.path} type="button" onClick={() => openFolder(entry.path)} className="art-folder-card">
                <Folder className="size-5 shrink-0 text-cyan-300/70" />
                <span className="min-w-0 flex-1">
                  <span className="block truncate text-sm font-medium text-slate-200">{entry.name}</span>
                  <span className="mt-0.5 block font-mono text-[10px] text-slate-600">
                    {entry.dir_count} 子目录 / {entry.file_count} 文件
                  </span>
                </span>
                <ChevronRight className="ml-auto size-4 shrink-0 text-slate-600" />
              </button>
            ))}
          </div>
        ) : !selectedListing ? (
          <div className="flex justify-center py-10">
            <LoaderCircle className="size-5 animate-spin text-cyan-300" />
          </div>
        ) : (
          <div className="space-y-4">
            {childDirs.length > 0 && (
              <div className="flex flex-wrap gap-2">
                {childDirs.map((entry) => (
                  <button key={entry.path} type="button" onClick={() => openFolder(entry.path)} className="art-subfolder-chip">
                    <Folder className="size-3.5 text-slate-500" />
                    <span className="truncate">{entry.name}</span>
                    <span className="font-mono text-[9px] text-slate-600">{entry.dir_count + entry.file_count}</span>
                  </button>
                ))}
              </div>
            )}
            {mediaEntries.length ? (
              <div className="art-gallery">
                {mediaEntries.map((entry) => (
                  <button key={entry.path} type="button" className="art-card" onClick={() => setPreview(entry)} title={entry.path}>
                    {entry.media === 'video' ? (
                      <video src={artMediaUrl(entry.path)} muted preload="metadata" className="art-card-media" />
                    ) : (
                      <img src={artMediaUrl(entry.path)} alt={entry.name} loading="lazy" className="art-card-media" />
                    )}
                    <span className="art-card-caption">
                      <span className="flex min-w-0 items-center gap-1.5">
                        {entry.media === 'video' ? (
                          <Film className="size-3 shrink-0 text-cyan-300/70" />
                        ) : (
                          <ImageIcon className="size-3 shrink-0 text-cyan-300/70" />
                        )}
                        <span className="truncate">{entry.name}</span>
                      </span>
                      <span className="shrink-0 font-mono text-[9px] text-slate-600">{formatBytes(entry.bytes)}</span>
                    </span>
                  </button>
                ))}
              </div>
            ) : (
              <EmptyState label="此目录没有可直接预览的图片或视频" />
            )}
            {otherEntries.length > 0 && (
              <div>
                <p className="console-label mb-2">其他文件 · {otherEntries.length}</p>
                <div className="flex flex-wrap gap-2">
                  {otherEntries.map((entry) => (
                    <span key={entry.path} className="art-file-chip" title={entry.path}>
                      <FileText className="size-3 shrink-0 text-slate-500" />
                      <span className="max-w-56 truncate">{entry.name}</span>
                      <span className="font-mono text-[9px] text-slate-600">{formatBytes(entry.bytes)}</span>
                    </span>
                  ))}
                </div>
              </div>
            )}
          </div>
        )}
      </div>

      {preview && (
        <div className="art-lightbox">
          <button
            type="button"
            aria-label="关闭预览"
            onClick={() => setPreview(null)}
            className="art-lightbox-backdrop"
          />
          <figure className="art-lightbox-figure">
            {preview.media === 'video' ? (
              <video src={artMediaUrl(preview.path)} controls autoPlay className="art-lightbox-media" />
            ) : (
              <img src={artMediaUrl(preview.path)} alt={preview.name} className="art-lightbox-media" />
            )}
            <figcaption className="flex items-center gap-3 text-xs text-slate-400">
              <span className="min-w-0 flex-1 truncate">{preview.path}</span>
              <span className="shrink-0 font-mono text-[10px] text-slate-600">{formatBytes(preview.bytes)}</span>
              <button type="button" onClick={() => setPreview(null)} aria-label="关闭预览" className="task-meter-toggle">
                <XCircle className="size-4 text-slate-400 hover:text-red-300" />
              </button>
            </figcaption>
          </figure>
        </div>
      )}
    </div>
  );
}

function ArtView({ snapshot, openDocument }: { snapshot: Snapshot; openDocument: (id: string, anchor?: string) => void }) {
  const artDocuments = useMemo(
    () => snapshot.documents.filter((document) => documentCategories(document).includes('art')),
    [snapshot.documents],
  );
  const kindSummary = useMemo(() => {
    const counts: Record<string, number> = {};
    for (const document of artDocuments) counts[document.kind] = (counts[document.kind] ?? 0) + 1;
    return Object.entries(counts)
      .map(([kind, count]) => `${KIND_LABELS[kind] ?? kind} ${count}`)
      .join(' · ');
  }, [artDocuments]);
  return (
    <div>
      <SectionHeading
        eyebrow="ART PIPELINE"
        title="美术相关"
        description="上方汇总 Progress 中标记美术分类的文档；下方只读浏览 ArtSource/ 素材目录（参考图、风格调整与制作审核证据），点击图片或视频可放大查看。"
      />
      <Card className="control-card mb-5">
        <CardHeader className="border-b border-slate-800 pb-4">
          <CardDescription className="console-label">ART DOCS</CardDescription>
          <CardTitle className="text-slate-100">美术文档 · {artDocuments.length} 篇</CardTitle>
          {kindSummary && <p className="pt-1 font-mono text-[10px] text-slate-600">{kindSummary}</p>}
        </CardHeader>
        <CardContent className="pt-5">
          <DocumentTable documents={artDocuments} openDocument={openDocument} placeholder="筛选美术文档、模块或 ID……" />
        </CardContent>
      </Card>
      <Card className="control-card">
        <CardHeader className="border-b border-slate-800 pb-4">
          <CardDescription className="console-label">ARTSOURCE BROWSER</CardDescription>
          <CardTitle className="text-slate-100">ArtSource 素材目录</CardTitle>
          <p className="pt-1 font-mono text-[10px] text-slate-600">D:/UE5.7/test1/ArtSource · 只读 · Esc 关闭预览</p>
        </CardHeader>
        <CardContent className="pt-5">
          <ArtSourceBrowser />
        </CardContent>
      </Card>
    </div>
  );
}

function resolveMarkdownDocument(sourcePath: string, href: string, documentsByPath: Map<string, ProgressDocument>): ProgressDocument | undefined {
  if (!href || /^(?:https?:|mailto:|#)/i.test(href)) return undefined;
  const rawPath = decodeURIComponent(href.split('#', 1)[0]);
  const parts = sourcePath.split('/');
  parts.pop();
  for (const part of rawPath.replace(/\\/g, '/').split('/')) {
    if (!part || part === '.') continue;
    if (part === '..') parts.pop();
    else parts.push(part);
  }
  return documentsByPath.get(parts.join('/'));
}

function DocumentDrawer({
  documentId,
  anchor,
  documents,
  onClose,
  openDocument,
}: {
  documentId: string | null;
  anchor: string | null;
  documents: ProgressDocument[];
  onClose: () => void;
  openDocument: (id: string, anchor?: string) => void;
}) {
  const [detail, setDetail] = useState<ProgressDocumentDetail | null>(null);
  const [loadError, setLoadError] = useState<{ id: string; message: string } | null>(null);
  const bodyRef = useRef<HTMLDivElement | null>(null);
  const documentsById = useMemo(() => new Map(documents.map((document) => [document.id, document])), [documents]);
  const documentsByPath = useMemo(() => new Map(documents.map((document) => [document.path, document])), [documents]);

  useEffect(() => {
    if (!documentId) return;
    const controller = new AbortController();
    fetch(`/api/documents/${encodeURIComponent(documentId)}`, { signal: controller.signal })
      .then((response) => {
        if (!response.ok) throw new Error(`读取失败：HTTP ${response.status}`);
        return response.json() as Promise<ProgressDocumentDetail>;
      })
      .then((value) => {
        setDetail(value);
        setLoadError(null);
      })
      .catch((reason: Error) => {
        if (reason.name !== 'AbortError') setLoadError({ id: documentId, message: reason.message });
      });
    return () => controller.abort();
  }, [documentId]);

  useEffect(() => {
    if (!detail || detail.id !== documentId || !anchor) return;
    const container = bodyRef.current;
    if (!container) return;
    const headings = Array.from(container.querySelectorAll('h1, h2, h3, h4'));
    const target = headings.find((heading) => (heading.textContent ?? '').includes(anchor));
    if (!target) return;
    target.scrollIntoView({ block: 'start' });
    target.classList.add('drawer-anchor-target');
    const timeout = window.setTimeout(() => target.classList.remove('drawer-anchor-target'), 2400);
    return () => {
      window.clearTimeout(timeout);
      target.classList.remove('drawer-anchor-target');
    };
  }, [detail, documentId, anchor]);

  const relationRows = detail
    ? Object.entries(detail.relations).flatMap(([label, value]) => {
        const values = Array.isArray(value) ? value : value === null || value === false ? [] : [String(value)];
        return values.map((item) => ({ label, value: item, target: documentsById.get(item) }));
      })
    : [];

  return (
    <Sheet open={Boolean(documentId)} onOpenChange={(open) => !open && onClose()}>
      <SheetContent
        className="data-[side=right]:w-[min(94vw,1152px)] data-[side=right]:sm:max-w-[1152px] border-slate-800 bg-[#08111f] p-0"
        showCloseButton
      >
        {!detail || detail.id !== documentId ? (
          loadError?.id === documentId ? (
            <div className="flex h-full flex-col items-center justify-center gap-3 text-red-200"><XCircle className="size-7" /><p>{loadError.message}</p></div>
          ) : (
          <div className="flex h-full items-center justify-center"><LoaderCircle className="size-6 animate-spin text-cyan-300" /></div>
          )
        ) : detail ? (
          <>
            <SheetHeader className="border-b border-slate-800 bg-slate-950/45 px-6 py-5 pr-14">
              <div className="mb-2 flex flex-wrap items-center gap-2">
                <Badge variant="outline" className="border-slate-700 text-slate-400">{KIND_LABELS[detail.kind] ?? detail.kind}</Badge>
                <StatusBadge value={detail.status} />
                <StatusBadge value={detail.verification} />
                {detail.role === 'detail' && <Badge variant="outline" className="border-cyan-500/25 text-cyan-300">子页</Badge>}
              </div>
              <SheetTitle className="text-xl leading-7 text-slate-50">{detail.title}</SheetTitle>
              <SheetDescription className="font-mono text-[10px] text-slate-600">{detail.id} · {detail.path}</SheetDescription>
            </SheetHeader>
            <ScrollArea className="min-h-0 flex-1">
              <div ref={bodyRef} className="px-6 py-6 sm:px-8">
                <div className="grid gap-3 sm:grid-cols-2">
                  <InfoBlock label="摘要" value={detail.summary} />
                  <InfoBlock label="下一步" value={detail.next_action || '—'} tone="cyan" />
                  <InfoBlock label="状态说明" value={detail.status_note || '—'} />
                  <InfoBlock label="范围" value={detail.areas.join(' / ') || '—'} />
                </div>
                {relationRows.length > 0 && (
                  <div className="mt-5 border-y border-slate-800 py-4">
                    <p className="console-label mb-3">RELATIONS</p>
                    <div className="flex flex-wrap gap-2">
                      {relationRows.map((relation, index) => (
                        <button
                          key={`${relation.label}-${relation.value}-${index}`}
                          type="button"
                          disabled={!relation.target}
                          onClick={() => relation.target && openDocument(relation.target.id)}
                          className="relation-chip disabled:cursor-default"
                        >
                          <span className="text-slate-600">{relation.label}</span>
                          <span>{relation.target?.title ?? relation.value}</span>
                        </button>
                      ))}
                    </div>
                  </div>
                )}
                {detail.headings.length > 1 && (
                  <details className="mt-5 rounded-lg border border-slate-800 bg-slate-950/35 p-4">
                    <summary className="cursor-pointer text-sm font-medium text-slate-300">目录 · {detail.headings.length} 节</summary>
                    <ol className="mt-3 space-y-1.5 text-xs text-slate-500">
                      {detail.headings.slice(1).map((heading, index) => (
                        <li key={`${heading.title}-${index}`} style={{ paddingLeft: `${Math.max(0, heading.level - 2) * 12}px` }}>{heading.title}</li>
                      ))}
                    </ol>
                  </details>
                )}
                <article className="markdown-body mt-7">
                  <ReactMarkdown
                    remarkPlugins={[remarkGfm]}
                    skipHtml
                    components={{
                      a: ({ href = '', children }) => {
                        const target = resolveMarkdownDocument(detail.path, href, documentsByPath);
                        if (target) {
                          return <button type="button" className="markdown-link" onClick={() => openDocument(target.id)}>{children}</button>;
                        }
                        return <a href={href} target={/^https?:/i.test(href) ? '_blank' : undefined} rel="noreferrer">{children}</a>;
                      },
                    }}
                  >
                    {detail.body}
                  </ReactMarkdown>
                </article>
              </div>
            </ScrollArea>
          </>
        ) : null}
      </SheetContent>
    </Sheet>
  );
}

function InfoBlock({ label, value, tone }: { label: string; value: string; tone?: 'cyan' }) {
  return (
    <div className="rounded-lg border border-slate-800 bg-slate-950/35 p-3">
      <p className="console-label">{label}</p>
      <p className={cn('mt-2 text-sm leading-6', tone === 'cyan' ? 'text-cyan-200' : 'text-slate-400')}>{value}</p>
    </div>
  );
}

function ViewContent({
  view,
  snapshot,
  openDocument,
  stageFilter,
  onSelectStage,
  onOpenTasks,
}: {
  view: ViewKey;
  snapshot: Snapshot;
  openDocument: (id: string, anchor?: string) => void;
  stageFilter: WorkflowStage;
  onSelectStage: (stage: WorkflowStage) => void;
  onOpenTasks: () => void;
}) {
  if (view === 'overview')
    return (
      <Overview
        snapshot={snapshot}
        openDocument={openDocument}
        onSelectStage={onSelectStage}
        onOpenTasks={onOpenTasks}
      />
    );
  if (view === 'stage') return <StageView stage={stageFilter} snapshot={snapshot} openDocument={openDocument} />;
  if (view === 'tasks') return <TasksView snapshot={snapshot} openDocument={openDocument} />;
  if (view === 'search') return <SearchView openDocument={openDocument} />;
  if (view === 'archive') return <ArchiveTimeline documents={snapshot.documents} openDocument={openDocument} />;
  if (view === 'backlog')
    return <BacklogView items={snapshot.backlog} documents={snapshot.documents} openDocument={openDocument} />;
  if (view === 'quality') return <QualityView snapshot={snapshot} />;
  if (view === 'art') return <ArtView snapshot={snapshot} openDocument={openDocument} />;

  const config = {
    requirements: {
      kind: 'requirement',
      eyebrow: 'PRODUCT CONTRACTS',
      title: '需求文档',
      description: '已确认边界与草案状态以 front matter 为准；work_id 是需求和开发的权威关系。',
      placeholder: '筛选需求、模块或 ID……',
      categoryFilter: true,
    },
    development: {
      kind: 'development',
      eyebrow: 'IMPLEMENTATION PLANS',
      title: '开发文档',
      description: '技术方案、任务勾选和验证状态集中查看；长文档子页保留同一 work_id。',
      placeholder: '筛选开发方案、模块或 ID……',
      categoryFilter: true,
    },
    gameplay: {
      kind: 'gameplay',
      eyebrow: 'GAMEPLAY SOURCEBOOK',
      title: '玩法模块',
      description: '玩法总册记录当前规则与验证边界，历史事实通过归档追溯。',
      placeholder: '筛选玩法模块……',
      categoryFilter: false,
    },
  }[view];
  const documents = snapshot.documents.filter((document) => document.kind === config.kind);
  return (
    <div>
      <SectionHeading eyebrow={config.eyebrow} title={config.title} description={config.description} />
      <DocumentTable
        documents={documents}
        openDocument={openDocument}
        placeholder={config.placeholder}
        enableCategoryFilter={config.categoryFilter}
      />
    </div>
  );
}

export function ProgressDashboard() {
  const [view, setView] = useState<ViewKey>('overview');
  const [stageFilter, setStageFilter] = useState<WorkflowStage>('in_progress');
  const [snapshot, setSnapshot] = useState<Snapshot | null>(null);
  const [selectedDocument, setSelectedDocument] = useState<string | null>(null);
  const [selectedAnchor, setSelectedAnchor] = useState<string | null>(null);
  const [error, setError] = useState('');
  const [refreshing, setRefreshing] = useState(false);

  const loadSnapshot = useCallback(async (quiet = false) => {
    if (!quiet) setRefreshing(true);
    try {
      const response = await fetch('/api/snapshot', { cache: 'no-store' });
      if (!response.ok) throw new Error(`快照读取失败：HTTP ${response.status}`);
      setSnapshot((await response.json()) as Snapshot);
      setError('');
    } catch (reason) {
      setError((reason as Error).message);
    } finally {
      if (!quiet) setRefreshing(false);
    }
  }, []);

  useEffect(() => {
    const initial = window.setTimeout(() => void loadSnapshot(), 0);
    const timer = window.setInterval(() => void loadSnapshot(true), 15_000);
    return () => {
      window.clearTimeout(initial);
      window.clearInterval(timer);
    };
  }, [loadSnapshot]);

  const openDocument = useCallback((id: string, anchor?: string) => {
    setSelectedDocument(id);
    setSelectedAnchor(anchor ?? null);
  }, []);
  const openStage = useCallback((stage: WorkflowStage) => {
    setStageFilter(stage);
    setView('stage');
  }, []);
  const openTasks = useCallback(() => setView('tasks'), []);

  return (
    <div className="min-h-screen bg-[#060d18] text-slate-200">
      <header className="sticky top-0 z-40 border-b border-slate-800/90 bg-[#07101d]/95 backdrop-blur">
        <div className="mx-auto flex h-16 max-w-[1800px] items-center gap-4 px-4 sm:px-6">
          <div className="flex min-w-0 items-center gap-3">
            <div className="flex size-9 items-center justify-center rounded-md border border-cyan-400/30 bg-cyan-400/8 font-mono text-xs font-bold text-cyan-300">G//S</div>
            <div className="min-w-0">
              <p className="truncate text-sm font-semibold tracking-wide text-slate-100">PROGRESS CONTROL</p>
              <p className="truncate font-mono text-[9px] tracking-[0.18em] text-slate-600">LOCAL · READ ONLY · 127.0.0.1</p>
            </div>
          </div>
          <div className="ml-auto flex items-center gap-2">
            {snapshot && (
              <div className="hidden items-center gap-2 border-r border-slate-800 pr-4 text-xs text-slate-500 sm:flex">
                <span className={cn('size-2 rounded-full', snapshot.stats.errors ? 'bg-red-400' : 'bg-emerald-400')} />
                {snapshot.stats.errors ? `${snapshot.stats.errors} 个错误` : '结构正常'}
              </div>
            )}
            <Button
              variant="ghost"
              size="icon-sm"
              onClick={() => void loadSnapshot()}
              aria-label="刷新快照"
              className="text-slate-500 hover:bg-slate-800 hover:text-cyan-200"
            >
              <RefreshCw className={cn('size-4', refreshing && 'animate-spin')} />
            </Button>
          </div>
        </div>
      </header>

      <div className="mx-auto grid max-w-[1800px] grid-cols-1 lg:grid-cols-[220px_minmax(0,1fr)]">
        <aside className="border-b border-slate-800 bg-[#08111f] lg:sticky lg:top-16 lg:h-[calc(100vh-4rem)] lg:border-b-0 lg:border-r">
          <nav className="flex gap-1 overflow-x-auto p-3 lg:flex-col lg:p-4" aria-label="进度视图">
            {NAVIGATION.map((item) => {
              const Icon = item.icon;
              const active =
                view === item.key ||
                (item.key === 'overview' && (view === 'stage' || view === 'tasks'));
              return (
                <button
                  key={item.key}
                  type="button"
                  onClick={() => setView(item.key)}
                  className={cn('nav-item', active && 'nav-item-active')}
                >
                  <Icon className="size-4 shrink-0" />
                  <span className="whitespace-nowrap">{item.label}</span>
                  {active && <span className="ml-auto hidden font-mono text-[9px] text-cyan-500 lg:block">ACTIVE</span>}
                </button>
              );
            })}
          </nav>
          {snapshot && (
            <div className="mx-4 mt-2 hidden border-t border-slate-800 pt-4 lg:block">
              <p className="console-label mb-3">SOURCE STATUS</p>
              <div className="space-y-2 font-mono text-[10px] text-slate-600">
                <p className="flex justify-between"><span>DOCUMENTS</span><span className="text-slate-400">{snapshot.stats.documents}</span></p>
                <p className="flex justify-between"><span>WORK ITEMS</span><span className="text-slate-400">{snapshot.stats.work_items}</span></p>
                <p className="flex justify-between"><span>WARNINGS</span><span className="text-amber-400">{snapshot.stats.warnings}</span></p>
              </div>
            </div>
          )}
        </aside>

        <main className="min-w-0 px-4 py-6 sm:px-6 lg:px-8 lg:py-8">
          {error && (
            <div className="mb-5 flex items-center gap-3 rounded-lg border border-red-400/30 bg-red-400/8 px-4 py-3 text-sm text-red-200">
              <XCircle className="size-4 shrink-0" />{error}
            </div>
          )}
          {!snapshot ? (
            <div className="flex min-h-[60vh] flex-col items-center justify-center gap-3 text-slate-500">
              <LoaderCircle className="size-6 animate-spin text-cyan-300" />
              <p className="font-mono text-xs tracking-widest">LOADING SOURCE OF TRUTH</p>
            </div>
          ) : (
            <ViewContent
              view={view}
              snapshot={snapshot}
              openDocument={openDocument}
              stageFilter={stageFilter}
              onSelectStage={openStage}
              onOpenTasks={openTasks}
            />
          )}
        </main>
      </div>

      {snapshot && (
        <DocumentDrawer
          documentId={selectedDocument}
          anchor={selectedAnchor}
          documents={snapshot.documents}
          onClose={() => {
            setSelectedDocument(null);
            setSelectedAnchor(null);
          }}
          openDocument={openDocument}
        />
      )}
    </div>
  );
}
