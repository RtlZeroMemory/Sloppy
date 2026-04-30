# Slop Engine Final Shape

Status: strategic target shape before higher-level framework perks.

This is not a public alpha document and not a feature implementation plan by itself. It
defines the intended engine/framework foundation that must be coherent before Sloppy moves
up-stack into broader ergonomics, public launch docs, benchmarks, or provider expansion.

ENGINE-01 locks the concrete framework contract for implementation handoff in
`docs/project/engine-framework-contract.md`. That contract is the immediate source of truth
for ENGINE-02 through ENGINE-05 decisions around JS app shape, Results, request context,
async/microtasks, cancellation, HTTP, SQLite, capabilities, and deferred behavior.
The post-Core framework ergonomics target is locked in
`docs/project/framework-api-shape.md`; it supersedes older aspirational examples where they
conflict with Minimal API `app.get/post/...`, function modules, explicit provider imports,
inferred capabilities, layered config, and Plan-first DSL extraction.

## 1. Runtime Philosophy

Sloppy is a compiler-planned TypeScript backend runtime/app-host.

The intended foundation has these invariants:

- C runtime kernel owns the portable host, Plan validation, routing, lifecycle, resources,
  diagnostics, and provider boundaries.
- V8 executes JavaScript through an isolated C++ bridge under `src/engine/v8/*`; V8 types do
  not leak into core modules.
- `app.plan.json` is source-of-truth runtime metadata, not an incidental sidecar.
- Artifacts are deterministic: `app.plan.json`, generated JS, source maps, hashes, and
  compatibility metadata must be stable for the same source/toolchain inputs.
- Unsupported behavior is explicit and diagnostic-driven.
- Sloppy is not Node/npm compatible by default. Node, npm, package-manager, or arbitrary
  import behavior requires a scoped future decision.

Core foundation also means the primitive contracts cannot be happy-path-only. Every core
runtime boundary should carry the standard infrastructure needed for scale and predictable
performance:

- cancellation from request entry through async/native operation completion and cleanup;
- deadline/timeout hooks built on that cancellation path, even if public timeout helpers
  arrive later;
- bounded buffers, body sizes, queue depths, and resource budgets instead of unbounded
  growth;
- backpressure or explicit rejection when work cannot be accepted safely;
- monotonic time and lifecycle state owned by platform/runtime layers, not ad hoc globals;
- deterministic cleanup for success, failure, cancellation, shutdown, and startup abort;
- diagnostics and evidence gates that say which path was actually exercised.

Those are foundation requirements, not framework perks. Higher-level middleware,
validation, OpenAPI polish, package-manager behavior, and production hosting breadth remain
later layers.

The async target is intentionally bigger than ENGINE-03. ENGINE-03 proves real returned
Promise settlement for the bounded V8 owner-thread microtask boundary. ENGINE-12 tracks
the full generic async runtime with native completion queues, an owner-thread V8
continuation scheduler, deadline and shutdown wakeups, cancellation tokens that cross
JS/native work, bounded async admission, explicit backpressure, provider/offload policy
hooks, and stress evidence for many pending operations. That target is tracked by
ENGINE-12 (#306 through #310).

Provider execution is intentionally split from generic async. ENGINE-23 turns the
provider/offload policy from ENGINE-12 into a first-class provider runtime with owned
operation descriptors, per-provider-instance executors, serialized SQLite-class blocking
offload, bounded blocking pools, future nonblocking provider mode, capability-gated
admission, deterministic cancellation/timeout/shutdown/late-completion behavior, worker
lifecycle, diagnostics, and stress evidence. ENGINE-23 must land before Sloppy claims
scalable provider execution or completes SQLite runtime work that depends on off-owner
thread provider execution.

ENGINE-12 should be implemented when Sloppy has a real external async source to wire end to
end: HTTP disconnect/shutdown cancellation, timer/deadline wakeups, async SQLite/provider
work, or worker-pool offload. It is required before public alpha docs, benchmark
methodology, or product language claim scalable async behavior, production-ready async HTTP
lifecycle, async provider execution, or performance with many pending requests.

ENGINE-13 through ENGINE-20 complete the rest of the engine foundation after the async and
provider runtime layers: proper HTTP runtime backend, module/bootstrap completion, source
maps and diagnostics, app/resource lifetime, SQLite data runtime completion, CLI/dev loop,
conformance compatibility, and the strong Plan strategic layer. The source document is
`docs/project/archive/post-core-mvp/engine-13-plus-architecture.md`; the created issue map is
`docs/project/archive/post-core-mvp/engine-13-plus-issue-index.md`.

ENGINE-21 and ENGINE-22 make memory/string handling explicit foundation work rather than
miscellaneous helper cleanup. ENGINE-21 defines app/request/temp/static/V8/SQLite/
diagnostic lifetimes, allocation and failure rules, string/byte/owned-buffer primitives,
builders, formatting helpers, bounded app/static string interning and symbol tables, and
V8/SQLite conversion policy. ENGINE-22 adopts those primitives in hot paths: HTTP
parse/write/body, V8 conversions, SQLite rows/parameters, diagnostics/source frames/JSON,
Plan/artifact loading, stable metadata lookup, CLI output, and allocation-aware
conformance. The issue map is `docs/project/post-core-mvp-memory-string-audit.md`. Public alpha docs
remain blocked until this layer is complete or explicitly scoped down with honest
exclusions.

## 2. Developer Workflow

Target core workflow:

```powershell
sloppyc build app.js --out .sloppy
sloppy run --artifacts .sloppy --host 127.0.0.1 --port 5173
```

`sloppyc build` is the compiler boundary. It emits the artifacts the native runtime needs
to validate, route, host, diagnose, and execute the app.

`sloppy run --artifacts` is the runtime boundary. It does not guess source state; it loads a
validated artifact directory.

Optional future:

```powershell
sloppy run app.ts
```

That shortcut is allowed only if a deliberate source-input handoff is implemented:
compiler invocation, cache keys, stale artifact detection, diagnostic mapping, and artifact
cleanup. It must not become an implicit fake build path.

## 3. App Model

The final core API should remain small and in the spirit of ASP.NET Minimal API:

```js
import { Sloppy, Results, data } from "sloppy";

const app = Sloppy.create();

app.get("/users", async ctx => {
  ctx.signal.throwIfAborted();
  return Results.json(await ctx.services.users.list());
});

app.get("/users/{id:int}", async ctx => {
  return Results.json(await ctx.services.users.get(ctx.route.id));
});

app.post("/users", async ctx => {
  const body = await ctx.request.json();
  return Results.created(`/users/${body.id}`, body);
});

export default app;
```

Core engine foundation owns:

- app creation and freeze/build boundary;
- route registration for supported methods;
- request context shape;
- result serialization contract;
- data/capability bridge for SQLite;
- lifecycle ownership for app/request resources;
- cancellation/deadline/backpressure primitives that the app API can expose without
  redesign.

Service and module registration can be part of the core app-host contract only where it is
needed to make realistic API apps work before public alpha. Rich module packages,
middleware ecosystems, DI container ergonomics, validation frameworks, and plugins belong
to later framework expansion.

## 4. Async Model

Async handlers are core foundation, not a perk.

Final policy:

- route handlers may return either a supported result value or a Promise for a supported
  result value;
- returned Promises keep the request scope alive until settlement;
- fulfilled Promises serialize the resolved result exactly like sync returns;
- rejected Promises become deterministic route-aware diagnostics and safe error responses;
- V8 microtasks drain under an explicit bridge policy on the isolate owner thread;
- native completions post back to the owner loop before JavaScript resumes;
- every request owns a cancellation token/signal from entry through cleanup;
- cancellation is visible to JavaScript handlers and native async operations through one
  documented request signal;
- server shutdown, host abort, body-limit rejection, and future client disconnects
  transition the request signal before resource cleanup;
- backpressure is explicit: when queues, bodies, or native work limits are exceeded, the
  runtime rejects work with diagnostics instead of growing without bound;
- native worker threads must never enter a V8 isolate directly.

Current reality after ENGINE-03: returned handler Promises that settle during the explicit
V8 owner-thread microtask drain are real in V8-enabled builds. Fulfilled Promises serialize
like sync returns, rejected Promises become deterministic diagnostics, request-scope
cleanup runs for the bounded call, and still-pending Promises fail as deadline-style
handler failures. Native async completion queues, timers, fetch, worker-thread scheduling,
and HTTP disconnect/shutdown drain behavior remain future ENGINE-12 work.

Initial native async and cancellation policy:

- SQLite may begin as a sync facade on the owner thread for tiny/local examples only if
  docs and tests say so clearly.
- Long-running or blocking provider work needs a worker-backed strategy before it is
  described as scalable.
- The worker-backed strategy is ENGINE-23 provider execution, not ENGINE-13 HTTP backend
  work and not libuv's global threadpool.
- Cancellation-token infrastructure is required from the first real async foundation cut:
  request context, native operation submission, completion settlement, diagnostics, and
  shutdown paths must all carry the token/signal even if a given operation is not yet
  interruptible mid-call.
- Deadline/timeout policy is built on cancellation. A minimal first cut may only support
  host-driven cancellation, but it must not require redesign to add per-request deadlines.
- SQLite sync-facade work must check cancellation before starting and before converting
  results. It must not claim mid-query interruption until a real provider interruption path
  exists.

No fake async claims: an `async` API that blocks the owner thread may be acceptable as an
initial SQLite policy only if the docs call it a sync-backed promise facade and examples are
bounded.

## 5. HTTP Model

Sloppy needs a framework HTTP runtime for local API apps. It does not need to be a
production-grade Kestrel/Nginx replacement before alpha.

Core method support target:

- `GET`;
- `POST`;
- `PUT`;
- `PATCH`;
- `DELETE`;
- `OPTIONS` for framework-owned route discovery/preflight behavior if explicitly scoped.

`HEAD` can remain parser-supported but framework-dispatch policy should be explicit before
claiming support.

Core HTTP foundation must define:

- route precedence: exact/literal before parameter routes, stable tie behavior, duplicate
  rejection;
- route params with documented supported constraints;
- query params with repeated-key policy and bounds;
- request headers in context, normalized enough for app use without hiding raw input;
- request cancellation signal in context, wired to shutdown and future client-disconnect
  behavior;
- request body policy with size limits;
- header count/line limits, body limits, queue limits, and bounded response buffers;
- timeout/deadline hooks for accept/read/body/handler phases, even if public timeout
  helpers are later;
- JSON body parse policy and diagnostics;
- text body policy;
- unsupported multipart/file upload policy;
- result serialization for text, JSON, no-content, problem/error, created, and status
  helpers;
- error responses for route miss, method mismatch, body too large, malformed JSON,
  unsupported media type, handler exception, rejected Promise, and malformed result;
- localhost server lifecycle: bind, serve, stop, cleanup, diagnostics;
- backpressure policy: reject or stop reading when bounded queues/buffers are full; do not
  imply unbounded scalability;
- production hardening boundary: no TLS/HTTP2/keep-alive/compression/static-file claims
  until scoped.

ENGINE-13 supplies the HTTP semantic foundation for that path: parser, lifecycle, body
policy, cancellation/shutdown state, response serialization, and bounded smoke. ENGINE-24
is the transport foundation that must turn those semantics into real TCP/libuv execution:
bind/listen/accept/read/write/close, Content-Length-only request bodies, close-after-
response, bounded buffers/admission, disconnect cancellation, graceful stop, and localhost
socket/curl evidence. ENGINE-17.E users API proof should run over ENGINE-24 transport
rather than the CLI-local dev socket loop.

## 6. V8 Model

Final V8 foundation:

- one isolate/context has a clear owner thread;
- core C never exposes V8 types;
- app artifact evaluation is deterministic;
- bootstrap runtime is loaded through documented assets or generated artifacts;
- supported module strategy is explicit: either classic generated artifact for foundation,
  or true ESM loading once implemented;
- handler registration uses runtime-owned IDs validated against Plan handlers;
- module/cache strategy avoids global mutable hidden state;
- source maps map runtime diagnostics back to author source where claimed;
- exceptions and Promise rejections become stable diagnostics;
- microtasks drain through explicit bridge policy;
- cancellation is checked before resuming JS continuations and before serializing async
  handler results;
- shutdown releases V8-owned resources and closes engine-owned native resources.

Global V8 platform teardown can remain deferred if per-engine cleanup is correct and docs
say what is intentionally process-lifetime.

## 7. Compiler Model

The compiler is not an arbitrary JS/TS bundler.

Final foundation source support should cover supported Sloppy apps:

- public `sloppy` facade import;
- default exported app;
- route registrations for core HTTP methods;
- literal/static route patterns and simple groups;
- inline and named handlers under documented closure/import rules;
- async handlers;
- request cancellation reads through the supported context shape;
- request context reads used by handlers;
- Results helper calls;
- SQLite/data API declarations needed for Plan capabilities/providers;
- deterministic handler IDs and route metadata;
- useful diagnostics for rejected shapes.

Rejected shapes must fail before success artifacts:

- arbitrary package imports;
- Node builtins;
- dynamic route registration unless a future dynamic mode exists;
- unsupported top-level side effects;
- unsupported TypeScript syntax before TS lowering exists;
- unsupported async/data patterns.

Compiler output target:

- generated JS artifact;
- real source map;
- Plan metadata for routes, handlers, capabilities, providers, artifacts/hashes, and source
  mapping;
- Plan/runtime metadata for resource limits, required capabilities, and compatibility
  versioning;
- deterministic diagnostic output.

## 8. Plan Model

Strongly typed `app.plan.json` is a competitive advantage.

Strategic Plan responsibilities:

- route table;
- handler table;
- capability declarations;
- provider declarations;
- artifact paths and hashes;
- runtime compatibility;
- diagnostics/source mapping;
- resource limits and capability requirements;
- cancellation/deadline policy metadata where a host-level policy is configured;
- future OpenAPI metadata;
- future optimization metadata;
- future static validation.

The Plan lets the native runtime validate and prepare the app before JavaScript execution,
and lets CLI tools inspect routes/security/evidence without running handlers.

The Plan should stay strict enough to protect the runtime, but not so rigid that early user
ergonomics cannot be corrected.

## 9. SQLite Model

SQLite is the core foundation database story.

Target public JS API:

```js
const db = data.sqlite("main");

await db.exec("create table users (id integer primary key, name text not null)");
await db.exec("insert into users (name) values (?)", ["Ada"]);

const users = await db.query("select id, name from users");
const ada = await db.queryOne("select id, name from users where id = ?", [1]);

await db.close();
```

Foundation decisions:

- JS never sees raw native pointers;
- SQLite bridge uses resource IDs and generation checks;
- a declared capability is required for open/use and is checked by the SQLite bridge before
  provider work;
- `query`, `queryOne`, and `exec` are core;
- transactions are core if examples need realistic write flows, but JS wrapper transaction
  semantics can be one layer after open/query/exec if documented;
- prepared statements need an explicit decision: either defer them or implement as scoped
  resources with cleanup;
- public SQLite operations must be cancellation-aware through the request context or an
  explicit options bag, depending on the Layer 1 API decision;
- app-scope versus request-scope connections must be documented;
- `:memory:` examples are core for conformance;
- file DB policy requires capability/path rules before public docs;
- PostgreSQL and SQL Server JS bridges are deferred until SQLite is excellent.
- SQLite's scalable provider path depends on ENGINE-23 serialized blocking execution for
  SQLite-class provider instances.

## 10. Security / Capabilities

Capabilities are runtime authorization metadata and enforcement hooks. They are not an OS
sandbox claim.

Foundation requirements:

- capability registry from Plan metadata;
- enforcement at SQLite bridge open/use;
- denied diagnostics with provider/token/access context;
- redaction for secrets and connection-like values;
- metadata audit/doctor surfaces;
- clear statement that filesystem/network capabilities are skeleton/future until APIs
  exist;
- no claim that Sloppy confines arbitrary process/file/network behavior at the OS level.

Future extension can add filesystem, network, prompts, sandbox research, and richer policy,
but foundation public docs must not imply those are present.

## 11. Diagnostics

Diagnostics are product surface.

Foundation diagnostics:

- compiler diagnostics with stable codes, spans, source frames, and hints;
- runtime startup diagnostics for Plan/artifact/hash/compatibility failures;
- V8 exception diagnostics mapped through source maps where claimed;
- Promise rejection diagnostics with route/handler context;
- cancellation, timeout, backpressure, and resource-limit diagnostics with stable codes;
- HTTP diagnostics for malformed request, unsupported body, body limits, malformed JSON,
  unsupported media type, bad result descriptors, and handler failures;
- JSON diagnostic output for machine use;
- redaction for secrets;
- source-map validation and real remapping before author-source claims.

No public doc should treat generated-source line numbers as author-source fidelity.

## 12. Examples / Conformance

Mandatory final foundation examples:

- hello route through `sloppyc build` and `sloppy run --artifacts`;
- request context with params/query/headers;
- async handler returning a Promise;
- cancellation/abort path proving request cleanup;
- JSON body parse and JSON response;
- SQLite users API with `:memory:`;
- denied capability example;
- unsupported behavior example that fails clearly.

Each public example must have either:

- executable conformance through the real compiler/runtime path; or
- a clear label saying it is static API-shape documentation and not public alpha proof.

Public alpha docs stay blocked until this set is true or explicitly scoped down with
honest exclusions.
