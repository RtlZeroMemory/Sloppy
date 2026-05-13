export const File: {
  readText(path: string, options?: Record<string, unknown>): Promise<string>;
  writeText(path: string, value: string, options?: Record<string, unknown>): Promise<void>;
  readBytes(path: string, options?: Record<string, unknown>): Promise<Uint8Array>;
  writeBytes(path: string, value: Uint8Array | ArrayBuffer, options?: Record<string, unknown>): Promise<void>;
  exists(path: string): Promise<boolean>;
};

export const Directory: {
  create(path: string, options?: Record<string, unknown>): Promise<void>;
  exists(path: string): Promise<boolean>;
  entries(path: string, options?: Record<string, unknown>): Promise<string[]>;
};

export const Path: {
  join(...parts: string[]): string;
  dirname(path: string): string;
  basename(path: string): string;
  extname(path: string): string;
};

export const FileHandle: Record<string, unknown>;
export const FileWatcher: Record<string, unknown>;
