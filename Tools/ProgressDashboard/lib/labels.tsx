import { Badge } from '@/components/ui/badge';
import { cn } from '@/lib/utils';

export const KIND_LABELS: Record<string, string> = {
  requirement: '需求',
  development: '开发',
  archive: '归档',
  gameplay: '玩法',
  backlog: 'Backlog',
  reference: '参考',
};

export const CATEGORY_ORDER = ['art', 'gameplay', 'performance'] as const;

export type CategoryKey = (typeof CATEGORY_ORDER)[number];

export const CATEGORY_LABELS: Record<string, string> = {
  art: '美术',
  gameplay: '玩法',
  performance: '性能优化',
};

export const STATUS_LABELS: Record<string, string> = {
  draft: '草案',
  approved: '已确认',
  superseded: '已取代',
  cancelled: '已取消',
  planned: '规划中',
  in_progress: '实施中',
  verification: '待验收',
  done: '完成',
  abandoned: '已放弃',
  recorded: '已记录',
  current: '当前',
  reference: '参考',
  not_run: '未运行',
  partial: '部分通过',
  passed: '通过',
  failed: '失败',
  not_applicable: '不适用',
  inbox: '收件箱',
  promoted: '已提升',
  discarded: '已舍弃',
};

export function statusClass(status: string): string {
  if (status === 'failed') return 'border-red-400/35 bg-red-400/10 text-red-200';
  if (status === 'verification' || status === 'partial')
    return 'border-amber-400/35 bg-amber-400/10 text-amber-200';
  if (status === 'in_progress') return 'border-cyan-400/35 bg-cyan-400/10 text-cyan-200';
  if (status === 'passed' || status === 'done' || status === 'approved')
    return 'border-emerald-400/25 bg-emerald-400/8 text-emerald-200';
  return 'border-slate-600 bg-slate-800/70 text-slate-300';
}

export function StatusBadge({ value }: { value: string }) {
  return (
    <Badge variant="outline" className={cn('font-mono tracking-wide', statusClass(value))}>
      {STATUS_LABELS[value] ?? value}
    </Badge>
  );
}

export function readableDate(value: string): string {
  return value ? value.slice(0, 10) : '—';
}
