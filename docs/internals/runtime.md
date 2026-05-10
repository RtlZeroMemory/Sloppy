# Runtime

The runtime is the C kernel that boots a Sloppy app, loads its Plan,
runs handlers through the V8 bridge, and tears everything down. The
entrypoint is `src/main.c`; most of the work happens in `src/core/`.

## Startup sequence

`sl_cli_command_run` (in `src/cli/cli_run.inc`) runs every step. They
are all fail-closed — any error before `dispatch` aborts startup with a
diagnostic and a non-zero exit.

```text
1. parse CLI options                    src/cli/cli_common.inc
2. resolve project config               sloppy.json + appsettings*
3. compile source input (if any)        sloppyc handoff
4. read app.plan.json                   src/core/plan_parse.c
5. validate Plan                        plan_parse.c + app_host.c
6. stage bootstrap stdlib               src/core/app_host.c
7. activate required features           src/core/features.c
8. initialize logging runtime           src/core/logging.c
9. initialize engine bridge             src/engine/engine.c -> v8/*
10. evaluate generated bundle           src/engine/v8/engine_v8.cc
11. register handlers                   bridge intrinsics
12. build native route table            src/core/route.c
13. accept work (--once or listener)    src/platform/libuv/*
```

After step 13 the runtime is in steady state. Shutdown reverses 13->1
in cleanup order.

## Plan validation

`sl_plan_parse` returns an arena-owned `SlPlan`. Validation rejects, in
order:

- unknown or unsupported `schemaVersion`;
- target/runtime version mismatch;
- artifact files missing or hash mismatch;
- duplicate `(method, pattern)` route pairs;
- duplicate non-empty route names;
- handler IDs that don't appear in the handler table;
- duplicate provider or capability tokens;
- secret-bearing fields in Plan metadata that should have been redacted.

The strictness is intentional. The runtime treats compiler output as
untrusted input.

## Feature activation

`requiredFeatures[]` is a list of strings — `"stdlib"`, `"http"`,
`"sqlite"`, `"postgres"`, `"sqlserver"`, `"workers"`, `"crypto"`,
`"codec"`, `"net"`, `"os"`, `"fs"`, `"time"`. The activation loop in
`src/core/features.c` checks each against the runtime feature registry
and errors out if any is unavailable on this build.

A feature being declared in the Plan is *not* the same as the JS API
surface for that feature being implemented end-to-end. Features gate
runtime initialization; coverage is a separate question
([reference/stability.md](../reference/stability.md)).

## Engine bridge

The engine bridge in `src/engine/engine.c` exposes engine-neutral
operations to the rest of the runtime: initialize, evaluate bundle,
register handler, dispatch handler, shutdown.

`src/engine/v8/engine_v8.cc` is the V8 implementation. The noop
implementation lives alongside it for builds without V8 — every
operation returns an "unsupported" diagnostic, which lets metadata
commands run without V8 present.

V8 invariants are documented in [v8-bridge.md](v8-bridge.md).

## Logging Runtime

`src/core/logging.c` owns structured event construction, redaction, bounded
queueing, sink fan-out, flushing, and shutdown. `sloppy run` creates one
logging runtime before the engine bridge is initialized and passes it through
`SlEngineOptions`.

Current native sinks:

- memory sink for deterministic tests and bridge inspection;
- console sink with pretty or JSONL formatting;
- JSONL file sink with append mode, buffering, explicit flush, and shutdown
  close.

Events are copied into fixed-size native storage before queue admission. The
request path uses non-blocking enqueue with bounded capacity and drop counters.
Redaction is applied before events reach sinks.

## Request dispatch

The transport layer (`src/platform/libuv/http_transport_libuv.c`)
parses request bytes into `SlHttpRequest`. `sl_http_dispatch_dispatch`
in `src/core/http_dispatch.c` then:

1. matches the request against the route table;
2. enforces method, content-type, and body limits;
3. opens a per-request scope (`src/core/scope.c`);
4. materializes route params, query, and headers into the request
   context;
5. calls into the bridge with the matched handler ID and the context;
6. converts the returned result descriptor into an HTTP response;
7. closes the scope, running scope-owned cleanups.

A request's scope is the cleanup container — every per-request resource
(provider handles, allocations, transient services) is registered with
it. End of scope is end of life for those resources.

## Cleanup ordering

Cleanups run **latest-registered first** at every scope boundary.

```text
request scope dispose:
  for each cleanup in reverse order:
    invoke async dispose / dispose / close
  release arena
  release request memory

app scope dispose:
  drain pending request scopes
  shutdown provider runtime
  shutdown engine bridge
  flush and shutdown logging runtime
  release app arena
```

Late completions (provider results that arrive after request
cancellation, native callbacks that fire after the listener stopped)
only ever run their own cleanup. The runtime never notifies a
JavaScript handle that has already been disposed.

## CLI mode selection

`src/main.c` parses the top-level command and dispatches to a per-command
function in `src/cli/cli_*.inc`. The metadata commands (`routes`,
`capabilities`, `doctor`, `audit`, `openapi`) reuse the Plan parser but
skip the engine init steps; they don't enter V8 at all.

```text
src/main.c::main
  ├─ "build"          → cli_run.inc / sloppyc handoff
  ├─ "run"            → cli_run.inc::sl_cli_command_run
  ├─ "routes"         → cli_routes.inc
  ├─ "capabilities"   → cli_metadata.inc / cli_lookup.inc
  ├─ "doctor"         → cli_doctor.inc
  ├─ "audit"          → cli_audit.inc
  └─ "openapi"        → cli_openapi.inc
```

Source-input `sloppy run src/main.ts` invokes `sloppyc build` first,
writes artifacts to `.sloppy` by default (see
`SL_RUN_DEFAULT_SOURCE_OUT_DIR` in `src/main.c`), then executes the same
artifact path.

## What you can rely on

- The run path is the same regardless of `--once` vs listener.
- The Plan loaded by `sloppy run` is the same Plan inspected by
  `sloppy routes` / `audit` / `openapi`.
- Engine bridge calls are owner-thread only; cross-thread access fails
  before touching V8.
- Diagnostics never embed unredacted secrets (see
  [security-model.md](security-model.md)).

## Where to read next

- [Plan](plan.md) — schema and validation
- [V8 bridge](v8-bridge.md) — engine boundaries
- [HTTP runtime](http-runtime.md) — parser through transport
- [Async runtime](async-runtime.md) — cancellation, deadlines, late completion
- [Memory model](memory-model.md) — arenas and ownership
