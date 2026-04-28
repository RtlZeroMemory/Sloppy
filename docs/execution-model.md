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
from that smoke path into `SlDiag`. Handler calls by numeric Sloppy Plan ID still return
unsupported until handler registration and plan mapping land.

## Future Phase

The first real milestone is not full TypeScript compilation. It is:

```text
handwritten app.js + handwritten app.plan.json -> runtime calls handler by numeric ID
```

That milestone should use a synthetic execution path before HTTP exists. A CTest fixture can
ask the runtime to invoke handler ID `1` directly and assert the returned result descriptor.

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

Before HTTP exists, the synthetic execution flow is:

1. load handwritten plan;
2. load handwritten bundle;
3. register handlers;
4. create synthetic request/job context;
5. call handler ID directly;
6. assert result descriptor;
7. cleanup scope/resources.

The C-side handler-call ABI for this future path is already shaped as
`sl_engine_call_handler(SlEngine*, SlEngineHandlerCall*, SlEngineResult*, SlDiag*)`.
TASK 07.B deliberately implements that entry point as unsupported for the noop engine; it
does not load modules, invoke exports, convert JavaScript values, or run `app.js`.

TASK 07.C deliberately uses a smaller smoke-only shape before the handwritten artifact
execution milestone: `sl_engine_eval_source` evaluates classic script text, and
`sl_engine_call_function0` calls a global function name such as `sloppy_smoke`. This is not
`app.js` module execution, not ESM loading, and not Sloppy Plan handler dispatch.

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
  app.js.map
```

CTest should execute the runtime in a synthetic mode and assert the returned result text.

## Acceptance Criteria For First Execution Milestone

The first execution milestone is accepted when:

- a handwritten `app.plan.json` declares one handler ID;
- a handwritten `app.js` registers that handler ID;
- runtime validates plan/bundle consistency;
- runtime invokes the handler by numeric ID;
- runtime converts a simple result;
- CTest integration covers the flow;
- missing handler, duplicate handler, and wrong bundle diagnostics are tested;
- no TypeScript compiler extraction is required for the milestone.
- no V8 types leak outside `src/engine/v8/`;
- no OS APIs appear outside `src/platform/*`.

## Open Questions

- Exact JS module format for `app.js`.
- Whether handler registration is explicit exports, bootstrap calls, or both.
- Whether source map parsing lives in runtime C or a helper library.
- Exact dev watch/restart behavior.
