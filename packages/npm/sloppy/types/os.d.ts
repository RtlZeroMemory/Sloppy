export const Environment: {
  get(name: string, fallback?: string): string | undefined;
  require(name: string): string;
};

export const System: {
  platform(): string;
  arch(): string;
  cwd(): string;
};

export const Process: Record<string, unknown>;
export const Signals: Record<string, unknown>;
export class OsError extends Error {}
