# Framework v2 Current State

Status: current-state note for `FRAMEWORK-V2-01` compiler metadata consolidation. GitHub
issues remain authoritative for live task state.

Framework v2 currently has a compiler/Plan metadata foundation, not full runtime
integration. The compiler can recognize the supported Minimal API typed-handler subset and
emit deterministic metadata for:

- `Sloppy.create()` plus `app.get/post/put/patch/delete(...)` route declarations;
- literal route paths, normalized Plan route patterns, and source spans;
- implicit route/body binding plus explicit `Route<T>`, `Query<T>`, `Body<T>`,
  `Header<"name">`, `Service<T>`, and `Config<T>` wrapper metadata;
- `RequestContext`, `SlopRequest`, `SlopResponse`, `CancellationSignal`, and `Deadline`
  context bindings;
- metadata-only `Postgres<"name">`, `Sqlite<"name">`, `SqlServer<"name">`, and
  `WorkQueue<"name">` injection requirements;
- supported TypeScript aliases/interfaces/object literals, optional and nested properties,
  arrays, nullable unions, literal unions, and selected semantic types;
- password/secret redaction metadata;
- visible `Results.*` status and response metadata where statically visible.

The generated handler for typed multi-parameter Framework v2 routes remains runtime
deferred after the validation boundary. The native HTTP dispatch path now consumes
Plan-backed route/query/header bindings and schema-backed JSON body metadata to return a
safe `400` validation problem before handler invocation when supported metadata fails.
Provider parameters and queue parameters are Plan facts only; they do not register native
providers, create provider handles, perform DI, execute queues, or open SQLite/PostgreSQL/
SQL Server JavaScript bridges. The provider platform is ready for a future Framework v2
injection slice because provider metadata, capability references, runtime features, stdlib
facades, and provider bridge contracts now use one common Db shape; this document is not
that runtime injection implementation.

Still deferred:

- controller/module runtime APIs and constructor injection;
- typed handler execution after successful runtime validation;
- full binding/coercion breadth beyond Plan-backed route/query/header scalars and JSON body
  schema validation;
- provider runtime injection and full provider/DI consumption;
- broader CRUD/background examples that actually execute through runtime provider and queue
  integrations;
- full OpenAPI/security-scheme/exporter completion beyond consuming existing Plan
  metadata.

Issue-state guide:

- `#667`, `#668`, `#669`, `#670`, `#671`, `#674`, and `#675` have compiler metadata
  foundation coverage, but runtime portions stay open unless GitHub has narrower
  completion comments.
- `#672` and `#676` remain runtime/example follow-up work.
- `#673` has a native Plan-backed validation foundation, but remains open if GitHub tracks
  broader coercion, custom validation, or typed-handler execution under the same issue.

Non-claims:

- no full provider runtime integration;
- no full DI container;
- no HTTP/TLS runtime scope from this Framework v2 compiler state;
- no public alpha docs;
- no benchmark or performance claims;
- no Node/Bun/Deno/Express/Nest compatibility.
