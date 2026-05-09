# Architecture

Sloppy is three programs that ship as one tool:

- A **Rust compiler** (`compiler/`, binary `sloppyc`) that reads supported
  source and emits a Plan + bundle.
- A **C runtime kernel** (`src/core/`, `src/platform/`, `src/data/`,
  binary `sloppy`) that loads the Plan, manages app/request lifecycle,
  drives HTTP, and owns provider boundaries.
- A **C++ V8 bridge** (`src/engine/v8/`) that runs handler bundles
  inside an isolated V8 instance.

```text
source            sloppyc          .sloppy/             sloppy
src/main.ts  ──►  compile     ──►  app.plan.json    ──► load + validate
                                   app.js                 ▼
                                   app.js.map         engine/v8/*
                                                     register handlers
                                                         ▼
                                                       dispatch
```

Everything else — providers, platform code, the public stdlib — fits
between those three pieces along documented boundaries.

## Layers

| Layer        | Owner directory       | Owns                                                                     |
| ------------ | --------------------- | ------------------------------------------------------------------------ |
| Compiler     | `compiler/src/`       | Source parsing (Oxc), syntax validation, route/capability/schema extraction, deterministic artifact emission |
| Plan         | `src/core/plan_parse.c`, `include/sloppy/plan.h` | JSON parsing, schema validation, route/handler/feature/provider/capability checks |
| App host     | `src/core/app_host.c` | Startup, feature activation, request lifecycle, cleanup ordering         |
| HTTP         | `src/core/http*.c`, `src/platform/libuv/http_transport_libuv.c` | Parser, dispatch, response writer, transport |
| Engine bridge | `src/engine/v8/*`    | V8 isolate ownership, handler registration, Promise settlement, exception mapping |
| Providers    | `src/data/*`          | SQLite/PostgreSQL/SQL Server native code, value/result conversion, redaction |
| Platform     | `src/platform/*`      | OS calls, libuv transport backend                                        |
| Stdlib       | `stdlib/sloppy/*`     | Public JS surface — `Sloppy`, `Results`, `data`, `schema`, etc.          |
| CLI          | `src/main.c`, `src/cli/cli_*.inc` | Command parsing and dispatch into the layers above            |

## Boundary rules

Five rules that everything else follows:

1. **No OS APIs outside `src/platform/`.** Core modules see opaque
   platform types and Sloppy-owned callbacks. Including a Win32 or POSIX
   header in `src/core/` is a build break.
2. **No V8 types outside `src/engine/v8/`.** The C ABI between the
   kernel and the bridge passes Sloppy-owned types only. Public headers
   under `include/sloppy/` never see `v8::*`.
3. **No raw native pointers in JavaScript.** Every native resource the
   bridge exposes to JS is wrapped in a capability-checked handle.
4. **One owner thread per V8 isolate.** Wrong-thread entry fails before
   touching V8.
5. **Plan validation is fail-closed.** A request never executes against
   a malformed Plan, missing artifact hash, unsupported feature, or
   unknown handler ID.

These are enforced by code, not just documented. Boundary scanners under
`tools/windows/check-platform-boundaries.ps1` and CI gates fail PRs that
violate them.

## Lifecycles

**Source → artifacts** (compile time):

```
src/*.ts
   │  oxc parse
   ▼
AST
   │  sloppyc.rs::compile
   │    extract routes, capabilities, schemas, providers
   │    validate supported subset
   │    emit deterministic JS bundle + source map
   ▼
.sloppy/app.plan.json + app.js + app.js.map
```

**Artifacts → handlers** (startup):

```
sloppy run
   │  src/cli/cli_run.inc::sl_cli_command_run
   ▼
read app.plan.json
   │  src/core/plan_parse.c
   │    schema version, hashes, routes, handlers, providers, features
   ▼
app host validate
   │  src/core/app_host.c
   ▼
activate features (sl_feature_*)
   │
   ▼
init engine bridge
   │  src/engine/v8/engine_v8.cc (V8) or noop
   ▼
evaluate app.js
   │  bridge registers handlers via Sloppy-owned intrinsics
   ▼
ready (one-shot or transport listener)
```

**Request → response** (per request):

```
transport accepts bytes
   │  src/platform/libuv/http_transport_libuv.c
   ▼
parse + validate
   │  src/core/http.c, http_dispatch.c, request_validation.c
   ▼
match route (Plan-derived table)
   │  src/core/route.c
   ▼
build request context, open request scope
   │  src/core/http_context.c, scope.c
   ▼
dispatch handler ID through bridge
   │  src/engine/v8/intrinsics_*
   ▼
JS handler runs, returns Results.*
   │
   ▼
convert response, write
   │  src/core/http_response.c
   ▼
end scope, run cleanup hooks
```

The same code path runs for `--once` and for the long-lived listener.
There is no "test" or "dev" path that skips validation.

## Failure model

Every layer has a single failure mode: produce an `SlDiag` (see
`include/sloppy/diagnostics.h`) and abort the operation. Higher layers
attach context but don't reinterpret the inner failure.

| Failure point             | Surface                                              |
| ------------------------- | ---------------------------------------------------- |
| Compile error             | Stderr diagnostic with stable code; non-zero exit    |
| Plan parse / validation   | Startup diagnostic; runtime exits before V8 init     |
| Feature activation        | Startup diagnostic; runtime exits                    |
| V8 init / bundle eval     | Startup diagnostic; runtime exits                    |
| Handler exception         | 500 with redacted body; request scope cleaned up     |
| Provider error            | Bubbles to handler as a typed exception              |
| Transport error           | Connection closed; logged; server keeps serving      |

Late completions (e.g. a provider call that returns after its request
has timed out) only ever do cleanup. They cannot double-settle JS state.

## What this design buys

- **Tooling without execution.** `sloppy routes`, `capabilities`,
  `audit`, and `openapi` work off the Plan without entering V8.
- **Fail-fast startup.** Apps refuse to serve if their declared shape
  doesn't match the host's capabilities.
- **Replaceable engine.** The V8 bridge is the only thing that
  understands V8. A future engine swap is a bridge swap.
- **Reviewable boundaries.** Everything risky (FFI, OS calls, secret
  redaction) is concentrated in named directories.

## What this design costs

- **A bounded source subset.** The compiler can extract only what it
  can statically analyze. See [supported syntax](../reference/supported-syntax.md).
- **Edit-build-run** instead of edit-run. Source changes go through
  `sloppyc` first.
- **No npm.** No `node_modules` resolution; the public stdlib has to
  cover what an app actually needs.

## Where to read next

- [Runtime](runtime.md) — startup and dispatch in more detail
- [Plan](plan.md) — what's in `app.plan.json` and how it's validated
- [V8 bridge](v8-bridge.md) — bridge invariants and ownership
- [HTTP runtime](http-runtime.md) — parser, dispatch, transport
- [Provider runtime](provider-runtime.md) — how providers plug in
- [Memory model](memory-model.md) — arenas, lifetimes, ownership
- [Platform boundaries](platform-boundaries.md) — what crosses `src/platform/`
- [Async runtime](async-runtime.md) — owner threads, cancellation
- [Security model](security-model.md) — capabilities and redaction
