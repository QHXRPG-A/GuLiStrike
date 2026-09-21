export type WorkflowStage =
  | 'draft'
  | 'planned'
  | 'in_progress'
  | 'blocked'
  | 'verification'
  | 'done';

export type RelationValue = string | string[] | boolean | null;

export interface Heading {
  level: number;
  title: string;
}

export interface ProgressDocument {
  schema: string;
  id: string;
  work_id: string;
  kind: string;
  role: string;
  title: string;
  areas: string[];
  categories?: string[];
  status: string;
  verification: string;
  created: string;
  updated: string;
  summary: string;
  next_action: string;
  relations: Record<string, RelationValue>;
  status_note: string;
  path: string;
  bytes: number;
  headings: Heading[];
  tasks_total: number;
  tasks_done: number;
  task_progress: number | null;
}

export interface ProgressDocumentDetail extends ProgressDocument {
  body: string;
}

export interface WorkItem {
  id: string;
  title: string;
  stage: WorkflowStage;
  areas: string[];
  updated: string;
  summary: string;
  next_action: string;
  verification: string;
  requirement_id: string | null;
  development_id: string | null;
  archive_ids: string[];
  tasks_total: number;
  tasks_done: number;
  task_progress: number | null;
}

export interface BacklogItem {
  id: string;
  title: string;
  status: string;
  areas: string[];
  value: string;
  question: string;
  requirement: string;
  path: string;
}

export interface Diagnostic {
  path: string;
  code: string;
  message: string;
}

export interface Snapshot {
  schema: string;
  generated_at: string;
  revision: string;
  stats: {
    documents: number;
    work_items: number;
    by_kind: Record<string, number>;
    by_stage: Record<string, number>;
    errors: number;
    warnings: number;
  };
  work_items: WorkItem[];
  backlog: BacklogItem[];
  diagnostics: {
    errors: Diagnostic[];
    warnings: Diagnostic[];
  };
  documents: ProgressDocument[];
}

export interface SearchResult extends ProgressDocument {
  score: number;
  excerpt: string;
}

export interface ArtSourceEntry {
  name: string;
  path: string;
  type: 'dir' | 'file';
  media: 'image' | 'video' | 'text' | null;
  bytes: number;
  modified: string;
  dir_count: number;
  file_count: number;
}

export interface ArtSourceListing {
  dir: string;
  parent: string | null;
  entries: ArtSourceEntry[];
}
