# Framework v2 Current State

Status: current-state note for `FRAMEWORK-V2-01` compiler metadata consolidation. GitHub
issues remain authoritative for live task state.

Framework v2 currently has compiler/Plan metadata plus bounded runtime integration for the
supported alpha subset. The compiler can recognize the supported Minimal API typed-handler
subset and emit deterministic metadata for:

- `Sloppy.create()` plus `app.get/post/put/patch/delete(...)` route declarations;
- literal route paths, normalized Plan route patterns, and source spans;
- implicit route/body binding plus explicit `Route<T>`, `Query<T>`, `Body<T>`,
  `Header<"name">`, `Service<T>`, and `Config<T>` wrapper metadata;
- `RequestContext`, `SlopRequest`, `SlopResponse`, `CancellationSignal`, and `Deadline`
  context bindings;
- metadata-only `Postgres<"name">`, `Sqlite<"name">`, `SqlServer<"name">`, and
  `WorkQueue<"name">` injection requirements;
- literal `app.services.addSingleton/addScoped/addTransient("Token", factory)` registrations
  for generated Framework v2 service injection;
- supported TypeScript aliases/interfaces/object literals, optional and nested properties,
  arrays, nullable unions, literal unions, and selected semantic types;
- password/secret redaction metadata;
- visible `Results.*` status and response metadata where statically visible.

The generated handler for typed multi-parameter Framework v2 routes now uses a compiler-owned
JavaScript wrapper instead of the previous generated 501 placeholder. The native HTTP
dispatch path consumes Plan-backed route/query/header bindings and schema-backed JSON body
metadata to return a safe `400` validation problem before handler invocation when supported
metadata fails. After that boundary, the wrapper materializes typed route, query, header,
body, and context arguments and invokes the stripped handler source in the V8 owner-thread
Promise settlement lane. The generated wrapper also creates one request service scope and
resolves `Service<T>` from literal source-level service registrations, including singleton,
scoped, and transient lifetimes plus circular-dependency and singleton-to-scoped diagnostics.
Bootstrap controller/module APIs now cover route-only `app.useModule(...)`,
`Router.group(...)`, nested `app.group(...)` route composition, duplicate route diagnostics,
and explicit `app.mapController(...)` method mapping with constructor injection through the
same service provider. Provider parameters no longer return fake dependencies: SQLite
injection opens the existing Plan-backed stdlib/native bridge, and PostgreSQL or SQL
Server injection attempts the provider bridge and reports the existing
provider/config/live-lane error honestly when the lane is unavailable. Queue and
`Config<T>` parameters still fail explicitly until their runtime lanes can materialize
them. The provider platform is ready for future Framework v2 injection expansion because
provider metadata, capability references, runtime features, stdlib facades, and provider
bridge contracts now use one common Db shape.

Named Framework v2 examples now exist for the current evidence split:

- `examples/framework-v2-hello`: executable TypeScript source-input route binding and
  `Results.ok(...)` response mapping;
- `examples/framework-v2-validation-errors`: schema-backed body binding metadata and
  validation/tooling evidence;
- `examples/framework-v2-explicit-binding`: explicit route/query/header/body/context
  binding metadata;
- `examples/framework-v2-di-services`: executable singleton/scoped/transient service
  injection through a request scope;
- `examples/framework-v2-controller`: bootstrap controller API shape with explicit method
  mapping and constructor injection;
- `examples/framework-v2-sqlite-crud`: V8-gated executable SQLite CRUD source-input
  example;
- `examples/framework-v2-postgres-crud`: opt-in PostgreSQL live-lane source shape with
  deadline propagation and unavailable-diagnostic boundaries;
- `examples/framework-v2-sqlserver-crud`: opt-in SQL Server live-lane source shape with
  deadline propagation and unavailable-diagnostic boundaries.

Still deferred:

- compiler extraction for controller classes and decorator/scanning-style controllers;
- full typed handler breadth beyond the current compiler-emitted wrapper subset;
- full binding/coercion breadth beyond Plan-backed route/query/header scalars and JSON body
  schema validation;
- queue injection, `Config<T>` injection, and broader provider/live-lane evidence;
- background/queue examples that execute through runtime queue integrations;
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

- no queue or config injection runtime;
- no controller class compiler extraction or decorator/scanning support;
- no HTTP/TLS runtime scope from this Framework v2 compiler state;
- no public alpha docs;
- no benchmark or performance claims;
- no Node/Bun/Deno/Express/Nest compatibility.
