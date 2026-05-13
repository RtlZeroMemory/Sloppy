export interface SqlFragment {
  readonly __sloppySql?: true;
}

export interface MigrationOptions {
  provider?: string;
  path?: string;
  [key: string]: unknown;
}

export const sql: {
  sqlite(strings: TemplateStringsArray, ...values: unknown[]): SqlFragment;
  postgres(strings: TemplateStringsArray, ...values: unknown[]): SqlFragment;
  sqlserver(strings: TemplateStringsArray, ...values: unknown[]): SqlFragment;
};

export const data: Record<string, unknown>;
export const Migrations: {
  apply(database: unknown, options?: MigrationOptions): Promise<unknown>;
  run(options?: MigrationOptions): Promise<unknown>;
  status(options?: MigrationOptions): Promise<unknown>;
};
export const ProviderHealth: Record<string, unknown>;
export function isRealDataProvider(value: unknown): boolean;
