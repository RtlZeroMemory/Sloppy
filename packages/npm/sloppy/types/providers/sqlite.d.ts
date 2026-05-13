export interface SqliteProviderOptions {
  database?: string;
  readonly?: boolean;
  migrations?: string;
  [key: string]: unknown;
}

export function sqlite(name: string, options?: SqliteProviderOptions): any;
