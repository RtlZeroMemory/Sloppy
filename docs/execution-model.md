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

`sloppyc` now has an EPIC-21 compiler extraction MVP. It can compile one tiny supported
Sloppy source file into deterministic `app.plan.json`, `app.js`, and placeholder
`app.js.map` artifacts. The execution model beyond artifact emission is still staged. The
engine-neutral `SlEngine` C ABI exists with create/destroy/info and handler-call shapes.
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

TASK 09.C adds an inline/fake `SlWorkerPool` skeleton. It proves the native worker
completion contract by running a work callback immediately on the caller thread and posting
the completion to `SlLoop`; the completion callback runs only when the loop drains. It does
not implement real threads, cross-thread posting, blocking DB/filesystem work, libuv, or
V8 Promise settlement.

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

The MVP also supports `Sloppy.createBuilder()` plus `builder.build()`, simple
`app.mapGroup(prefix)` variables, literal grouped `mapGet` routes, `.withName(...)`, and
handlers returning `Results.text(...)` or `Results.json(...)`. It does not implement full
TypeScript checking, Node/npm package resolution, bundling, module extraction,
services/data providers, source-input `sloppy run`, or `app.run`.

EPIC-22 adds the first dev-only run path for those artifacts. EPIC-23 extends it with the
first real response/request boundary. EPIC-24 loads the classic bootstrap runtime asset
before the generated app artifact and validates runtime-owned handler registrations.
`sloppy run --artifacts <dir>` loads `app.plan.json`, reads the compiler-emitted `routes`
metadata through the native Plan parser, verifies referenced artifact hashes before V8 is
created, evaluates bootstrap runtime plus `app.js` in a V8-enabled build, dispatches GET
request paths through the native route matcher, passes a minimal `{ route, query, request }`
context to the handler, converts supported `Results.*` descriptors, writes a deterministic
HTTP/1.1 response, and closes the connection. The deterministic `--once METHOD TARGET`
mode performs the same dispatch without opening a socket.

## Current Handwritten Milestone

The first real milestone is not full TypeScript compilation. It is now covered by a
V8-gated integration test:

```text
handwritten app.js + handwritten app.plan.json -> runtime calls handler by numeric ID
```

That milestone uses a synthetic execution path before HTTP exists. CTest asks the runtime
contract helper to invoke handler ID `1` directly and asserts the returned string result.

## Public API Shape

The user-facing API remains the app-host API:

```ts
const builder = Sloppy.createBuilder();
const app = builder.build();

app.mapGet("/", () => Results.text("Sloppy is alive"));

await app.run();
```

The execution model supports this API without making users think about generated handler
functions, bridge intrinsics, or plan files during normal development.

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

## Artifact Boundary

`sloppyc` emits:

- `app.js`: executable JavaScript bundle;
- `app.js.map`: source map for diagnostics;
- `app.plan.json`: host graph contract.

The MVP source map is a deterministic placeholder with no source mappings. It exists
because Plan v1 requires `sourceMap` fields. MAIN1-02 verifies the source-map artifact hash
when the run path loads artifacts, but source-map fidelity is still deferred.

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

Current EPIC-22/23/24 `sloppy run` flow:

1. accept an artifact directory through `--artifacts <dir>` or positional `<artifact-dir>`;
2. load `<dir>/app.plan.json` through the native Plan parser;
3. validate parsed Plan route/provider/capability metadata where those sections are present;
4. build a native dev route table from Plan GET route patterns, ordered by
   literal-before-parameter precedence and stable source order when equal;
5. read `bundle.path` and `sourceMap.path` and verify their `sha256:` hashes;
6. create a V8 engine, load the configured bootstrap stdlib root, and evaluate
   `internal/runtime-classic.js`;
7. evaluate the artifact `app.js` and validate all plan handler IDs were registered;
8. either dispatch one synthetic `--once METHOD TARGET` request or start a local
   `127.0.0.1:5173` dev server by default;
9. parse request heads, reject unsupported request bodies, route GET paths, call handlers by
   numeric ID with route/query context, convert supported descriptors, write a native HTTP
   response, and close the connection.

Deferred dev-mode work:

- source input handoff to `sloppyc`;
- cache keys and rebuild validation;
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
2. reject non-GET methods;
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
3. reject non-GET dispatch as `405`;
4. return `404` when no GET route matches;
5. materialize route params, query params, and request method/path/rawTarget;
6. call the matched handler through the context-aware runtime-contract helper;
7. convert plain string fallback or supported result descriptors;
8. write a minimal native HTTP response with status line, `Connection: close`,
   `Content-Type`, `Content-Length`, CRLF formatting, and body bytes.

The response writer remains deliberately small and dev-only. Body parsing, streaming,
headers in context, middleware, production hardening, and content negotiation remain
future work.

## Async And Promise Lifecycle

Alpha V8 handlers must return a concrete supported value during the native call: a string
or supported `Results.*` descriptor. Returned Promises and `async` handlers are rejected
with an explicit unsupported diagnostic. Sloppy does not yet keep request scope alive across
JavaScript async work and does not run a JS event loop or timer/microtask integration path.

Async work must not retain request-arena memory beyond request scope. Long-lived work must
own data explicitly or use resource table entries.

Future Promise lifecycle requirements:

- request scope stays alive until the returned promise settles;
- continuations run on the owning JS event-loop thread;
- native async completions post back to that JS owner before JS resumes;
- cleanup runs exactly once;
- rejected promises become diagnostics;
- microtask draining is controlled by the V8 bridge;
- cancellation semantics are specified in `docs/concurrency.md` and implemented later.

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

Future V8 Promise handling should resolve or reject through this model or a documented
evolution of it so request cleanup remains owned by the runtime. Until then, tests must
verify Promise rejection as unsupported rather than `[object Promise]` success.

## Source Map Diagnostic Flow

Runtime exception flow:

1. V8 reports generated JS location;
2. bridge captures exception message, generated source name, generated line/column, and
   stack summary when available;
3. when source maps become useful, runtime maps generated location through `app.js.map`;
4. diagnostic reports original TypeScript file/span;
5. generated location is included as fallback detail;
6. missing or placeholder source maps keep generated locations as the honest fallback.

MAIN1-05 behavior stops after generated source name, 1-based line/column, message, and a
bounded stack note. Current compiler `app.js.map` files are hashed artifacts but carry empty
mappings, so TypeScript source remapping and code frames remain future work. Promise returns
now have a clear unsupported diagnostic, not async stack handling.

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
queryOne, and transactions. The JavaScript stdlib still cannot send descriptors to native
SQLite because runtime intrinsics and JS-visible resource IDs are future work; the
`data.sqlite.open(...)` stdlib entry point reports that bridge gap instead of pretending to
open a database.
EPIC-17 and EPIC-18 add the same native-provider boundary shape for PostgreSQL and SQL
Server. Native tests pass `$1` PostgreSQL SQL or `?` ODBC SQL plus parameters directly to
the providers and verify non-live behavior by default, with live execution gated by
`SLOPPY_POSTGRES_TEST_URL` and `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING`. The JavaScript
stdlib still cannot send descriptors to those native providers; `data.postgres.open(...)`
and `data.sqlserver.open(...)` report the bridge gap honestly.

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
are available. Provider/capability entries remain metadata-only and are not enforcement.

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
