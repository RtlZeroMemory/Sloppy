# Sloppy Compiler and Execution Model

## Purpose

This document explains how TypeScript application code becomes executable work inside the
Sloppy runtime. It is the contract between `sloppyc`, `app.plan.json`, the C runtime, the V8
bridge, and the JavaScript bootstrap stdlib.

Concurrency, owner-thread rules, native completion queues, and async request lifetime are
defined in `docs/concurrency.md`.

## Scope

This document covers:

- TypeScript to artifact flow;
- dev mode and build mode;
- runtime startup;
- handler registration;
- request execution;
- async/promise lifetime;
- source map diagnostics;
- plan/bundle consistency;
- future build cache behavior;
- first execution milestone tasks.

## Non-Goals

This document does not implement:

- TypeScript compilation;
- V8 loading;
- HTTP parsing;
- app plan loading;
- route dispatch;
- source map parsing.

## Current Phase

Post-ENGINE-16 consolidation note: the supported execution path now has compiler-emitted
Plan/bundle/source-map artifacts, optional V8 artifact execution, bounded HTTP transport
features, current synchronous SQLite bridge behavior, ENGINE-15 source-map/diagnostic
evidence, and ENGINE-16 app/request/resource lifecycle evidence. Engine Roadmap-2 is the
next runtime maturation source: execution model hardening, runtime feature modularity,
provider executor adoption, route-level HTTP policy/observability, runtime events/metrics,
and later torture tests. Public alpha docs, benchmark claims, provider expansion, and
production HTTP remain blocked until their own evidence exists.

HTTP-25.A/B/C update: the libuv localhost transport now supports bounded sequential
HTTP/1.1 keep-alive. HTTP-25.D/E adds bounded chunked request decoding and an internal
chunked response writer. HTTP-25.F adds bounded conformance/stress evidence over those
implemented transport behaviors without adding new protocol features. Eligible successful HTTP/1.1 responses write managed
`Connection: keep-alive`, reset request-owned state after the response write callback, and
return the same connection to idle/read-wait for one next request. `Connection: close`,
HTTP/1.0, disabled keep-alive config, shutdown, unsafe error responses, and max-request
exhaustion write `Connection: close` and close after write. Pipelined bytes are rejected
deterministically; request streaming APIs, public response streaming helpers, SSE,
WebSockets, and file streaming remain deferred.

`sloppyc` now has the ENGINE-02 compiler/Plan pipeline. It can compile a supported
single-file Sloppy app into deterministic `app.plan.json`, `app.js`, and real handler-line
`app.js.map` artifacts. The execution model beyond artifact emission is still staged. The
FRAMEWORK-01.B compiler path resolves application configuration before artifact emission
so provider metadata and redacted config metadata become part of the Plan-visible contract.
Source-input `sloppy run` passes environment and selected CLI overrides into that compiler
handoff; explicit `--artifacts` continues to execute already-emitted artifacts.
The engine-neutral `SlEngine` C ABI exists with create/destroy/info and handler-call shapes.
The noop backend is always available. A V8-enabled build can run the TASK 07.C smoke path:
evaluate a borrowed classic JavaScript source string and call a named global zero-argument
function returning a copied string. TASK 07.D maps basic V8 compile/eval/call exceptions
from that smoke path into `SlDiag`.

TASK 08.A adds the first handwritten artifact execution smoke path. A parsed handwritten
`app.plan.json` can map numeric handler ID `1` to a handwritten `app.js` global named
`__sloppy_handler_1`; the runtime contract helper then invokes that global through the
engine boundary and receives the copied string result `sloppy-ok`. This is still not the
HTTP runtime, compiler output, route dispatch, module loader, request context, public
TypeScript API, or full `Results.*` model.
EPIC-24 keeps that compatibility smoke but moves the generated app path to
runtime-owned handler registration through `__sloppy_register_handler`.

TASK 09.A adds a native `SlLoop` completion queue skeleton for future async runtime work.
It is caller-backed, fixed-capacity, and synchronous: completion callbacks run on the caller
thread during `sl_loop_run_once` or `sl_loop_drain`. It does not integrate with V8
microtasks, promise settlement, HTTP, libuv, OS event loops, worker pools, or request
lifetime.

TASK 09.B adds a native `SlAsync` promise settlement model skeleton over `SlLoop`. It can
manually represent pending, fulfilled, rejected, and cancelled native work and dispatch a
continuation through the loop. It is not JS Promise integration, V8 microtask draining,
async handler execution, HTTP request lifecycle, request-scope retention, worker-pool
completion, or cancellation/deadline/backpressure behavior.

ENGINE-03 adds the first V8 async handler runtime cut. V8-enabled handler calls now drain
V8 microtasks explicitly on the isolate owner thread, settle returned Promises that
complete during that drain, map rejected Promises to engine diagnostics, fail still-pending
Promises as deadline-style handler failures, and expose a native cancellation snapshot to
handler context as `ctx.signal` plus a minimal `ctx.deadline` marker. It does not add a
Node event loop, timers, fetch, filesystem/process APIs, native async provider queues, or
worker-thread scheduling.

TASK 09.C adds an inline/fake `SlWorkerPool` skeleton. It proves the native worker
completion contract by running a work callback immediately on the caller thread and posting
the completion to `SlLoop`; the completion callback runs only when the loop drains. It does
not implement real threads, cross-thread posting, blocking DB/filesystem work, libuv, or
V8 Promise settlement.

ENGINE-12.AB adds the first real async backend boundary. `SlAsyncLoop` is a Slop-owned,
opaque, fixed-capacity completion loop created over caller-owned completion storage. The
deterministic test backend remains available for unit tests, and the primary runtime
backend uses libuv internally under `src/platform/libuv/` for cross-thread wakeup. Libuv
handles and loop types do not escape the backend implementation, and the public/runtime
model is still Sloppy's model rather than Node or libuv compatibility.

ENGINE-12.CD adds the deterministic provider/offload policy skeleton, and ENGINE-23 is the
planned provider execution runtime that turns that policy into production provider
operation descriptors, per-provider-instance executors, serialized SQLite-class blocking
offload, bounded blocking pools, capability-gated admission, cancellation/late-completion
semantics, diagnostics, and stress evidence. Provider execution is not part of ENGINE-13's
HTTP backend implementation and must not block the V8 owner thread.
ENGINE-26.C/D hardens the shared completion boundary without expanding provider or HTTP
runtime behavior. `SlAsyncCompletion` can now carry a caller-owned terminal-state check and
late-completion hook; owner-thread drain skips dispatch when the owner reports terminal and
still runs cleanup plus scope release exactly once. Cancellation reasons also map through
`sl_cancellation_diag_code`, which keeps queued/active cancel, timeout/deadline,
backpressure, and shutdown diagnostics consistent for provider and future runtime
completion owners.

ENGINE-27.A/B adds a Plan-driven runtime feature activation check before runtime
initialization. The registry derives active features from `target.engine`, route metadata,
provider metadata, and explicit `requiredFeatures[]`. It activates only the current
required feature ids, reports V8-disabled builds as unavailable instead of pretending the
lane ran, and fails closed for unknown/unavailable features or unavailable dependencies.
This does not dynamically load feature code or change provider/HTTP behavior.

TASK 10.A adds a pure-C route pattern parser and matcher foundation for later native route
dispatch. It supports only a minimal path-pattern subset and one-pattern matching. It does
not add HTTP parsing, request lifecycle, method matching, route table/trie dispatch,
handler invocation, public TypeScript APIs, llhttp, libuv, V8 integration, or compiler
extraction.
TASK 10.B adds a bounded HTTP dependency/parser skeleton. It uses llhttp to parse one
complete HTTP/1 request head into arena-owned native fields and uses libuv only for a
dependency/link smoke. It does not add sockets, streaming request parsing, body parsing,
response writing, route dispatch, request contexts, handler invocation, public TypeScript
APIs, V8 integration, or compiler extraction.
TASK 10.C adds the first synthetic GET dispatch path from parsed request head to numeric
handler ID. A manual borrowed route binding table maps a parsed `SlRoutePattern` to a
`SlHandlerId`; the helper matches the parsed request path, validates the handler exists in
the parsed plan, and calls the existing runtime-contract helper. It is still in-memory test
dispatch only: no TCP server, sockets, response writer, body parsing, request context,
middleware, public TypeScript API, compiler extraction, or plan routes section exists.
TASK 11.A adds only the bootstrap stdlib file layout. TASK 11.B/11.C adds a tiny public
facade inside that layout: `Results.text/json` descriptors plus in-memory
`Sloppy.create().mapGet(...)` route registration. The runtime still does not load these
modules, the V8 bridge does not bind intrinsics or resolve ESM imports from this
directory, and `app.mapGet` does not produce `app.plan.json`. TASK 11.D adds
`examples/hello/` as a static API-shape example using the source stdlib; it does not change
runtime startup or execution behavior.
TASK 12.A/12.B/12.C/12.D adds a JavaScript-only app-host foundation skeleton in the same
bootstrap stdlib: `Sloppy.createBuilder()`, `builder.build()`, structural `app.freeze()`,
object config, memory logging, and string-token singleton/transient services. Route
handlers invoked through bootstrap route snapshots can receive `{ services, config, log }`
for tests/examples. This still does not change runtime startup, native HTTP dispatch,
compiler extraction, or plan emission behavior.
TASK 13 adds route groups, route metadata storage, `schema`, and the fuller bounded
`Results.*` helper set. TASK 14 adds bootstrap app modules with deterministic dependency
ordering, services/routes phase execution, route/service attribution, and module debug
metadata. These remain local JavaScript bootstrap structures; runtime startup, native HTTP
dispatch, compiler module extraction, and real plan emission are still future work.
TASK 13.A/13.B/13.C/13.D keeps that boundary and adds JavaScript-only route groups,
expanded result descriptors, a `schema` validation skeleton, and an ergonomics example.
Route schemas and group metadata are stored only in bootstrap route snapshots. They are not
emitted into `app.plan.json`, consumed by native HTTP dispatch, or converted into automatic
request validation.

EPIC-21 adds compiler extraction for the tiny public API bridge:

```js
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.mapGet("/", () => Results.text("Hello from Sloppy"));

export default app;
```

ENGINE-02 supports `Sloppy.createBuilder()` plus `builder.build()`, simple
`app.mapGroup(prefix)` variables, literal grouped route calls for
GET/POST/PUT/PATCH/DELETE, `.withName(...)`, direct async handler metadata, supported
`Results.*` descriptors, and minimal SQLite provider/capability Plan metadata. It does not
implement full TypeScript checking, Node/npm package resolution, bundling, module
extraction, source-input `sloppy run`, `app.run`, or native SQLite bridge execution from
compiled handlers.

EPIC-22 adds the first dev-only run path for those artifacts. EPIC-23 extends it with the
first real response/request boundary. EPIC-24 loads the classic bootstrap runtime asset
before the generated app artifact and validates runtime-owned handler registrations.
`sloppy run --artifacts <dir>` loads `app.plan.json`, reads the compiler-emitted `routes`
metadata through the native Plan parser, verifies referenced artifact hashes before V8 is
created, runs native app-host startup validation over the parsed app graph, evaluates
bootstrap runtime plus `app.js` in a V8-enabled build, dispatches
GET/POST/PUT/PATCH/DELETE request paths through the native route matcher, passes a minimal
`{ route, query, request, signal, deadline }` context with request headers and bounded
JSON/text body access to the handler, converts supported `Results.*` descriptors, writes a
deterministic HTTP/1.1 response, and closes the connection. The deterministic
`--once METHOD TARGET` mode
performs the same dispatch without opening a socket.

ENGINE-13.A/B/C adds the first core HTTP backend state model under that dev path:
backend/listener init-start-stop-dispose state, accepted/open/reading/dispatching/writing/
closing/closed/error connection states, created/reading/dispatching/writing/completed/
cancelled/timed-out/failed/closed request states, parser limit policy, timeout hooks, and
bounded admission. ENGINE-13.D/E adds the bounded body-reader and shutdown terminal paths:
body chunks are copied into request-arena storage only up to configured limits, supported
body media remain explicit, cancellation/timeout/shutdown stop body reads before dispatch,
new request work is rejected once shutdown begins, and active request work can drain or be
cancelled with cleanup-once release. It does not change public handler semantics, does not
make the dev server production-ready, and does not add TLS, HTTP/2/3, WebSockets, static
files, compression, V8/provider/compiler work, or production benchmark evidence.
ENGINE-24.A/B adds the first HTTP transport listener foundation below the current request
execution pipeline. A `SlHttpTransportServer` can initialize, bind/listen on localhost,
accept TCP sockets into bounded accepted connections, reject overflow by closing the
accepted socket, stop, and dispose.
ENGINE-24.C advances that transport state through the read/request-accumulation boundary.
Accepted connections start reading, append TCP chunks into bounded per-connection storage,
parse exactly one Content-Length request through existing ENGINE-13 parser/body rules, and
then park the parsed request in a `REQUEST_READY` transport state. ENGINE-24.D consumes
that state when dispatch is configured: the backend request moves to dispatching, a narrow
internal callback returns an `SlHttpResponse`, the existing response writer serializes
bytes into connection-owned storage, libuv writes those bytes, the write callback
completes the request lifecycle, and the connection closes. Without dispatch, the parsed
request is closed immediately to release admission. Extra bytes after the first complete
request are unsupported pipelining.
ENGINE-24.E completes the current transport terminal semantics for cancellation, timeout,
and shutdown. Client disconnect during read cancels/closes without dispatch. Header-read,
body-read, total-request, and write timers transition the connection/request to terminal
state; header/body/request timeouts write `408 Request Timeout` when the socket is still
writable and otherwise close. Server stop is immediate-cancel/drain-lite: it stops
accepting, rejects newly accepted work, cancels active request lifecycles through the
backend shutdown token path when present, closes active transport connections, and drains
close callbacks. This is not production graceful drain, localhost conformance, V8 transport
execution, keep-alive, streaming, or benchmark evidence.
ENGINE-24.G keeps that state machine close-after-response for the MVP. The current path
does not loop from write completion back to read, does not accept a second sequential
request on the same TCP connection, and treats extra bytes after the first complete request
as unsupported pipelining. A future HTTP/1.1 upgrade must add an explicit post-write
read-resume transition, per-request state and arena reset, idle timeout, max requests per
connection, shutdown drain behavior, and diagnostics before keep-alive is enabled.

ENGINE-24 is the next HTTP execution layer. It owns the real TCP/libuv transport server
that turns client socket bytes into ENGINE-13 request lifecycle work and writes serialized
responses back to the socket. Its MVP is HTTP/1.1 over TCP, Content-Length only,
close-after-response, bounded read/head/body/response storage, bounded max connections and
active requests, deterministic malformed/overload/timeout behavior, disconnect
cancellation, graceful listener stop and active-connection drain/cancel, and localhost
socket or curl smoke. It does not add keep-alive, pipelining, TLS, HTTP/2/3, WebSockets,
streaming bodies, static files, compression, reverse proxy behavior, or benchmark claims.

## Current Handwritten Milestone

The first real milestone is not full TypeScript compilation. It is now covered by a
V8-gated integration test:

```text
handwritten app.js + handwritten app.plan.json -> runtime calls handler by numeric ID
```

That older milestone used a synthetic execution path before HTTP dispatch existed. Current
V8-gated HTTP integration tests exercise handler ID dispatch through the native route table
and request context when the SDK is available.

## Public API Shape

ENGINE-01 locks the framework contract in `docs/project/engine-framework-contract.md`.
Implementation layers should use that document as the source of truth for the final JS app
API, Results API, request context, async/microtask policy, HTTP support matrix, SQLite API,
capability expectations, and deferred behavior.

The user-facing API remains the app-host API:

```ts
const app = Sloppy.create();

app.mapGet("/", () => Results.text("Sloppy is alive"));

export default app;
```

The foundation execution workflow is explicit artifacts:

```powershell
sloppyc build app.js --out .sloppy
sloppy run --artifacts .sloppy --host 127.0.0.1 --port 5173
```

ENGINE-17.E proves that workflow for a small SQLite users API when the V8 SDK lane is
enabled: source app -> `sloppyc build` -> `app.plan.json`/`app.js` -> `sloppy run
--artifacts` -> localhost TCP HTTP -> request parsing/body policy -> route dispatch -> V8
handler -> capability-gated SQLite bridge -> JSON response -> TCP write. The current
SQLite bridge in that path is synchronous and single-thread-owned; async/offload provider
conversion remains deferred.

`sloppy run app.js` now exists as a compiler/CLI shortcut: it invokes `sloppyc build`,
validates generated artifacts, then runs the same path as `sloppy run --artifacts`.
`sloppy run` without a source reads the minimal current-directory `sloppy.json` project-run
config. `app.run` and `app.listen` remain deferred. The execution model should still
support the app-host API without making users think about generated handler functions,
bridge intrinsics, or plan files once later app-host work lands.

## Core Pipeline

```text
TypeScript source
  -> sloppyc parses/resolves/transforms/extracts metadata
  -> sloppyc emits app.js + app.js.map + app.plan.json
  -> sloppy runtime loads app.plan.json
  -> runtime validates plan compatibility
  -> runtime builds native host graph
  -> runtime creates an opaque SlEngine through include/sloppy/engine.h
  -> V8 bridge loads app.js
  -> JS startup registers handler functions
  -> runtime dispatches requests/jobs to handlers by numeric handler ID
```

V8 executes JavaScript, not TypeScript. TypeScript is never evaluated directly. The runtime
does not type-check. `sloppy check` later uses the official TypeScript checker through
`tsgo` or `tsc`.

Current EPIC-21/22/23/24 behavior covers the first executable compiler-to-runtime path for
one source file when V8 is enabled. The generated `app.js` is still a classic script, but it
expects bootstrap runtime state from `globalThis.__sloppy_runtime`, assigns legacy
`globalThis.__sloppy_handler_<id>` exports for the no-context contract, and calls
`__sloppy_register_handler(N, handler)` for the registered-handler table. The runtime does
not resolve Node packages, npm packages, arbitrary bare specifiers, dynamic imports, or user
source module graphs.

Provider execution in this pipeline must enter through ENGINE-23 provider executors once
work can block or outlive the caller stack. Direct V8 bridge calls into synchronous native
providers are acceptable only where docs identify the path as current limited behavior and
do not claim scalable provider execution.

The app-host lifecycle has native debug/test snapshots for the current helper layer.
`SlAppLifecycleSnapshot`, `SlAppRequestScopeSnapshot`, and `SlResourceTableSnapshot`
observe active app/request scopes, cleanup counts, late completions, and live resources by
kind without exposing native pointers. The snapshots are lifecycle evidence hooks, not a
production process manager, DI container, plugin runtime, or public monitoring API.

## Artifact Boundary

`sloppyc` emits:

- `app.js`: executable JavaScript bundle;
- `app.js.map`: source map for diagnostics;
- `app.plan.json`: host graph contract.

The ENGINE-02 source map is a deterministic Source Map v3 artifact with `sources`,
`sourcesContent`, and mappings from generated handler assignment lines back to the original
handler source lines. MAIN1-02 verifies the source-map artifact hash when the run path
loads artifacts. Runtime exception remapping through that map remains deferred to
ENGINE-08, so generated V8 locations are still the execution-time fallback.

The plan is authority for the native host graph. The bundle provides executable handler
functions. Both must agree at startup.

Handwritten milestone artifacts should be deliberately tiny:

```json
{
  "schemaVersion": 1,
  "compilerVersion": "sloppyc-placeholder",
  "runtimeMinimumVersion": "0.1.0",
  "stdlibVersion": "0.1.0",
  "target": {
    "platform": "windows-x64",
    "engine": "v8"
  },
  "bundle": {
    "path": ".sloppy/app.js",
    "id": "handwritten-smoke",
    "hash": "sha256-test-placeholder"
  },
  "sourceMap": {
    "path": ".sloppy/app.js.map",
    "id": "handwritten-smoke-map",
    "hash": "sha256-test-placeholder"
  },
  "handlers": [
    {
      "id": 1,
      "exportName": "__sloppy_handler_1",
      "displayName": "Smoke.Hello"
    }
  ]
}
```

Conceptual `app.js`, not final compiler output:

```js
export function __sloppy_handler_1(ctx) {
  return SloppyInternal.results.text("hello");
}

SloppyInternal.registerHandler(1, __sloppy_handler_1);
```

## File And Module Layout

Likely implementation areas:

```text
compiler/src/emit/
compiler/src/plan/
src/core/plan*
src/core/runtime*
src/engine/v8/
tests/integration/execution/
tests/golden/plan/
```

Exact filenames are deferred until implementation stories begin.

## Dev Mode Flow

Current `sloppy run` flow:

1. accept an artifact directory through `--artifacts <dir>` or positional `<artifact-dir>`,
   or accept source input through `<source.js>` / current-directory `sloppy.json`;
2. for source input, invoke `sloppyc build` and write artifacts to
   `.sloppy/cache/dev/source-input` or the configured `outDir`;
3. load `<dir>/app.plan.json` through the native Plan parser;
4. validate parsed Plan route/provider/capability metadata where those sections are present;
5. build a native dev route table from Plan GET/POST/PUT/PATCH/DELETE route patterns, ordered by
   literal-before-parameter precedence and stable source order when equal;
6. read `bundle.path` and `sourceMap.path` and verify their `sha256:` hashes;
7. create a V8 engine, load the configured bootstrap stdlib root, and evaluate
   `internal/runtime-classic.js`;
8. evaluate the artifact `app.js` and validate all plan handler IDs were registered;
9. either dispatch one synthetic `--once METHOD TARGET` request or start a local
   `127.0.0.1:5173` dev server by default;
10. parse bounded request messages, reject unsupported body framing/content types and
   malformed JSON before handler entry, route GET/POST/PUT/PATCH/DELETE paths, call handlers
   by numeric ID with route/query/header/body context, convert supported descriptors, write a
   native HTTP response, and close the connection.

Deferred dev-mode work:

- cache reuse and stale-cache validation beyond the current rebuild-always handoff;
- source spans for startup diagnostics;
- file watching and restart/hot reload.

Dev mode must use the same artifact architecture as production. It may add caching and
watching, but it must not invent a runtime-only app discovery model.

## Build Mode Flow

Planned `sloppy build` flow:

1. validate project config;
2. run transforms and plan extraction;
3. run static plan validation;
4. emit `app.js`, `app.js.map`, `app.plan.json`;
5. emit build metadata;
6. fail on diagnostics above configured severity.

## Runtime Startup Flow

Target startup flow:

1. initialize runtime;
2. initialize platform abstraction;
3. initialize diagnostics and allocators;
4. load plan;
5. validate plan version/features;
6. validate target platform and engine;
7. build native host graph;
8. initialize V8;
9. install intrinsics;
10. load bootstrap stdlib;
11. load `app.js`;
12. register handlers;
13. verify handler table;
14. enter event loop.

## Handler Registration Flow

Current handler registration:

1. plan declares expected handler IDs and generated exports;
2. V8 runtime context exposes only `__sloppy_register_handler(id, handler)`;
3. generated app bundle calls registration during startup;
4. V8 bridge stores function handles by numeric ID;
5. runtime verifies all expected IDs exist;
6. duplicate IDs, nonnumeric IDs, and non-callable handlers are diagnosed.

User-facing names such as `Users.Get` exist for diagnostics and tooling. Dispatch uses
numeric IDs.

Handler table consistency checks:

- every expected plan handler ID is registered;
- no handler ID is registered twice;
- registered handler callable type is verified by the engine bridge;
- user-facing handler name is carried only for diagnostics/tooling.

`stdlib/sloppy/internal/intrinsics.js` remains an ESM stdlib placeholder for the later app
host. EPIC-24 does not expose a broad `__sloppy` host object and does not surface native
pointers to JavaScript; the V8-only intrinsic is installed directly in the runtime context
for generated artifacts.

## Request Execution Flow

Target request flow:

1. native protocol parse;
2. native route match;
3. create request scope;
4. lazily materialize JS request context only when needed;
5. call handler by numeric ID;
6. await promise if returned;
7. convert `Results.*` or compatible value;
8. write native response;
9. close request resources;
10. release request arena;
11. report debug leaks.

Current ENGINE-13.A/B/C backend foundation makes the native prelude explicit:

1. backend starts over a platform listener boundary;
2. a connection is admitted or rejected by bounded connection capacity;
3. a request lifecycle is admitted or rejected by bounded active-request capacity;
4. request bytes are parsed with target/header/body limits into the request arena;
5. optional backend body-reader work validates the supported media policy and copies body
   chunks into request-owned arena storage only up to the configured body limit;
6. timeout/deadline/cancellation/shutdown hooks can cancel the request token before body
   read, during body read, before dispatch, during dispatch, or during response writing;
7. dispatch and response-writing states are recorded;
8. complete/fail/cancel/timeout/shutdown/close releases the request admission slot exactly
   once;
9. connection close/fail releases the connection slot exactly once.

ENGINE-24.A/B wires only the first two platform-facing pieces of that prelude: listener
bind/listen and accepted connection admission. ENGINE-24.C wires the read/accumulation
piece and parks the request. ENGINE-24.D wires dispatch/write/close-after-response through
a narrow internal dispatch callback and the existing response writer. ENGINE-24.E wires
transport disconnect, timeout, and shutdown terminal paths. V8 transport conformance,
provider proof, streaming, and production graceful drain remain deferred.

HTTP-25.A/B/C upgrades the CLI socket loop from close-after-response to bounded
sequential HTTP/1.1 keep-alive. A connection may return to idle/read-wait only after the
previous response write completes, request-owned state is reset, shutdown has not begun,
the configured max-request count has not been reached, and the request/response lifecycle
is safe to reuse. The loop still forces close for HTTP/1.0, explicit `Connection: close`,
disabled keep-alive config, shutdown, unsafe error/body/parse failures, unsupported
pipelining, and max-request exhaustion. HTTP-25.D/E adds bounded chunked request decoding
before dispatch and an internal/native chunked response writer that sequences transport
writes through the write callback and writes the final zero chunk before keep-alive reset.
Public request streaming, public JS response streaming helpers, SSE/WebSockets/file
streaming, keep-alive stress/conformance, and production graceful drain remain future
HTTP-25 follow-ups.

ENGINE-24 replaces or wraps that CLI-local socket loop with a reusable transport runtime
boundary. `src/main.c` should become a caller of the transport server instead of the owner
of HTTP transport policy once the server exists.

The current shutdown policy is bounded and honest: shutdown stops acceptance and rejects
new request work, then the backend reaches stopped when active connection/request counters
are released. Active requests may finish normally, fail, time out, close, or be cancelled
through the request shutdown hook. The transport now closes active TCP connections during
stop, but this remains immediate-cancel/drain-lite rather than production graceful drain;
localhost smoke/conformance remains #417.

ENGINE-01 target handler context contains `route`, `query`, `request`, `signal`,
`deadline`, and future request-owned `resources`. The foundation request lifecycle must
retain that scope until a synchronous handler returns or an async handler Promise settles,
is rejected, or is cancelled.

MAIN1-03 implements the minimal native request-scope boundary for the current request
dispatch path. The scope begins before the handler boundary and closes after success or
failure. Cleanup callbacks use `SlScope` LIFO order. The scope owns cleanup registrations
only; independently closable resources still belong in `SlResourceTable` entries and must
be closed by registered cleanup callbacks when request-scoped.

Before `sloppy run`, the synthetic execution flow was:

1. load handwritten plan;
2. load handwritten bundle;
3. evaluate the bundle as a classic script in the V8 bridge;
4. find the plan handler by numeric ID;
5. call the plan handler export/global name through the engine boundary;
6. assert copied string result or diagnostic;
7. cleanup engine resources.

The C-side handler-call ABI for this future path is already shaped as
`sl_engine_call_handler(SlEngine*, SlEngineHandlerCall*, SlEngineResult*, SlDiag*)`.
TASK 07.B deliberately implements that entry point as unsupported for the noop engine; it
does not load modules, invoke exports, convert JavaScript values, or run `app.js`.

TASK 08.A deliberately keeps the handwritten path small. `sl_engine_eval_source` evaluates
classic script text, `sl_runtime_contract_call_handler` maps a plan handler ID to its
`exportName`, and `sl_engine_call_function0` calls that global function. This is not ESM
loading, handler registration intrinsics, request/job context construction, or compiler
output loading.

TASK 10.C adds only the native HTTP prelude before the first runtime-contract call:

1. parse an in-memory HTTP request head;
2. reject methods outside the later runtime-supported set;
3. match the parsed path against manual route bindings;
4. resolve the matched binding to a numeric handler ID;
5. verify that handler ID exists in the parsed plan;
6. call the existing runtime-contract helper.

EPIC-23 extends this path for `sloppy run` only: route params, query params, and
request method/path/rawTarget are materialized into a minimal JavaScript context, and
supported handler result descriptors become native HTTP responses.

EPIC-22 and EPIC-23 wrap that foundation in the dev-only CLI path:

1. parse artifact route metadata into route bindings;
2. parse an HTTP/1 request head from `--once` or a libuv connection;
3. apply GET/POST/PUT/PATCH/DELETE method dispatch from Plan route metadata;
4. return `404` when no route matches and `405` when a path matches with another method;
5. reject unsupported body framing, unsupported content types, oversized bodies, and
   malformed JSON before handler entry;
6. materialize route params, query params, request method/path/rawTarget, request headers,
   text body access, JSON body access, cancellation signal, and deadline marker;
7. call the matched handler through the context-aware runtime-contract helper;
8. convert plain string fallback or supported result descriptors;
9. write a minimal native HTTP response with status line, `Connection: close`,
   `Content-Type`, `Content-Length`, CRLF formatting, and body bytes.

ENGINE-07 adds an explicit native app lifecycle boundary around this dev-only run path.
`sloppy run` starts an app lifecycle before Plan-backed startup validation and registers
engine destruction as an app shutdown cleanup after V8 creation succeeds. Command exit,
startup aborts after engine creation, and dev-server loop exit all run app shutdown through
that lifecycle. Shutdown is idempotent; app-scoped cleanup callbacks use the same
`SlScope` LIFO contract as request scopes. ENGINE-16.A/B makes that lifecycle stateful:
created -> starting -> running -> stopping/draining -> stopped, with failed as the
terminal startup-failure state. Request scopes opened through the app lifecycle carry
app/request IDs, increment the active request count, and close before app-scope resources
are released. Beginning shutdown rejects new request scopes; graceful finish waits for the
active count to reach zero, while forced shutdown closes app-scope cleanups exactly once
for the current dev runtime policy.
ENGINE-16.C adds a shared request terminal-outcome helper over that scope model. Success,
sync failure, V8 exception, Promise rejection, validation/body parse failure, timeout,
cancel, client disconnect, response write failure, provider failure, provider pre-start
cancel, shutdown, and backpressure can all mark the request terminal before cleanup runs.
Late completions after a terminal request fail deterministically with
`SL_STATUS_STALE_RESOURCE`/`SLOPPY_E_APP_LIFECYCLE` and must not touch closed request or app
state.

The response writer remains deliberately small and dev-only. Streaming request/response
bodies, middleware, production hardening, multipart upload, cookies/sessions, and content
negotiation remain future work.

## Async And Promise Lifecycle

V8 handlers may return a concrete supported value during the native call, or a Promise for
a supported value that settles during the explicit owner-thread microtask drain. Fulfilled
Promises are converted exactly like synchronous values. Rejected Promises produce
deterministic engine diagnostics. Promises still pending after the bounded drain fail as a
deadline-style handler failure rather than being serialized or reported as success.

Async work must not retain request-arena memory beyond request scope. Long-lived work must
own data explicitly or use resource table entries.

Current Promise lifecycle requirements:

- request scope stays alive only until the owner-thread microtask drain completes the
  bounded ENGINE-03 fulfilled/rejected/pending-timeout outcome handling;
- continuations run on the owning JS event-loop thread;
- cleanup runs exactly once;
- terminal request outcomes are recorded before cleanup so late native completions can
  observe closed scope state without re-closing resources;
- rejected promises become diagnostics;
- async diagnostics are ordinary `SlDiag` values and can be rendered through the stable JSON
  renderer, but a CLI-wide async diagnostic JSON mode remains deferred;
- microtask draining is controlled by the V8 bridge;
- cancellation semantics are specified in `docs/concurrency.md` and required with the first
  real async/HTTP implementation.

Native async completions now post through `SlAsyncLoop` and resume JavaScript only through
the V8 owner-thread continuation scheduler under `src/engine/v8/`. Worker/provider/native
threads may post completions, but only the owning V8 thread drains and settles the Promise.
`include/sloppy/execution_domain.h` is the fixed ENGINE-26.A/B source for domain names and
the yes/no policy used by tests: only `v8-owner-thread` may enter V8; provider workers,
blocking offload, libuv callbacks, HTTP callbacks, and generic async completions must copy
or retain cross-thread data and dispatch any JavaScript continuation back to the owner
thread. This is a policy helper, not runtime feature modularity.
ENGINE-26.E/F adds provider execution-mode policy helpers and bounded race-oriented
evidence. Only `INLINE_FAST` is classified as owner-thread inline work; serialized and
pool-backed blocking provider modes require offload workers. The async backend race test
also proves a retained completion that becomes terminal after enqueue but before owner
drain is cleanup-only. This does not convert the synchronous SQLite V8 bridge or add the
ENGINE-30 torture harness.
ENGINE-12.AB does not add public timers, fetch, fs, process, Node APIs, production provider
offload, or production scalability evidence. ENGINE-12.CD adds native cancellation/
deadline, shutdown, backpressure, and provider-executor policy for a deterministic
provider-like source. It defines operation kinds, provider execution modes,
per-provider-instance bounded admission, copied operation inputs, cleanup-once terminal
completions, and immediate-cancel shutdown. It does not convert SQLite to async offload,
start production provider workers, or add production scalability evidence beyond unit-test
proof of the shape. ENGINE-23 owns the production provider execution runtime.

ENGINE-12 (#306, tasks #307-#310) owns the full generic scalable async runtime target.
ENGINE-23 owns provider execution/offload after that generic substrate. ENGINE-12 work
should begin only when at least one real external async source needs to cross the native
runtime boundary, such as HTTP disconnect/shutdown cancellation, timer/deadline wakeups,
provider offload policy, or worker-pool work. ENGINE-23 is required before public docs,
alpha readiness, benchmark methodology, or product language claim scalable provider
execution. ENGINE-12 remains required before claims about scalable async behavior,
production-ready async HTTP lifecycle, or performance for many pending requests.

ENGINE-01 makes that future lifecycle a foundation requirement rather than an optional
enhancement. Async handlers returning Promises must be supported before Sloppy claims the
framework HTTP foundation is complete. V8 microtasks must drain at documented app-load,
handler-call, and native-completion boundaries. Every request must carry a native
cancellation token and JS `ctx.signal`; deadlines, shutdown, queue overflow, and future
client disconnect behavior cancel through the same path. Native operations that are not yet
interruptible must still check cancellation before work starts and before results are
converted, and docs must not claim mid-call interruption until it exists.

Current native skeleton:

- `SlAsync` starts pending and settles exactly once;
- settlement posts a completion to `SlLoop` rather than running continuations inline;
- fulfillment carries borrowed payload/user pointers;
- rejection/cancellation carry a non-OK `SlStatus` and optional borrowed `SlDiag`;
- failed loop posting leaves the async object pending.
- `SlWorkerPool` inline mode runs native work immediately but posts completion through
  `SlLoop`;
- worker result ownership transfers to the work completion callback when that completion
  dispatches;
- failed worker completion posting destroys an owned result through the submitted destroy
  callback when one is available.
- discarded worker completions require an explicit `sl_worker_pool_reset_inline` cleanup
  after the owning `SlLoop` is reset.

Current V8 Promise handling resolves or rejects at documented owner-thread boundaries:
ENGINE-03 covers bounded microtask checkpoints, and ENGINE-12.AB covers native completion
continuations posted through `SlAsyncLoop`. Request/app scope retention across queued work
uses explicit native retain/release hooks. ENGINE-12.CD provider operations must copy or
own queued inputs and complete through `SlAsyncCompletion`; cancellation, timeout,
overflow, shutdown, and provider failure are distinct terminal outcomes. Tests must
continue to reject fake success, unresolved Promise success, wrong-thread V8 entry,
unbounded queues, and provider work that bypasses admission.

## Source Map Diagnostic Flow

Runtime exception flow:

1. V8 reports generated JS location;
2. bridge captures exception message, generated source name, generated line/column, and
   stack summary when available;
3. when source maps become useful, runtime maps generated location through `app.js.map`;
4. diagnostic reports original TypeScript file/span;
5. generated location is included as fallback detail;
6. missing, placeholder, or not-yet-consumed source maps keep generated locations as the
   honest fallback.

MAIN1-05 behavior stops after generated source name, 1-based line/column, message, and a
bounded stack note. ENGINE-02 compiler `app.js.map` files are hashed artifacts with real
handler-line mappings, but TypeScript source remapping, map consumption by the V8
diagnostic path, and code frames remain future work. ENGINE-07 keeps lifecycle diagnostics
honest by using generated locations when V8 reports them and by not claiming author-source
remapping. ENGINE-03 adds deterministic Promise rejection and pending-Promise diagnostics
for the bounded microtask path; async stack/source-remapping across native completions
remains future ENGINE-12/ENGINE-08 work.

Source map task boundaries:

- first execution milestone may use source map placeholders;
- source-mapped exception diagnostics begin when compiler emits source maps;
- missing map behavior must be tested separately from handler exception behavior.

## Database Query Flow

Tagged templates parameterize by default:

```ts
await db.query`
  select id, name
  from users
  where id = ${id}
`;
```

The JS wrapper sends:

```text
segments: ["select id, name from users where id = ", ""]
params: [id]
```

The provider lowers placeholders and binds values.

Current EPIC-15 bootstrap behavior implements this only inside the source-controlled ESM
stdlib. `sql`, `data.lowerQueryTemplate(...)`, and fake provider methods produce frozen
query descriptors with SQL text and parameters separated. No compiler transform parses
template literals, no native provider receives queries, no database connection is opened,
and no SQL is executed.

EPIC-16 adds native C SQLite execution for the provider boundary. Native tests pass lowered
`?` SQL text and parameter arrays directly to the SQLite provider and verify exec, query,
queryOne, and transactions. MAIN1-08 adds the first V8-gated JS-to-native bridge for
SQLite. ENGINE-05 wires that bridge to Plan provider metadata and the database capability
hook: `data.sqlite("main")` can resolve `data.main`, create a generation-checked resource
handle, and call native `exec`, `query`, and `queryOne` when the V8 runtime installs
`__sloppy.data.sqlite` and the app host passes hook metadata into the engine.
Bootstrap-only and non-V8 contexts still report the bridge gap instead of pretending to
open a database.

The V8 engine layering is provider-neutral and framework-bridge code is split by feature.
`engine_v8.cc` owns engine lifecycle, context setup, handler registration, and Promise
orchestration. HTTP request-context and `Results.*` conversion live in
`src/engine/v8/http_bridge.cc`. `src/engine/v8/intrinsics.cc` aggregates provider
registration, and `src/engine/v8/intrinsics_sqlite.cc` owns SQLite argument/result
conversion and native provider calls. Future framework/provider bridges must follow that
module split and must not expand `engine_v8.cc` with feature-specific conversion logic.

EPIC-17 and EPIC-18 add the same native-provider boundary shape for PostgreSQL and SQL
Server. Native tests pass `$1` PostgreSQL SQL or `?` ODBC SQL plus parameters directly to
the providers and verify non-live behavior by default, with live execution gated by
`SLOPPY_POSTGRES_TEST_URL` and `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING`. The JavaScript
stdlib still cannot send descriptors to those native providers; `data.postgres.open(...)`
and `data.sqlserver.open(...)` report the bridge gap honestly until their own provider
intrinsic modules exist.

## Example Input

```ts
app.mapGet("/users/{id:int}", async ({ route, services }) => {
  const db = services.get("data.main");

  const user = await db.queryOne`
    select id, name
    from users
    where id = ${route.id}
  `;

  return user ? Results.ok(user) : Results.notFound();
}).withName("Users.Get");
```

Conceptual emitted handler shape, not final compiler output:

```ts
export async function __sloppy_handler_1(ctx) {
  const db = ctx.services.get("data.main");
  const user = await db.queryOne(["select id, name from users where id = ", ""], ctx.route.id);
  return user ? Results.ok(user) : Results.notFound();
}
```

## Plan/Bundle Consistency Checks

Startup must validate:

- plan schema version;
- runtime minimum version;
- target platform;
- target engine;
- compiler version;
- bootstrap stdlib version;
- bundle ID/hash;
- source map presence/hash;
- handler IDs;
- handler exports;
- module ordering;
- declared features.

TASK 06.A implements the borrowed native struct shape and small handler/version helpers for
this contract. TASK 06.B parses caller-provided Plan v1 JSON bytes into arena-owned
`SlPlan` storage and validates the minimal handwritten shape. MAIN1-02 adds native route,
data provider, and capability metadata validation when those sections are present, and the
supported artifact run path verifies runtime compatibility and artifact hashes where bytes
are available. ENGINE-02 makes the compiler emit minimal SQLite provider/capability
metadata, but those entries remain metadata-only until ENGINE-05/06 provider and
capability enforcement work consumes them.

Mismatch fails before serving work.

## Future Build Cache Behavior

Build cache keys should include:

- source hashes;
- `sloppyc` version;
- Oxc version;
- relevant TypeScript config options;
- target platform;
- target engine;
- bootstrap stdlib version;
- feature flags.

Cache hits must not skip plan/bundle consistency validation.

## Error And Diagnostic Behavior

Execution diagnostics should cover:

- missing plan;
- incompatible plan version;
- bundle hash mismatch;
- missing source map;
- missing handler ID;
- duplicate handler ID;
- handler throws;
- handler promise rejects;
- result conversion failure.

Diagnostics should prefer TypeScript source spans through source maps.

## Testing Requirements

Execution tests should include:

- startup validation success;
- missing plan field;
- missing handler;
- duplicate handler;
- duplicate route or route name;
- duplicate represented service token;
- request scope cleanup on handler success and failure;
- thrown JS exception;
- rejected promise;
- simple result conversion;
- source map fallback behavior.

## Quality Gates

- CTest integration fixtures;
- diagnostics snapshots once diagnostics exist;
- V8 bridge tests only under engine bridge phase;
- no compiler extraction dependency for the first execution milestone;
- warnings-as-errors clean.

## Implementation Tasks

- Define artifact directory conventions under `.sloppy/`.
- Define minimal plan v1 schema for handwritten artifacts.
- Add runtime CLI option for artifact directory later.
- Add plan loader skeleton.
- Add V8 bridge smoke loading later.
- Add handler table registration intrinsic later.
- Add integration fixture with handwritten `app.js` and `app.plan.json`.
- Add diagnostic mapping placeholder for source maps.
- Add synthetic non-HTTP invocation entry point for tests.
- Add missing, duplicate, and unexpected handler consistency checks.
- Add promise settlement cleanup tests once async bridge exists.
- Add source map fallback diagnostics after source map parser exists.

## Milestone: Handwritten Artifact Execution

Scope:

- handwritten `app.js`;
- handwritten `app.plan.json`;
- optional placeholder `app.js.map`;
- runtime loads plan;
- V8 bridge loads JS;
- handler ID maps to exported/registered function;
- runtime invokes handler ID `1`;
- handler returns simple `Results.text`-like descriptor;
- no HTTP;
- no TypeScript compiler extraction.

Test shape:

```text
tests/integration/execution/handwritten_smoke/
  app.plan.json
  app.js
```

CTest executes the runtime contract helper in a synthetic mode and asserts the returned
result text.

## Acceptance Criteria For First Execution Milestone

The first execution milestone is accepted when:

- a handwritten `app.plan.json` declares one handler ID;
- a handwritten `app.js` defines global function `__sloppy_handler_1`;
- generated compiler artifacts register handler ID `1` through
  `__sloppy_register_handler`;
- runtime validates enough plan/bundle consistency to catch missing plan handler IDs,
  duplicate plan handler IDs, missing JS functions, and thrown handlers;
- runtime invokes the handler by numeric ID;
- runtime converts a simple string result;
- CTest integration covers the flow;
- missing handler, duplicate handler, missing JS function, and thrown handler diagnostics
  are tested;
- no TypeScript compiler extraction is required for the milestone.
- no V8 types leak outside `src/engine/v8/`;
- no OS APIs appear outside `src/platform/*`.

## Open Questions

- Exact JS module format for `app.js`.
- Whether final handler registration is explicit exports, bootstrap calls, or both.
- Whether source map parsing lives in runtime C or a helper library.
- Exact dev watch/restart behavior.
## ENGINE-14 Module Bootstrap Decision

Framework MVP module loading is source-level ESM syntax with compiler-owned graph
resolution and classic artifact execution. `sloppyc` resolves supported relative modules
and Sloppy stdlib/provider imports, then emits the existing `app.js` artifact that the V8
bootstrap runtime evaluates as a classic script. Full V8 native ESM loading, dynamic
imports, Node/npm resolution, and package-manager behavior are not part of this execution
model.

Bootstrap assets are loaded from a deterministic stdlib root. `sloppy run` validates the
bootstrap manifest version and required classic runtime asset before evaluating user
artifacts, and missing or incompatible assets fail closed instead of silently falling back.
