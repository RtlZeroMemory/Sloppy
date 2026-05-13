/// <reference path="./providers/sqlite.d.ts" />
/// <reference path="./sloppy.d.ts" />

export type Awaitable<T> = T | Promise<T>;
export type JsonPrimitive = string | number | boolean | null;
export type JsonValue = JsonPrimitive | JsonValue[] | { [key: string]: JsonValue };

export type Route<T = string> = T;
export type Query<T = string> = T;
export type Header<T = string> = T;
export type Body<T = unknown> = T;
export type Service<T = unknown> = T;

export interface SloppyRequest {
  method: string;
  path: string;
  headers?: Record<string, string>;
  query?: Record<string, string | string[]>;
  body?: unknown;
}

export interface RequestBody<T = unknown> {
  value?: T;
  text(): Promise<string>;
  json<TJson = unknown>(): Promise<TJson>;
  validate<TValue>(schema: SchemaType<TValue>): Promise<TValue>;
}

export interface RequestContext {
  request: SloppyRequest;
  route: Record<string, string>;
  routeName?: string;
  requestId?: string;
  services?: Record<string, unknown>;
  body: RequestBody;
  log?: {
    debug(message: string, fields?: Record<string, unknown>): void;
    info(message: string, fields?: Record<string, unknown>): void;
    warn(message: string, fields?: Record<string, unknown>): void;
    error(message: string, fields?: Record<string, unknown>): void;
  };
}

export interface ResultDescriptor<TBody = unknown> {
  readonly __sloppyResult?: true;
  readonly kind?: string;
  readonly status?: number;
  readonly headers?: Record<string, string>;
  readonly contentType?: string;
  readonly body?: TBody;
}

export type Handler<T = unknown> = (ctx: RequestContext) => Awaitable<T | ResultDescriptor<T>>;
export type Middleware = (ctx: RequestContext, next: () => Awaitable<ResultDescriptor>) => Awaitable<ResultDescriptor>;

export interface RouteBuilder {
  withName(name: string): RouteBuilder;
  withTags(...tags: string[]): RouteBuilder;
  produces(status: number, contentType?: string): RouteBuilder;
  accepts(schema: SchemaType, options?: Record<string, unknown>): RouteBuilder;
  returns(schema: SchemaType, options?: Record<string, unknown>): RouteBuilder;
}

export interface RouteGroup {
  use(middleware: Middleware): RouteGroup;
  get(path: string, handler: Handler): RouteBuilder;
  post(path: string, handler: Handler): RouteBuilder;
  put(path: string, handler: Handler): RouteBuilder;
  patch(path: string, handler: Handler): RouteBuilder;
  delete(path: string, handler: Handler): RouteBuilder;
  group(prefix: string): RouteGroup;
}

export interface SloppyApp extends RouteGroup {
  useCors(options?: Record<string, unknown>): SloppyApp;
  useErrors(options?: Record<string, unknown>): SloppyApp;
  useStaticFiles(options: Record<string, unknown>): SloppyApp;
  useModule(module: (app: SloppyApp) => void): SloppyApp;
  provider<T = unknown>(name: string): T;
  mapController(prefix: string, controller: new (...args: any[]) => unknown, configure: (mapper: any) => void): SloppyApp;
}

export interface ServiceBuilder {
  addSingleton<T>(name: string, factory: () => T): ServiceBuilder;
  addScoped<T>(name: string, factory: () => T): ServiceBuilder;
  addTransient<T>(name: string, factory: () => T): ServiceBuilder;
}

export interface SloppyBuilder {
  services: ServiceBuilder;
  build(): SloppyApp;
}

export const Sloppy: {
  create(options?: Record<string, unknown>): SloppyApp;
  createBuilder(options?: Record<string, unknown>): SloppyBuilder;
};

export const Router: {
  group(prefix: string): RouteGroup;
};

export const Results: {
  ok<T = unknown>(value?: T, options?: Record<string, unknown>): ResultDescriptor<T>;
  json<T = unknown>(value: T, options?: Record<string, unknown>): ResultDescriptor<T>;
  text(value: string, options?: Record<string, unknown>): ResultDescriptor<string>;
  html(value: string, options?: Record<string, unknown>): ResultDescriptor<string>;
  bytes(value: Uint8Array | ArrayBuffer, options?: Record<string, unknown>): ResultDescriptor<Uint8Array | ArrayBuffer>;
  created<T = unknown>(location: string, value?: T, options?: Record<string, unknown>): ResultDescriptor<T>;
  created<T = unknown>(value?: T, options?: Record<string, unknown>): ResultDescriptor<T>;
  accepted<T = unknown>(value?: T, options?: Record<string, unknown>): ResultDescriptor<T>;
  noContent(options?: Record<string, unknown>): ResultDescriptor<void>;
  notFound<T = unknown>(value?: T, options?: Record<string, unknown>): ResultDescriptor<T>;
  badRequest<T = unknown>(value?: T, options?: Record<string, unknown>): ResultDescriptor<T>;
  unauthorized<T = unknown>(value?: T, options?: Record<string, unknown>): ResultDescriptor<T>;
  status<T = unknown>(statusCode: number, value?: T, options?: Record<string, unknown>): ResultDescriptor<T>;
  problem(problemOrMessage: string | Record<string, unknown>, options?: Record<string, unknown>): ResultDescriptor<Record<string, unknown>>;
};

export interface SchemaValidation<T> {
  ok: boolean;
  value?: T;
  issues?: Array<{ path: Array<string | number>; code: string; message: string }>;
}

export interface SchemaType<T = unknown> {
  readonly kind: string;
  readonly metadata: Record<string, unknown>;
  validate(value: unknown): SchemaValidation<T>;
  optional(): SchemaType<T | undefined>;
  nullable(): SchemaType<T | null>;
  default(value: T): SchemaType<T>;
  min(value: number): SchemaType<T>;
  max(value: number): SchemaType<T>;
  email(): SchemaType<T>;
}

export const Schema: {
  string(): SchemaType<string>;
  number(): SchemaType<number>;
  int(): SchemaType<number>;
  integer(): SchemaType<number>;
  boolean(): SchemaType<boolean>;
  bool(): SchemaType<boolean>;
  array<T>(item: SchemaType<T>): SchemaType<T[]>;
  enum<T extends string | number | boolean | null>(values: readonly T[]): SchemaType<T>;
  literal<T extends string | number | boolean | null>(value: T): SchemaType<T>;
  object<T extends Record<string, SchemaType>>(shape: T): SchemaType<{ [K in keyof T]: T[K] extends SchemaType<infer V> ? V : never }>;
  validate<T>(value: unknown, schema: SchemaType<T>): T;
  isSchema(value: unknown): value is SchemaType;
};

export const schema: typeof Schema;

export const RequestId: {
  defaults(options?: Record<string, unknown>): Middleware;
};

export const RequestLogging: {
  defaults(options?: Record<string, unknown>): Middleware;
};

export const ProblemDetails: {
  defaults(options?: Record<string, unknown>): Middleware;
};
export const Auth: Record<string, unknown>;
export const Config: Record<string, unknown>;
export const Health: Record<string, unknown>;
export const Metrics: Record<string, unknown>;
export const RateLimit: Record<string, unknown>;
export const Realtime: Record<string, unknown>;
export const Webhooks: Record<string, unknown>;
export const Testing: Record<string, unknown>;

export * from "./data";
export * from "./fs";
export * from "./os";
