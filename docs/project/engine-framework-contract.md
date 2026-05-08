# Engine Framework Contract

Status: ENGINE-01 contract lock. This document is design/source-of-truth only. It does
not claim runtime, compiler, HTTP, V8, or provider implementation.

This contract exists so ENGINE-02 through ENGINE-05 can implement the compiler, V8 async
runtime, HTTP framework runtime, and SQLite path without guessing the public shape. If this
document conflicts with older roadmap language, this document owns the ENGINE-01 handoff
until a later contract PR intentionally changes it.

## Evidence Boundary

Current implementation evidence remains in the current-state docs and public status pages:

- `sloppyc build` has ENGINE-02 method, async metadata, capability/provider metadata, and
  source-map artifact emission for a supported one-file compiler subset.
- `sloppy run --artifacts` has a dev-only V8-required artifact run path for GET routes.
- the current request context has route/query/request basics only.
- ENGINE-03 adds V8-gated microtask-only Promise settlement for returned handler Promises;
  timers, fetch, native async provider queues, and broader JS event-loop behavior remain
  future work.
- ENGINE-12 (#306 through #310) tracks the future scalable async runtime: native
  completion/backend integration, owner-thread continuation scheduling, deadlines,
  shutdown drain/cancel behavior, bounded queues/backpressure, provider/offload
  integration, and stress/performance evidence.
- SQLite has native provider coverage and a V8-gated bridge for
  `data.sqlite("main")`, open/close/exec/query/queryOne, Plan provider resolution, and
  database capability hook checks. JS transactions, prepared statement handles, file
  database policy, and scalable async/offload remain future work.
- FRAMEWORK-01.B implements the first layered application configuration model in the
  compiler/stdlib/source-input path: appsettings overlays, environment selection, canonical
  Sloppy env vars, selected CLI overrides, typed access, `bind`, convention-bound SQLite
  provider config, and Plan-visible redacted metadata.
- Framework v2 alpha examples now include named source-input, DI, controller, SQLite CRUD,
  and opt-in PostgreSQL/SQL Server live-lane examples under `examples/framework-v2-*`.
  Default gates prove only the non-live lanes they run; live-provider examples are not pass
  evidence unless their explicit live lane runs.

This document defines the required final foundation contract, not the current executable
surface.

## Decision Summary

Post-Core framework/API ergonomics are locked in
`docs/project/framework-api-shape.md`. Where this older ENGINE-01 handoff document still
mentions `app.mapGet(...)`, `Sloppy.createBuilder()`, or `data.sqlite("main")`, read those
as the current proven/bootstrap/compiler shapes unless the API-shape document explicitly
names the post-Core target. The locked target is Minimal API first (`app.get/post/...`),
function modules first, explicit provider imports, generated/inferred capabilities by
default, layered Plan-visible config, explicit `ctx` binding helpers, and explicit
`Results.*` descriptors.

| Area | ENGINE-01 decision |
| --- | --- |
| Source workflow | Supported foundation workflows are `sloppyc build app.js --out .sloppy` followed by `sloppy run --artifacts .sloppy --host 127.0.0.1 --port 5173`, plus the source-input dev loop that compiles `sloppy run app.js` / `sloppy run` with `sloppy.json` into the documented cache directory before running artifacts. |
| Direct source run | Supported as a dev-loop compiler handoff for the current compiler subset. It is not a Node/npm loader, package manager, watch mode, hot reload loop, or production server contract. |
| Public import | Core examples use `import { Sloppy, Results, data } from "sloppy";`. This is a compiler/stdlib contract, not Node/npm resolution. |
| App API | Current compiler/runtime support uses `Sloppy.create()` plus `map*` calls; post-Core framework target is `app.get/post/put/patch/delete(...)`, `Results`, request context, cancellation signal, and explicit provider imports. |
| Services/modules | Function modules are the first framework modularization target. The bootstrap app supports route-only `app.useModule(...)`, `Router.group(...)`, and explicit controller route mapping with service constructor injection. Decorators, scanning, and package modules are deferred. |
| HTTP methods | GET, POST, PUT, PATCH, and DELETE are core. OPTIONS is framework-owned for allowed-method/preflight-style responses. HEAD is deferred until its body/metadata policy is explicit. |
| Body support | JSON and text request bodies are core. Multipart/file upload, streaming uploads, and form binding are deferred. |
| Results | Text, JSON, bytes, status, empty, and problem results are core. Files, streams, redirects, cookies, content negotiation, and filters are deferred. |
| Async | Sync handlers and async handlers returning Promises are core. ENGINE-03 covers the bounded V8 owner-thread/microtask policy; ENGINE-12 owns scalable native async completions before Sloppy claims scalable async runtime behavior. |
| Cancellation | Request cancellation signal and native cancellation token plumbing are required from the first real async/HTTP implementation cut. |
| Limits/backpressure | Header, target, query, body, request, response, queue, and resource limits are core infrastructure. Exceeding a limit rejects work with diagnostics. |
| SQLite | SQLite is the core database foundation. `data.sqlite.open`, `exec`, `query`, `queryOne`, `transaction`, and `close` are the foundation API. |
| PostgreSQL/SQL Server | PostgreSQL has a V8-gated true-async bridge over nonblocking libpq and bounded pooling. SQL Server native provider boundaries can remain until the ODBC async bridge lane proves real async behavior. |
| Capabilities | SQLite open/use must be capability-wired. Normal framework apps should receive compiler/Plan-generated provider capabilities by default; manual declarations are advanced-only. Filesystem/network capabilities remain skeleton metadata until APIs exist. No OS sandbox claims. |
| Configuration | Layered Slop-owned application configuration is core. `sloppy.json` remains project/run config; `appsettings*.json` is app config. No Node/npm/package-manager semantics, reload, user secrets, custom providers, or remote config are part of this contract slice. |
| Public alpha docs | Blocked until foundation examples and conformance pass through real compiler/runtime evidence. |
| Benchmarks | Non-claim evidence only until comparable methodology and executable paths exist. |

## Final JS App API

Foundation examples use this shape:

```js
import { Sloppy, Results, data } from "sloppy";

const app = Sloppy.create();

app.mapGet("/users", async ctx => {
  ctx.signal.throwIfAborted();
  return Results.json(await listUsers(ctx));
});

app.mapGet("/users/{id:int}", async ctx => {
  const user = await getUser(ctx.route.id, ctx);
  return user ? Results.ok(user) : Results.notFound();
});

app.mapPost("/users", async ctx => {
  const body = await ctx.request.json();
  const user = await createUser(body, ctx);
  return Results.created(`/users/${user.id}`, user);
});

export default app;
```

Contract decisions:

- Route registration is startup composition only. It must finish before app execution.
- `export default app` is the supported app handoff for compiler extraction.
- `app.mapGet`, `app.mapPost`, `app.mapPut`, `app.mapPatch`, and `app.mapDelete` are
  core route declaration methods.
- Route patterns are compiler-readable literals in the foundation layer.
- Dynamic registration, conditional registration, loops, arbitrary factories, and broad
  module graphs remain rejected until compiler support intentionally expands.
- `app.run`, `app.listen`, and `await app.run()` are deferred. Runtime startup belongs to
  `sloppy run --artifacts` and the source-input `sloppy run <source.js>` / `sloppy run`
  compiler handoff; app code does not own process startup.
- Direct source-input run is a compiler/CLI handoff that emits artifacts before runtime
  startup. It must stay explicit, debuggable, and separate from in-app startup APIs.
- Node builtins, npm packages, package-manager behavior, and arbitrary bare imports are
  rejected unless a future scoped contract adds them.

`Sloppy.createBuilder()`, modules, DI, middleware, filters, validation schemas, and route
groups remain important framework growth paths, but they are not required to finish the
core engine foundation unless a layer explicitly pulls a narrow part forward for evidence.

## Request Context Contract

Handlers receive a request context when they declare one parameter:

```js
{
  route,
  query,
  request,
  signal,
  deadline,
  resources
}
```

`ctx.route`:

- contains matched route parameters as strings;
- validates constrained route segments such as `{id:int}` before handler entry;
- does not coerce values in the foundation layer;
- rejects malformed constrained segments with a framework error before handler execution.

`ctx.query`:

- contains decoded scalar string values;
- uses last-wins semantics for repeated keys in the foundation layer;
- bounds pair count and decoded byte length;
- defers array binding, typed coercion, validation DSL binding, and rich query objects.

`ctx.request`:

- exposes `method`, `path`, and `rawTarget`;
- exposes `headers` through a case-insensitive header bag;
- exposes `json()` and `text()` for bounded body reads;
- does not expose raw native pointers or mutable response objects.

`ctx.request.headers`:

- supports `get(name)` with case-insensitive lookup;
- supports deterministic `entries()` with lower-case names;
- rejects invalid header names and response header values with CR/LF;
- preserves multi-value policy as deterministic comma-join for foundation HTTP/1.1;
- defers cookie helpers, structured header parsing, and content negotiation.

`ctx.signal`:

- is the request cancellation signal available to JS from first async foundation work;
- has `aborted`, `reason`, and `throwIfAborted()`;
- may later grow listener helpers, but native ownership must not depend on JS callbacks.

`ctx.deadline`:

- is derived from host timeout policy when one is configured;
- is absent or `null` when no deadline is configured;
- never replaces `ctx.signal`; deadlines cancel through the same path.

`ctx.resources`:

- is reserved for request-owned handles only when a layer introduces them;
- all request resources must be released on success, exception, rejected Promise,
  cancellation, timeout, and shutdown.

## Results API Contract

Core foundation helpers:

- `Results.text(body, options?)`
- `Results.json(value, options?)`
- `Results.bytes(body, options?)`
- `Results.ok(value?, options?)`
- `Results.created(location, value?, options?)`
- `Results.accepted(value?, options?)`
- `Results.noContent()`
- `Results.notFound(valueOrProblem?, options?)`
- `Results.badRequest(valueOrProblem?, options?)`
- `Results.problem(problemOrMessage?, options?)`
- `Results.status(status, body?, options?)`

Result decisions:

- Result helpers produce plain descriptors that the V8 bridge converts to native HTTP
  responses.
- Plain string handler returns may remain a compatibility fallback, but public examples
  should use `Results.*`.
- JSON result serialization uses V8 `JSON.stringify` or a documented equivalent and fails
  with a diagnostic for unsupported values.
- Problem responses use `application/problem+json; charset=utf-8` by default.
- Error responses must not expose internal stack traces by default.
- Unsupported descriptor kinds fail with a safe framework error response and diagnostic.
- Custom response headers are core only as `options.headers` on supported result helpers;
  header normalization and validation happen before bytes are written.
- Streaming, files, redirects, cookies, compression, keep-alive tuning, middleware filters,
  and content negotiation are deferred.

## Async, Promise, And Microtask Policy

Async handlers are foundation behavior, not a perk.

Required semantics:

- A handler may return a concrete supported value or a Promise for a supported value.
- The request scope stays alive until the returned Promise settles or is cancelled.
- V8 microtasks drain on the owning engine thread after app evaluation, after handler call,
  and at each documented native completion boundary.
- Native completions that resume JS post back to the JS owner thread before Promise
  settlement continues.
- Fulfilled Promises pass through normal result conversion.
- Rejected Promises map to runtime diagnostics and safe error responses.
- Pending Promises at shutdown follow a documented drain-then-cancel policy.
- A Promise must never be converted to `[object Promise]` or reported as success before it
  settles.

Implementation layers may choose the first SQLite strategy honestly:

- sync-backed Promise facade first is acceptable only if docs call it sync-backed;
- operations must check cancellation before start and before result conversion;
- mid-query interruption must not be claimed until the provider has a real interrupt path;
- worker-backed or nonblocking provider execution can follow without changing the public
  cancellation contract.

## Cancellation, Deadlines, Limits, And Backpressure

Cancellation is required infrastructure from the first real async/HTTP foundation cut.

Cancellation sources:

- host shutdown;
- per-request deadline/timeout;
- body/header/queue/resource limit rejection;
- future client disconnect detection;
- explicit native operation cancellation where a provider supports it.

Required behavior:

- every request has a native cancellation token and JS `ctx.signal`;
- native operations receive the token or a documented cancellation snapshot;
- operations check cancellation before start and before result conversion;
- interruptible operations also check or react during execution;
- cancellation triggers request-scope cleanup exactly once;
- diagnostics distinguish cancellation, timeout, queue overflow, and provider failure;
- shutdown drains pending async work up to a bounded policy, then cancels remaining work.

Core limits:

- request target length;
- header count and header byte budget;
- query pair count and decoded query byte budget;
- JSON/text body byte budget;
- response buffer byte budget for buffered results;
- concurrent request count;
- pending completion queue depth;
- pending provider operation count;
- resource table capacity and request-scope handle count.

Backpressure/rejection:

- accepted work must have bounded ownership;
- overflow rejects before entering user code when possible;
- queue overflow after user code has begun maps to a diagnostic and safe error response;
- the runtime must not hide unbounded allocation behind async helpers.

## HTTP Framework Runtime Matrix

| Feature | Foundation target | Deferred |
| --- | --- | --- |
| Methods | GET, POST, PUT, PATCH, DELETE | HEAD until body policy is explicit |
| OPTIONS | Framework-owned allowed-method/preflight-style response | Full CORS policy helpers |
| Route params | Literal, `{name}`, `{name:int}`, strings in `ctx.route` | typed/coerced binding |
| Route precedence | literal before parameter, stable source order for ties, ambiguity diagnostics | optimized trie/catch-all/optional/regex |
| Query | decoded scalar strings, last-wins repeated keys, bounded pairs | arrays and typed binding |
| Headers | case-insensitive `get`, deterministic entries, bounds | cookie helpers/structured headers |
| JSON body | `application/json` and `application/*+json`, bounded, malformed JSON -> 400, Plan-backed schema validation for supported metadata | broader coercion/custom validation |
| Text body | bounded UTF-8 text reads | form parsing, streaming bodies |
| Multipart/upload | unsupported with clear 415/501-style response | file uploads/streaming |
| Result serialization | text, binary bytes, JSON, empty, problem, status, custom headers | files, streams, redirects, cookies |
| Errors | safe framework problem/error responses with diagnostics | production error page customization |
| Server lifecycle | localhost dev server plus deterministic `--once` conformance | Kestrel/Nginx replacement, TLS, HTTP/2 |

This is a framework HTTP runtime target, not a production-grade internet-facing server
claim.

## SQLite Contract

SQLite is the core foundation data provider.

Canonical public shape:

```js
const db = data.sqlite("main");

await db.exec("create table users (id integer primary key, name text not null)");
await db.exec("insert into users (name) values (?)", ["Ada"]);

const users = await db.query("select id, name from users order by id");
const user = await db.queryOne("select id, name from users where id = ?", [1]);

await db.transaction(async tx => {
  await tx.exec("insert into users (name) values (?)", ["Grace"]);
});

await db.close();
```

Decisions:

- `data.sqlite("main")` is the provider-token shorthand for Plan-backed handlers.
- `data.sqlite.open({ database, capability, access })` is the explicit low-level public
  shape. `database` is canonical; `path` is only a transitional alias.
- `:memory:` is core and must have conformance coverage.
- File databases require a capability/path policy before public examples bless them.
- `capability` is required for explicit public SQLite open/use. Provider shorthand obtains
  it from Plan provider metadata.
- `exec`, `query`, `queryOne`, and `close` are implemented foundation operations.
- `transaction` remains part of the foundation API target, but the JS wrapper transaction
  surface is deferred behind the current open/exec/query/queryOne bridge.
- Positional parameters are the foundation parameter style.
- Result rows are plain JS objects with deterministic column naming.
- Transactions are core, but savepoints/isolation-level controls are deferred.
- Prepared statement handles are deferred until resource lifecycle and cancellation are
  solid enough to own them safely.
- ORM, migrations, schema builder, query builder, and raw connection escape hatches are
  deferred.
- PostgreSQL JS bridge support is available in the V8-gated live provider lane. SQL Server
  JS bridge support remains separate until ODBC async evidence lands.

Ownership:

- app-scope connections may exist for simple examples, but must close on app shutdown;
- request-scope connections/resources must close on request completion/cancellation;
- closed/stale/wrong-kind handles fail before provider work;
- all native handles are opaque to JS.

Cancellation:

- open/query/exec/queryOne/transaction receive request cancellation when called from a
  request context;
- sync-backed calls check cancellation before work and before result conversion;
- mid-query SQLite interruption is a separate implementation decision and must have tests
  before docs claim it.

## Capability And Security Contract

Capabilities are runtime policy metadata plus enforcement points, not an OS sandbox.

Foundation requirements:

- Plan metadata declares database capabilities and provider references.
- SQLite open/use checks capability before provider work.
- Denied SQLite access returns a stable diagnostic and does not reach provider execution.
- Capability diagnostics include token, required access, provider kind, and safe route or
  handler identity when known.
- Filesystem/network capability checks remain metadata/skeleton status until APIs exist.
- No docs may claim OS sandboxing, prompt-based grants, Node/Deno permission compatibility,
  or broad host isolation.

## Compiler And Plan Handoff

ENGINE-02 implementation should compile only the shapes this contract blesses:

- `Sloppy.create()` app construction;
- literal route method calls for supported methods;
- literal route patterns;
- default app export;
- handlers with zero parameters or one context parameter;
- async handlers and Promise-returning handlers;
- supported `Results.*` helpers;
- supported `ctx.route`, `ctx.query`, `ctx.request`, and `ctx.signal` reads;
- SQLite/data API usage metadata where static extraction is required for capabilities.

Rejected source shapes must fail with diagnostics and leave no success artifacts.

Plan metadata must carry enough information for the runtime to avoid guessing:

- route table with method/pattern/handler/name/source;
- handler table with stable IDs and display names;
- capabilities and provider references;
- artifact paths/hashes/source map references;
- framework feature flags used by the app;
- resource limit/deadline policy when configured;
- diagnostics/source mapping metadata.

Future OpenAPI, optimization, and static validation can build on the same strong Plan
without requiring the runtime to infer app shape from JS at startup.

## Deferred And Rejected Behavior

Deferred:

- `sloppy run app.ts` / `sloppy run <source>` implementation until #302 has cache, stale-artifact, diagnostics,
  and cleanup acceptance criteria implemented;
- `app.run` and `app.listen`;
- service/module framework expansion beyond narrow proof needs;
- middleware, filters, validation DSL binding, and full OpenAPI schemas;
- HEAD, streaming uploads/downloads, multipart, files, redirects, cookies, compression,
  and production server hardening;
- prepared statements as public handles;
- hosted live-provider defaults for PostgreSQL and SQL Server;
- ORM, migrations, and query builder;
- Node/npm compatibility and package-manager behavior;
- benchmark claims and public alpha docs.

Rejected for the foundation:

- treating a pending Promise as a successful value;
- unbounded queues/buffers/resources hidden behind async APIs;
- direct JS access to native pointers;
- dynamic app discovery at runtime instead of Plan-backed artifacts;
- public examples that cannot execute through the claimed compiler/runtime path;
- security language that implies OS sandboxing.

## Implementation Handoff

ENGINE-02 can implement compiler/Plan completion against this contract.

ENGINE-03 can implement V8 Promise/microtask/cancellation behavior against this contract.

ENGINE-12 can implement the scalable async runtime only after ENGINE-03 semantics are stable
and a real external async source is ready to wire end-to-end. It is the required handoff for
native completion queues, owner-thread continuation scheduling across queued work,
deadline/shutdown drain policy, bounded async admission/backpressure, provider/offload
integration, and stress evidence before any public scalability or performance claim.

ENGINE-04 can implement HTTP methods, headers, bodies, result serialization, limits,
backpressure, and error responses against this contract.

ENGINE-05 can align SQLite public API, capability wiring, resource ownership,
transactions, and conformance examples against this contract.

Each implementation PR should remain a bounded context PR with several related tasks
grouped where reviewable: for example method extraction plus method metadata, or body
parsing plus body limits plus body diagnostics. One tiny task per PR is not required when
the chunk has a coherent module boundary and shared tests.
