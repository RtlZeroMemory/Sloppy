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

Only placeholder CLIs exist. The execution model is specified but not implemented. The
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
TASK 11.A adds only the bootstrap stdlib file layout. The runtime does not load these
modules yet, and the V8 bridge does not bind intrinsics or resolve ESM imports from this
directory.

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

## Artifact Boundary

`sloppyc` emits:

- `app.js`: executable JavaScript bundle;
- `app.js.map`: source map for diagnostics;
- `app.plan.json`: host graph contract.

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

Planned `sloppy run` flow:

1. discover project inputs;
2. compute build cache key;
3. invoke `sloppyc` pipeline or reuse valid cache;
4. write artifacts under `.sloppy/`;
5. start runtime with those artifacts;
6. surface diagnostics with source spans;
7. later, watch files and restart or hot-reload only where semantics are explicit.

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

Planned handler registration:

1. plan declares expected handler IDs and generated exports;
2. bootstrap stdlib exposes a registration intrinsic;
3. app bundle calls registration during startup;
4. V8 bridge stores function handles by numeric ID;
5. runtime verifies all expected IDs exist;
6. unexpected IDs are diagnosed;
7. duplicate IDs are diagnosed.

User-facing names such as `Users.Get` exist for diagnostics and tooling. Dispatch uses
numeric IDs.

Handler table consistency checks:

- every expected plan handler ID is registered;
- no handler ID is registered twice;
- unexpected handler IDs are rejected or warned according to mode;
- registered handler callable type is verified by the engine bridge;
- user-facing handler name is carried only for diagnostics/tooling.

TASK 11.A reserves `stdlib/sloppy/internal/intrinsics.js` as the future import boundary for
runtime-provided registration and host intrinsics. That file currently exports an empty
frozen placeholder object; no registration intrinsic exists yet.

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

Before HTTP exists, the current synthetic execution flow is:

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

TASK 10.C adds only the native HTTP prelude before that runtime-contract call:

1. parse an in-memory HTTP request head;
2. reject non-GET methods;
3. match the parsed path against manual route bindings;
4. resolve the matched binding to a numeric handler ID;
5. verify that handler ID exists in the parsed plan;
6. call the existing runtime-contract helper.

Route params are not materialized into a JavaScript request context yet. The returned value
is still the existing simple engine result, not an HTTP response.

## Async And Promise Lifecycle

Handlers may return promises. Request scope remains alive until the promise settles. Rejected
promises become diagnostics. The engine bridge controls microtask draining so the runtime
owns cleanup and error boundaries.

Async work must not retain request-arena memory beyond request scope. Long-lived work must
own data explicitly or use resource table entries.

Promise lifecycle requirements:

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
evolution of it so request cleanup remains owned by the runtime.

## Source Map Diagnostic Flow

Runtime exception flow:

1. V8 reports generated JS location;
2. bridge captures exception message, generated source name, generated line/column, and
   stack summary when available;
3. runtime maps generated location through `app.js.map`;
4. diagnostic reports original TypeScript file/span;
5. generated location is included as fallback detail;
6. missing source map is a diagnostic quality failure in dev and a configurable packaging
   concern in production.

Current TASK 07.D behavior stops after step 2 for the smoke API. Source maps, TypeScript
source remapping, code frames, route/handler context, async stacks, and promise rejection
policy remain future work.

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
`SlPlan` storage and validates the minimal handwritten shape. It does not load files,
verify runtime compatibility, verify hashes, load bundles, or perform startup consistency
checks beyond the parser's shape rules.

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
