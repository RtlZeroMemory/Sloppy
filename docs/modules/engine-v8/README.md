# Engine V8 Module

## Status

SDK detection implemented for TASK 07.A. TASK 07.B adds the engine-neutral C ABI and noop
engine stub. TASK 07.C adds the first V8-backed smoke path when the build is explicitly
configured with `SLOPPY_ENABLE_V8=ON` and a valid SDK. TASK 07.D adds the first basic V8
exception-to-`SlDiag` mapping skeleton for that smoke path. TASK 08.A adds the first
handwritten `app.plan.json` + `app.js` execution smoke through the V8 bridge.
EPIC-21 compiler output deliberately targets the same classic-script global-function shape
for now. EPIC-22 `sloppy run` requires this V8-enabled path to execute compiler artifacts.
EPIC-23 extends the bridge with a narrow handler call that materializes one plain request
context object and converts supported result descriptors into native response descriptors.
EPIC-24 adds the first bootstrap runtime handoff: `sloppy run` loads a source-controlled
classic bootstrap asset, evaluates generated `app.js`, lets generated code register
numeric handlers through a narrow intrinsic, validates the plan against that runtime-owned
handler table, and dispatches by handler ID.
ENGINE-03 adds the first real async handler runtime: returned Promises are settled through
an explicit owner-thread V8 microtask checkpoint, fulfilled values flow through the normal
result conversion path, rejected Promises produce deterministic async diagnostics, and
Promises still pending after the bounded microtask drain fail as a deadline-style handler
failure rather than `[object Promise]` success.
ENGINE-05 wires the SQLite V8 bridge to Plan provider metadata and the native database
capability hook. The bridge fails closed when hook inputs are absent and keeps all
provider-specific logic inside `src/engine/v8/intrinsics_sqlite.cc`.
ENGINE-12.AB adds the first native completion/backend and V8 owner-thread continuation
boundary. Native code can post a completion through `SlAsyncLoop`; V8 Promise
fulfillment/rejection happens only in `src/engine/v8/async_scheduler.cc` when the owning
engine thread drains that completion. The libuv backend is internal to
`src/platform/libuv/` and does not create Node/libuv public compatibility. ENGINE-12.CD
defines cancellation/deadline/shutdown/backpressure policy for native provider/offload
operations and proves it in default native tests. V8 remains the owner-thread settlement
boundary: provider executors may post completions, but provider threads must never enter V8
and no libuv/provider concept is exposed to JavaScript.
ENGINE-19.BC registers the existing V8 runtime, owner-thread, native async scheduler, and
HTTP dispatch integration executables under `conformance.v8.*` CTest names. These tests
remain V8-gated and skipped/not configured when the SDK is unavailable; default non-V8
gates do not prove this lane.
ENGINE-02.E source-input run does not add a V8 source loader. `sloppy run <source.js>` and
`sloppy run` with `sloppy.json` compile through `sloppyc`, validate generated artifacts,
and only then reach this same V8 artifact execution path.
Post-ENGINE-16 Roadmap-2 does not expand the public V8 API first. ENGINE-26 must lock
owner-thread scheduling and cross-thread completion invariants, ENGINE-27 must feature-gate
intrinsic registration, and ENGINE-28 must route provider results back through the owner
thread before provider expansion. Source-map exception primary spans are implemented for
the current claim; async stack remapping and broader source-frame fidelity remain later.
ENGINE-27.A/B adds the registry source of truth for the `v8` runtime feature and the
feature-activation prerequisite used by app-host startup. Non-V8 builds now have a stable
feature diagnostic path before engine creation. ENGINE-27.C/D passes the validated active
feature set into the V8 bridge so `__sloppy_register_handler` is installed for active
`stdlib.framework/app` plans and `__sloppy.data.sqlite` is installed only when
`provider.sqlite` is active. Direct low-level V8 smoke tests that omit a feature set keep
legacy intrinsic installation as bridge coverage, not as app-host startup policy.
CORE-FS-01.C/D/H registers the private `__sloppy.fs` intrinsic namespace only when the
validated runtime feature set activates `stdlib.fs`. The namespace exposes the core
filesystem operations used by `stdlib/sloppy/fs.js`. CORE-FS-01.E/F adds advanced
directory/temp/symlink/atomic operations and resource-table-backed FileHandle stream
helpers. CORE-FS-01.G adds resource-table-backed FileWatcher open/next/close intrinsics
with bounded non-recursive events. The bridge uses an optional borrowed
`SlEngineOptions.filesystem_policy` for path/root enforcement; when it is omitted, V8
keeps the documented development fallback roots for low-level smoke/source-input tests
until app-host config wiring supplies project policy.
CORE-TIME-01.C/D/G installs the private `__sloppy.time` namespace when the active Plan
enables `stdlib.time`. The native delay scheduler never enters V8; it posts owned
`SL_ASYNC_OPERATION_TIMER` completions to the engine async loop, and the owning isolate
thread resolves or rejects Promises during the normal native async drain. Intervals,
scheduled jobs, and fake clocks are implemented in the JavaScript stdlib layer and are
covered by bootstrap tests; native owner-thread Time evidence remains the V8-gated delay
bridge.
CORE-CRYPTO-01.E registers the private `__sloppy.crypto` namespace for active
`stdlib.crypto` plans. The namespace exposes bounded random, SHA-2, HMAC, constant-time,
and password helpers used by `stdlib/sloppy/crypto.js`; it does not expose raw native
pointers or backend handles. `Password.hash`, `Password.verify`, and
`Password.needsRehash` use worker-thread requests and settle on the V8 owner thread.
CORE-NET-01.C/D/H registers the private `__sloppy.net` namespace for active `stdlib.net`
plans. The bridge exposes TCP client/connection operations through JS-safe resource IDs;
blocking connect/read/write/close work runs on owned native worker threads, and Promise
settlement happens through the engine async loop on the V8 owner thread. Listener/accept,
DNS policy, richer socket options, and deadline/cancellation hardening remain later
CORE-NET slices.
CORE-CODEC-01.C/D/I registers the private `__sloppy.codec` namespace marker for active
`stdlib.codec` plans. Base64/Base64Url/Hex/UTF-8 algorithms live in the bootstrap stdlib
for this slice; the namespace intentionally exposes no raw native handles and no public
compatibility promise. Binary, Compression, Checksums, and owner-thread Promise settlement
paths remain dedicated later CORE-CODEC work.
ENGINE-27.E/F pins the inactive SQLite intrinsic behavior: stdlib code that reaches
`data.sqlite.open(...)` without an active `provider.sqlite` feature reports
`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE` and names `__sloppy.data.sqlite` as the missing V8
intrinsic namespace. That is a missing-feature diagnostic, not a raw V8 property lookup
failure.

## Purpose

Host V8 behind an isolated C++ bridge without leaking V8 types into the C runtime.

## Scope

Implemented now:

- explicit CMake opt-in for V8 SDK validation;
- shared Windows V8 SDK discovery through `tools/windows/v8-sdk.ps1`;
- `SLOPPY_V8_ROOT` and `SLOPPY_V8_SDK_HINTS` override paths;
- imported `Sloppy::V8` interface target after SDK validation succeeds;
- Windows helper validation through `tools/windows/resolve-v8-sdk.ps1` and
  `tools/windows/fetch-v8.ps1 -ValidateOnly`.
- `include/sloppy/engine.h` with opaque `SlEngine`;
- `SlEngineOptions`, `SlEngineInfo`, and explicit engine kind values;
- create/destroy/info lifecycle shape;
- `SlEngineHandlerCall`, `SlEngineResult`, and `sl_engine_call_handler`;
- noop engine creation for `SL_ENGINE_KIND_NONE`;
- deterministic unsupported behavior for noop handler calls and V8-disabled builds;
- V8 isolate/context creation and destruction in `src/engine/v8/`;
- classic script source evaluation through `sl_engine_eval_source`;
- zero-argument global function calls through `sl_engine_call_function0`;
- one-argument handler calls through `sl_engine_call_function_with_context`;
- copied plain-string compatibility results and supported `Results.*` descriptor responses
  through `SlEngineResult`.
- basic exception diagnostics for compile/eval/call failures.
- `sl_runtime_contract_call_handler` maps a parsed plan handler ID to a named JS global
  and invokes it through the engine boundary;
- V8-gated handwritten artifact integration fixtures under
  `tests/integration/execution/handwritten_smoke/`.
- compiler-generated `app.js` artifacts assign legacy `globalThis.__sloppy_handler_<id>`
  globals and call `__sloppy_register_handler(N, handler)`;
- the classic bootstrap runtime asset at `stdlib/sloppy/internal/runtime-classic.js`
  installs the current V8-runnable `Results.*` descriptor helpers on
  `globalThis.__sloppy_runtime`;
- `sloppy run --artifacts <dir>` creates a V8 engine, evaluates the artifact `app.js`, and
  dispatches matched GET routes through the runtime-contract helper with an EPIC-23
  request context argument;
- supported handler return values are plain strings or `Results.text/json/ok/noContent`
  and `problem` descriptors. Unsupported descriptor kinds and malformed descriptors fail
  safely with diagnostics.
- Promise-returning and `async` handlers settle when their returned Promise completes during
  the explicit V8 microtask drain. Fulfilled Promises convert the resolved string or
  supported `Results.*` descriptor, rejected Promises use
  `SLOPPY_E_ENGINE_PROMISE_REJECTION`, and still-pending Promises use
  `SLOPPY_E_ENGINE_PROMISE_PENDING` plus `SL_STATUS_DEADLINE_EXCEEDED`.
- request contexts now expose a plain `ctx.signal` object with `aborted`, `reason`, and
  `throwIfAborted()` plus `ctx.deadline` as `null` or an expired deadline marker. The
  bridge checks a native cancellation token before entering JavaScript and again before
  async result conversion. There is still no public timer API or client-disconnect source.
- the engine-owned resource table is available to V8-internal provider bridges;
- provider-specific intrinsic modules are split out of `engine_v8.cc`. `intrinsics.cc`
  aggregates bridge registration and `intrinsics_sqlite.cc` installs the SQLite bridge
  under `__sloppy.data.sqlite`.
- private string interop helpers under `src/engine/v8/string_interop.*` define the
  V8/native string boundary: native views become V8 strings only on the engine owner
  thread, V8 strings copied back to C become arena-owned native views, and byte conversions
  copy before leaving the bridge helper.
- ENGINE-22.D adopts those helpers in the provider-neutral bridge paths: engine intrinsics,
  request context materialization, HTTP `Results.*` descriptor conversion, JSON response
  bytes, and exception strings. SQLite result and parameter adoption stays scoped to
  ENGINE-22.E.
- V8 creation can borrow the parsed Plan and immutable capability registry through
  `SlEngineOptions`; provider bridges may use those pointers only as hook inputs while the
  app host keeps their storage alive.
- `async_scheduler.cc` owns the native-completion-to-Promise scheduler boundary. Its
  queued records keep V8 resolver handles inside the V8 module, use `SlAsyncLoop` only as
  a native completion transport, retain/release native scope hooks across queued work, and
  reject detectable wrong-thread dispatch before entering V8.

Later scope:

- true V8 ESM module loading and a production module cache;
- async stack remapping and rich source-frame rendering for generated app modules;
- mapping real provider/HTTP/timer completions onto the new async backend. ENGINE-12.AB
  proves the transport/scheduler boundary with native test completions only; it does not
  add timers, fetch, Node APIs, or public async sources;
- ENGINE-12 follow-up work after ENGINE-12.CD: full SQLite runtime completion through the
  provider-instance executor model, HTTP-specific disconnect/shutdown integration, and
  stress evidence for many pending operations without benchmark claims;
- broad runtime source-map remapping beyond V8 exception primary spans.

## Non-goals

No ESM resolver, full app host, arbitrary import graph, workers, inspector, snapshots, hot reload, Node
compatibility, timers, fetch, fs, process/Buffer APIs, npm resolution, or package-manager
behavior. EPIC-22/23/24 HTTP usage is limited to the dev-only CLI path and does not make
this bridge a production server boundary.

## Intrinsic Module Layout

`src/engine/v8/engine_v8.cc` is the V8 engine core. It owns process/platform acquire,
isolate/context lifetime, engine-neutral handler registration, source evaluation,
owner-thread checks, and bounded Promise orchestration. It must not grow
framework-specific or provider-specific bridge implementations.

Framework and provider bridge code belongs in sibling V8 modules:

- `engine_v8_internal.h` is a private header for files under `src/engine/v8/` only. It
  defines the backend shape and exposes the engine-owned `SlResourceTable` to bridge
  modules without leaking V8 or resource internals outside the directory.
- `http_bridge.cc` owns HTTP request context materialization and `Results.*` descriptor
  conversion into `SlEngineResult`.
- `intrinsics.cc` is the aggregator that registers provider bridges into the private
  `__sloppy.data` namespace.
- `intrinsics_sqlite.cc` owns SQLite-specific argument validation, parameter conversion,
  row materialization, Plan provider lookup, capability checks, resource-table lookup,
  cleanup callback, and native provider calls.
- `async_scheduler.cc` owns owner-thread native continuation scheduling and Promise
  settlement from native completions.
- `intrinsics_fs.cc` owns filesystem argument validation, request ownership, offloaded
  native filesystem calls, and owner-thread Promise settlement for active `stdlib.fs`
  plans.
- future `intrinsics_time.cc` owns Time argument validation, timer resource IDs,
  cancellation/deadline conversion, and owner-thread Promise settlement for active
  `stdlib.time` plans.
- `intrinsics_crypto.cc` owns crypto argument validation and bounded random/hash/HMAC/
  constant-time dispatch for active `stdlib.crypto` plans. The bridge performs only small
  bounded hash/HMAC work inline; `Password.hash`, `Password.verify`, and
  `Password.needsRehash` offload away from the V8 owner thread and settle through the
  owner-thread async loop.
- `intrinsics_codec.cc` owns the active `stdlib.codec` namespace marker today. Later
  Binary/Compression/Checksum bridge functions must stay in this V8 module, preserve owned
  byte/text boundaries, and settle any async work on the owner thread. Compression work
  that can materially block must offload away from the V8 owner thread.
- Native provider, filesystem, HTTP, timer, or other future completions must pass any
  terminal-state guard before reaching owner-thread settlement; provider/libuv/offload
  domains still never enter V8 directly.
- Current SQLite bridge callbacks remain synchronous and owner-thread-bound. They are
  documented ENGINE-28 debt, not an allowed general pattern for blocking provider work.

Future framework-specific V8 bridge code must add a dedicated sibling module, not expand
`engine_v8.cc`.
Future PostgreSQL, SQL Server, and other native bridges should follow the same pattern:
add an `intrinsics_<provider>.cc` file, register it from `intrinsics.cc`, keep public JS in
the stdlib wrapper, and keep all V8/provider conversion code out of `engine_v8.cc`.
Resource table ownership remains engine-level; provider modules only insert, look up, and
close their own resource kinds through the table.

## Public/Internal API

Runtime-visible APIs are C-shaped and engine-neutral through `include/sloppy/engine.h`.
`SlEngine` is opaque to callers. Public structs use Sloppy primitives (`SlStatus`,
`SlDiag`, `SlStr`, `SlArena`, and `SlHandlerId`) and do not expose C++ or V8 types. V8
implementation details stay under `src/engine/v8/`.

Current behavior:

- `SL_ENGINE_KIND_NONE` creates an arena-backed noop engine;
- `sl_engine_destroy(NULL)` is allowed;
- `sl_engine_destroy(engine)` is idempotent; double destroy is a no-op;
- a wrong-thread destroy is side-effect free and does not enter V8. The owner thread must
  perform the real destroy that releases bridge-owned resources;
- calls after destroy return `SL_STATUS_INVALID_STATE` with an engine lifecycle diagnostic
  where the API accepts `out_diag`;
- `sl_engine_info` returns stable noop metadata for active noop engines;
- `SL_ENGINE_KIND_V8` creates a V8 isolate/context only when V8 is enabled at configure
  time; otherwise it returns `SL_STATUS_UNSUPPORTED`;
- V8 creation may borrow the already-validated `app.js.map` bytes plus the generated source
  label they apply to. The bridge uses those bytes only for diagnostics and only when a V8
  exception originates from that generated app source;
- `sl_engine_eval_source` evaluates borrowed classic JavaScript source strings in the
  engine context, using `source_name` as the generated JavaScript diagnostic label;
- `sl_engine_call_function0` looks up a named global function, calls it with no arguments,
  and copies a plain string return value into the caller-provided arena for compatibility
  smoke paths;
- `sl_engine_call_function_with_context` looks up a named global function, materializes one
  plain request context object, and converts supported `Results.*` descriptors into native
  response descriptors;
- `__sloppy_register_handler(id, handler)` exists only in the V8 runtime context and accepts
  a positive numeric handler ID plus a callable handler. Duplicate IDs, nonnumeric IDs, and
  non-callable handlers fail during app evaluation with deterministic V8 diagnostics;
- `__sloppy.data.sqlite` exists only in the V8 runtime context and is normally installed by
  the SQLite provider intrinsic module when `provider.sqlite` is active. Low-level bridge
  callers that pass no runtime feature set may still receive `__sloppy.data.sqlite` for
  legacy smoke coverage; app-host startup passes a feature set and uses strict
  `provider.sqlite` gating. The namespace exposes internal open/exec/query/queryOne/close
  callbacks used by the stdlib wrapper, not a public raw native API;
- SQLite bridge parameter conversion rejects JavaScript parameter arrays above 32,766
  elements before any native parameter vector reserve/allocation. The failure is a stable
  redacted parameter-count error and does not include SQL parameter values;
- SQLite bridge opens can resolve `data.sqlite("main")` through Plan provider token
  `data.main`; every open/read/write call checks the native database capability hook before
  provider work. Missing Plan/capability hook inputs, missing providers, wrong provider
  kinds, and denied access fail as ordinary V8 bridge exceptions;
- `sl_engine_validate_registered_handlers` checks that every plan handler ID was registered
  by generated app code before `sloppy run` starts serving or dispatching `--once`;
- `sl_engine_call_registered_handler_with_context` dispatches by registered handler ID and
  does not expose raw native pointers to JavaScript;
- V8 handler calls drain microtasks after app evaluation and after each handler call on the
  owner thread. A returned Promise that fulfills during that drain is converted exactly like
  a synchronous handler return. A rejected Promise fails with
  `SLOPPY_E_ENGINE_PROMISE_REJECTION`. A Promise that remains pending after the bounded
  microtask drain fails with `SLOPPY_E_ENGINE_PROMISE_PENDING` and
  `SL_STATUS_DEADLINE_EXCEEDED`; no timer/fetch/native async queue is invented to keep it
  alive. Recursive Promise microtask chains that exceed the internal checkpoint cap also
  fail through the same pending/deadline diagnostic instead of hanging the owner thread.
- context-aware calls reject pre-cancelled request contexts before user code with
  `SLOPPY_E_ENGINE_CANCELLED` or `SLOPPY_E_ENGINE_BACKPRESSURE` depending on the native
  token reason. Deadlines cancel through the same token path. Request cleanup remains owned
  by the caller's request scope and is exercised for sync throw, resolve, reject,
  cancellation, and pending-promise timeout paths.
- `sl_runtime_contract_call_handler` looks up a handler ID in `SlPlan`, validates the
  export name, and calls the named global with no arguments;
- `sl_runtime_contract_call_handler_with_context` performs the same plan lookup and calls
  the runtime-owned registered handler with the EPIC-23 request context;
- EPIC-21/24 generated `app.js` remains a classic script, but it now expects the bootstrap
  runtime asset to have installed `globalThis.__sloppy_runtime` and it registers handlers
  through `__sloppy_register_handler`;
- `sloppy run` fails clearly with "requires V8-enabled build" when this bridge is not
  compiled in;
- `sl_engine_call_handler` exists as a future engine-owned handler dispatch shape but
  still returns `SL_STATUS_UNSUPPORTED` for the noop engine.

Build options:

- `SLOPPY_ENABLE_V8` defaults to `OFF`. Setting it to `ON` enables the V8 SDK gate.
- `SLOPPY_ENGINE` defaults to `none`. Setting it to `v8` also enables the V8 SDK gate.
- `SLOPPY_V8_ROOT` points to a prebuilt V8 SDK root when an explicit shell-local override
  is needed. The Windows wrapper can also discover `.sdeps/v8/windows-x64` in this
  worktree or another registered git worktree, and can search additional roots from
  `SLOPPY_V8_SDK_HINTS`.

Default foundation builds and CI do not require V8.

The current source-built Windows SDK is a release/RelWithDebInfo SDK. Do not link it into
the Debug CRT build. Use `windows-relwithdebinfo` for local V8 execution tests unless a
separate matching Debug V8 SDK is built and packaged.

CI behavior:

- required pull-request CI leaves V8 disabled on Windows, Linux, and macOS;
- optional V8 validation is available only through manual `workflow_dispatch`;
- the manual job requires `enable_v8=true` plus a runner-local `v8_root` input pointing to
  a preinstalled SDK;
- if the SDK path is empty or absent, the job reports skipped/not configured and does not
  claim V8 configure/build/test coverage;
- when configured, the job runs `tools/windows/fetch-v8.ps1 -ValidateOnly`, configures
  `windows-relwithdebinfo` with `SLOPPY_ENABLE_V8=ON`, builds, and runs CTest.

Passing required CI does not prove V8 execution. Report V8-enabled results separately from
default gate results.

For MAIN-01, the positive runtime proof is explicitly V8-gated:

```powershell
sloppy run --artifacts .sloppy-main-smoke --once GET /
```

That command must be run from a V8-enabled build and is expected to return
`Hello from Sloppy` for the canonical compiler hello artifact. If the SDK/build is not
available, report the V8 smoke as skipped or unavailable and rely only on the non-V8
diagnostic test; do not describe default gates as V8 execution evidence.

On Windows, prefer the repo wrapper instead of direct `cmake`:

```powershell
.\tools\windows\resolve-v8-sdk.ps1
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
```

The wrapper imports the Visual Studio C++ environment, keeps vcpkg attached on fresh
configure, and recreates a preset build directory if an earlier direct `cmake` attempt left
a stale cache without the vcpkg toolchain. Direct `cmake --preset` remains available for
custom automation, but that caller owns the MSVC, Windows SDK, and vcpkg environment.
Direct CMake callers must pass `-DSLOPPY_V8_ROOT=<sdk-root>` themselves; automatic
worktree discovery is a Windows script feature.

SDK discovery order for Windows scripts is:

1. command-line `-V8Root`;
2. `SLOPPY_V8_ROOT`;
3. `SLOPPY_V8_SDK_HINTS`, split by the platform path separator;
4. this worktree's `.sdeps/v8/windows-x64`;
5. `.sdeps/v8/windows-x64` in registered git worktrees.

`tools/windows/v8-sdk.ps1` is the single helper for V8 SDK manifest/layout validation and
path resolution. New scripts must dot-source it instead of reimplementing their own
`SLOPPY_V8_ROOT` checks.

## Module Loading Strategy

EPIC-24 deliberately chooses the smallest bridge from classic-script smoke tests toward the
real bootstrap/app runtime. V8 still evaluates classic scripts in one Sloppy-owned context.
`sloppy run` loads the source-controlled bootstrap asset
`internal/runtime-classic.js` from the configured stdlib root, then evaluates the generated
`app.js` artifact in that same context. The app artifact reads Sloppy-owned globals and
registers handlers through the intrinsic; it does not ask V8 to resolve public imports.

The public authoring import `import { Sloppy, Results } from "sloppy";` is handled by
`sloppyc`. The compiler recognizes only the bare specifier `"sloppy"` and emits a generated
classic script that reads `Results` from `globalThis.__sloppy_runtime`. Arbitrary bare
imports such as `"express"`, `"fs"`, and `"node:fs"` fail in the compiler MVP with a clear
unsupported import diagnostic. Runtime import resolution for npm packages, Node built-ins,
relative source module graphs, dynamic imports, and import maps is not implemented.

The stdlib lookup policy is deterministic:

1. `sloppy run --stdlib <dir>` uses the explicit directory. Relative explicit directories
   are resolved by the process in the usual way, so callers should prefer absolute paths in
   automation.
2. Build-tree executables use the CMake-staged bootstrap root compiled into the binary:
   `<build>/lib/sloppy/bootstrap/sloppy`.
3. Local ZIP/TAR packages stage the source-controlled stdlib root at
   `lib/sloppy/stdlib/sloppy`; executable-relative installed lookup remains deferred, so
   packaged smoke tests may pass that root explicitly.

Missing stdlib roots, missing `internal/runtime-classic.js`, missing `app.js`, malformed
modules, duplicate registrations, intrinsic misuse, and plan references to unregistered
handlers all fail before serving. Default non-V8 gates do not execute this path.

## Ownership/Lifetime Rules

The noop engine is allocated from the caller-provided arena and owns no external resources.
The V8 engine wrapper is arena-backed only after V8 isolate/context creation succeeds, so
failed creates do not consume caller arena capacity. V8 isolate/context resources are
bridge-owned and released by `sl_engine_destroy`. Callers must destroy an engine before
resetting the arena that backs the opaque handle. Destroy is idempotent. After destroy, the
arena-owned handle remains only as inactive storage; eval, call, validation, and future
handler APIs return `SL_STATUS_INVALID_STATE` rather than entering backend state.

`sl_engine_call_function0` copies supported plain-string compatibility results into the
caller-provided result arena. `sl_engine_call_function_with_context` and
`sl_engine_call_registered_handler_with_context` copy supported descriptor response bodies
into that same result arena and borrow the native request context only while constructing
the JS argument. Returned views remain valid until that arena is reset or its backing
storage ends. No V8 handle or raw native pointer escapes the bridge, handler functions are
stored in a bridge-owned table, and JS never receives raw native pointers.

Future JS-native resource intrinsics must expose only opaque JS objects that carry
`SlResourceId { slot, generation }` values. V8 `External` pointers, pointer-sized numbers,
or provider addresses are forbidden as JS-visible handles. The bridge must validate the
resource table entry's slot, generation, live state, and expected kind before provider code
runs. Stale and wrong-kind handles must fail with deterministic resource diagnostics. The
SQLite bridge consumes the MAIN1-07 table from `intrinsics_sqlite.cc` rather than adding a
V8-specific handle registry, and stores provider/capability metadata beside the native
connection resource so later calls can re-check authority.

Diagnostics produced by the V8 bridge are built through `SlDiagBuilder` in the engine
arena. Exception message text, generated source names, hints, and bounded stack summaries
are copied before returning to C. They remain valid until the engine arena is reset; callers
may still pass `out_diag == NULL`, in which case the bridge returns the failure status
without materializing a diagnostic.

String interop policy:

- native `SlStr` values passed into V8 use explicit lengths and do not require NUL
  termination;
- conversion into V8 is an owner-thread operation and fails before V8 entry from any other
  thread;
- V8 strings copied to native storage are staged only inside the private C++ bridge and
  then copied into caller/engine arenas before C observes them;
- HTTP result descriptor `kind`, `contentType`, text bodies, JSON/problem serialized bytes,
  custom headers, and exception message/source/stack text are copied before C observes them;
- V8 objects never retain pointers into request, scratch, SQLite, or other transient
  native storage;
- JavaScript still never receives raw native pointers, pointer-sized numbers, or V8
  `External` handles as Sloppy resources.

## Invariants

One V8 isolate/context has one owner thread: the thread that created the engine. Engine
creation, source evaluation, handler calls, registered-handler validation, and destroy are
owner-thread operations. Calls from another thread fail before entering V8 with
`SL_STATUS_INVALID_STATE` and an engine diagnostic when the API has `out_diag`.
`engine.v8.owner_thread` also checks that V8 engine creation records the creating thread as
the owner thread. Cross-domain policy lives in `include/sloppy/execution_domain.h`:
provider workers, blocking offload workers, libuv callbacks, HTTP callbacks, and generic
async completions are non-V8 domains and must post JavaScript continuations back through
the V8 owner-thread scheduler.

V8 headers and `v8::*` types may appear only below `src/engine/v8/`.

The current ABI is not a scheduler. It enforces owner-thread entry for V8 but does not post
work between threads, run worker callbacks, or allow native worker threads to enter an
isolate. Future worker/event-loop integration must post work back to the owner thread.

TASK 09.B's `SlAsync` model lives in the C runtime core and does not include V8 types.
ENGINE-03 updates the alpha Promise policy: returned Promises and `async` handlers are
supported only when the Promise settles during the explicit owner-thread microtask drain.
Fulfilled Promises convert through the normal result path, rejected Promises fail with
`SLOPPY_E_ENGINE_PROMISE_REJECTION`, and Promises still pending after the bounded checkpoint
fail with `SLOPPY_E_ENGINE_PROMISE_PENDING` and `SL_STATUS_DEADLINE_EXCEEDED`. The bridge
checks the native cancellation token before entering JavaScript and before async result
conversion. Future native completions must keep the same V8-owner-thread rule and keep V8
handles and microtask policy inside `src/engine/v8/`.

V8 requires process-wide platform initialization. TASK 07.C initializes that state once,
keeps it private to `src/engine/v8/`, and intentionally leaves it alive for process
lifetime. Per-engine destroy releases isolates and contexts only. A future explicit runtime
shutdown task owns any decision to call `v8::V8::Dispose()` / `DisposePlatform()`.
The bridge sets a bounded V8 stack size during process initialization. On Windows MSVC-style
builds, `sloppy.exe` also reserves enough process stack for V8 handler entry from the deeper
libuv HTTP transport callback path; this is transport/runtime stability evidence, not a
performance claim.

Expected SDK layout:

```text
<SLOPPY_V8_ROOT>/
  include/v8.h
  include/libplatform/libplatform.h
  lib/v8_monolith*.lib
  lib/v8_libplatform*.lib
  lib/v8_libbase*.lib
  lib/libc++*.lib
  support/libcxx/include/
  support/libcxx/buildtools/__config_site
  bin/  # optional runtime DLLs for dynamic SDKs
```

The approved local source-build strategy uses official V8/depot_tools source, GN, Ninja,
and an ignored SDK under `.sdeps/v8/windows-x64`. The source checkout may live in an
ignored local work root such as `.sdeps/v8-work` or an explicit local drive path. The
normal default build never fetches or builds V8.

The current GN shape is an embeddable monolithic release build:

```gn
is_debug = false
target_cpu = "x64"
is_component_build = false
v8_monolithic = true
v8_use_external_startup_data = false
v8_enable_temporal_support = false
```

Temporal is disabled for the smoke SDK because it pulls Rust `temporal_rs` link
requirements that are outside the 0.2 runtime contract. Chromium libc++ support headers
and `libc++.lib` are packaged because the current V8 public C++ ABI uses libc++ types in
public functions such as `v8::platform::NewDefaultPlatform`.

Exact prebuilt artifact hosting, checksum policy, and update cadence remain deferred.

## Distribution Policy

The V8 SDK is a build-time input, not a package payload. Source builds use a local ignored
SDK under `.sdeps/v8/<platform-arch>` or an explicit `SLOPPY_V8_ROOT`. The repository must
not commit V8 headers, import libraries, source trees, or build outputs.

End-user packages should not require a V8 SDK installation. The preferred release strategy
is static/monolithic V8 linking when practical. If a dynamic V8 build is used instead, the
package may include only the runtime DLL/shared-library files needed to run the `sloppy`
executable, staged under `lib/sloppy/engines/v8/`. It must not include SDK headers, import
libraries, depot_tools checkouts, GN/Ninja build trees, or `.sdeps/`.

The current default local package is non-V8 and records `containsV8Runtime: false` and
`containsV8Sdk: false` in `manifest.json`. `tools/windows/package.ps1 -IncludeV8Runtime`
is intentionally gated on an explicit SDK root and copies only dynamic runtime files from
`bin/`; monolithic/static SDK library content is never copied into packages.

Package validation has two levels:

- default package smoke validates archive layout, CLI startup/help, stdlib assets, manifest
  fields, checksum entries, and V8 SDK exclusion outside the checkout. It does not prove
  V8 execution.
- V8 package smoke requires a V8-enabled package, runtime-file validation when dynamic V8
  files are expected, and a V8-gated `sloppy run --artifacts ... --stdlib
  <package-root>/lib/sloppy/stdlib/sloppy --once GET /` execution from the extracted
  package. If that command did not run, do not report packaged V8 runtime success.

## Diagnostics

Current engine diagnostics include `SLOPPY_E_UNSUPPORTED_ENGINE` for unsupported noop
operations, `SLOPPY_E_ENGINE_COMPILE_ERROR` for V8 syntax/compile failures,
`SLOPPY_E_ENGINE_EXCEPTION` for thrown eval/function exceptions, and
`SLOPPY_E_ENGINE_CALL_ERROR` for missing/non-callable smoke globals, missing registered
handlers, and unsupported smoke result types. HTTP handler result descriptor conversion
failures use `SLOPPY_E_INVALID_HTTP_RESULT`. Promise rejections use
`SLOPPY_E_ENGINE_PROMISE_REJECTION`; Promises that remain pending after the bounded
microtask drain use `SLOPPY_E_ENGINE_PROMISE_PENDING`; pre-cancelled request contexts use
`SLOPPY_E_ENGINE_CANCELLED` or `SLOPPY_E_ENGINE_BACKPRESSURE` depending on the cancellation
reason. Async diagnostics are ordinary `SlDiag` values and can be rendered through the
stable JSON renderer. When a caller has matching source text, the core diagnostic renderer
can also emit deterministic JSON source frames, but the V8 bridge does not yet attach
source-map-remapped frames to async stacks. There is still no CLI-wide async diagnostic JSON
mode. Eval-time intrinsic failures such as invalid `__sloppy_register_handler(...)`
arguments or duplicate handler registration are reported as `SL_DIAG_ENGINE_EXCEPTION`
because they are raised while evaluating the app module.

The mapping is intentionally bounded. It captures V8 exception message text, generated
source/resource name when available, 1-based line and column when V8 reports them, and a
bounded stack string as a related note when practical. V8 reports start columns as
zero-based; the bridge converts them to Sloppy's 1-based diagnostic column convention.
ENGINE-15.B adds Source Map v3 `mappings` consumption for compile/eval/call exception
primary spans when `sloppy run` has already validated `app.js.map`. Successful remaps use
the author source as the primary span; missing maps, malformed maps, and uncovered
locations keep the generated span and do not fake source awareness. No arbitrary bundler
maps, async stack remapping, Node compatibility, or package-manager behavior is implemented
here.

## Tests

Current checks:

- default non-V8 configure/build/test gates;
- V8-enabled configure fails during CMake configure when `SLOPPY_V8_ROOT` is empty or
  invalid;
- `tools/windows/fetch-v8.ps1 -ValidateOnly` reports missing layout pieces for the
  resolved SDK, and `-V8Root <sdk-root>` can still validate an explicit override;
- C standards scanner rejects V8 headers and `v8::` references outside `src/engine/v8/`.
- `core.resource.lifecycle` covers the V8-independent resource ID/table lifecycle that
  future JS-native handle intrinsics must use.
- `core.engine.abi` covers noop create/info/destroy, invalid options, V8 unsupported
  creation in non-V8 builds, noop handler-call unsupported behavior, noop eval/call
  unsupported behavior, and invalid handler-call arguments.
- `engine.v8.smoke` is registered only when V8 is enabled and covers classic script
  evaluation, global function call returning `sloppy-ok`, syntax-error diagnostics,
  missing function diagnostics, non-callable global diagnostics, thrown function
  diagnostics, source-map-remapped thrown function and registered-handler diagnostics,
  malformed-map generated fallback, unsupported result diagnostics, handler intrinsic misuse, duplicate handler
  registration, missing registered handler diagnostics, registered handler context
  dispatch, Promise fulfillment/rejection/pending behavior, stable JSON rendering for a
  Promise rejection diagnostic, cancellation/deadline context snapshots, request-scope
  cleanup across sync throw and async success/failure paths, and create/destroy/create
  lifecycle behavior. It also covers active-feature-set intrinsic registration: SQLite
  provider-token opens, exec/query/queryOne, close, stale handles, invalid parameters,
  missing provider metadata, missing capability registry fail-closed behavior, denied
  capability reads, and the inactive-`provider.sqlite` path where the SQLite intrinsic is
  not registered.
- `engine.v8.owner_thread` is registered only when V8 is enabled and covers owner-thread
  lifecycle checks, wrong-thread eval rejection before entering V8, wrong-thread async
  handler call rejection before V8 microtasks run, and wrong-thread destroy deferral to the
  owner thread.
- `execution.handwritten_artifact` is registered only when V8 is enabled and covers parsing
  the handwritten plan fixture, evaluating bootstrap runtime and handwritten/compiler
  `app.js`, validating registered handlers, invoking handler ID `1`, missing plan handler
  ID diagnostics, missing registration diagnostics, and thrown handler diagnostics.
- `http.dispatch.execution` is registered only when V8 is enabled and covers synthetic
  in-memory GET dispatch from parsed HTTP request head through a manual route binding to a
  numeric handler ID, plus missing JavaScript function and throwing handler diagnostics.
- ENGINE-19.BC aliases the V8-gated runtime evidence as
  `conformance.v8.runtime_bridge`, `conformance.v8.owner_thread`,
  `conformance.v8.native_async_scheduler`, and
  `conformance.v8.http_dispatch_execution` so CTest reports show the evidence lane
  directly. These aliases run the same validated executables above and do not make default
  gates V8 evidence.
- `conformance.async_handler.run_once` is registered only when V8 is enabled and proves the
  compiler-emitted direct async handler artifact settles through `sloppy run --artifacts
  --once`.
- `conformance.sqlite.bridge` and `sloppy.run.once_sqlite_bridge` are V8-gated and prove a
  checked-in SQLite artifact can resolve `data.sqlite("main")`, write/query rows, return
  JSON, and close the resource. `conformance.sqlite.denied_capability` and
  `sloppy.run.once_sqlite_denied_capability` prove denied SQLite reads surface as 500
  responses without implementing a broader policy engine in this slice.
- `bootstrap.stdlib.assets` runs in the default CTest suite and verifies the bootstrap
  stdlib source files and copied build-tree assets exist. `bootstrap.stdlib.api_shape`
  statically checks the bootstrap JavaScript API shape. When `node` is available,
  `bootstrap.stdlib.app_host_foundation` executes the ESM stdlib to cover the JavaScript
  app-host skeleton. These checks do not prove V8 ESM module loading.
- `examples.hello.api_shape` runs in the default CTest suite and statically checks the
  first hello example. It also does not execute JavaScript or load ESM modules.

Later checks:

- true V8 ESM bootstrap loading.

EPIC-20's default benchmark harness does not require V8. Handler dispatch benchmarks cover
plan lookup and the current noop engine boundary by default. Any V8 handler-call benchmark
must be explicitly gated by `SLOPPY_ENABLE_V8`, an approved `SLOPPY_V8_ROOT`, and a
Release-compatible SDK, and the runtime `--include-v8` flag before its numbers can be
reported. V8-required benchmark entries are filtered or skipped unless `--include-v8` is
provided.

## Source Docs

- `docs/architecture.md`;
- `docs/execution-model.md`;
- `docs/concurrency.md`;
- `docs/testing-strategy.md`;
- ADR 0003;
- ADR 0014.

## Open Questions

- Exact prebuilt V8 SDK hosting and checksum format.
- Exact dynamic runtime DLL/shared-library list for non-monolithic release builds.
## ENGINE-14 Boundary

ENGINE-14 keeps module loading outside the V8 bridge. The compiler accepts a supported
source-level import subset and rewrites/bundles it into the existing classic artifact. The
V8 bridge still loads the bootstrap classic runtime asset, validates registered handler
IDs, and evaluates generated `app.js`; it does not own a native ESM module graph.
