# V8 bridge

The bridge is the only place V8 lives. Everything outside
`src/engine/v8/` sees engine-neutral C types — no `v8::Local`, no
`v8::Isolate`, no V8 headers. Replacing the bridge with a different
engine wouldn't touch the rest of the kernel.

## Layout

```text
include/sloppy/engine.h         engine-neutral C ABI
src/engine/engine.c             dispatcher: noop or V8
src/engine/engine_internal.h
src/engine/v8/                  C++ V8 implementation (only place V8 enters)
  engine_v8.cc                  isolate/context, evaluate, dispatch
  intrinsics_*.cc               Sloppy-owned intrinsics for the JS bridge
  source_map.cc                 stack remapping
  ...
stdlib/sloppy/                  JS surface that the bridge wires up
  internal/runtime-classic.js   bootstrap glue
```

## What crosses the boundary

The bridge exposes a small set of operations:

| Operation             | Purpose                                                  |
| --------------------- | -------------------------------------------------------- |
| `engine_init`         | Build isolate + context, load bootstrap stdlib           |
| `engine_evaluate`     | Run the compiled `app.js` once                           |
| `engine_register`     | Receive a handler ID + JS function from the bundle       |
| `engine_dispatch`     | Call a handler ID with a request context                 |
| `engine_shutdown`     | Tear down isolate, drain any final cleanup               |

All inputs and outputs are Sloppy-owned: status codes (`SlStatus`),
diagnostic structures (`SlDiag`), byte slices (`SlBytes`), and parsed
plan/route metadata. Anything that started life as a `v8::Local<…>`
ends up materialized as Sloppy memory before the function returns.

Request log calls cross the bridge as bounded structured events. The bridge
passes scalar field values into `SlLogEventBuilder`, rejects unsupported field
values, and never exposes the native logging runtime pointer to JavaScript.
Disabled levels return before message conversion, field enumeration, event
construction, or queue submission.

## Invariants

These hold at the C ABI boundary. They are enforced by code review,
boundary scanners, and the way the public headers are declared.

1. **No V8 types in public headers.** `include/sloppy/engine.h` exposes
   only opaque handles (`SlEngine*`) and Sloppy types.
2. **One owner thread per isolate.** Every entry point asserts the
   calling thread matches the isolate's owner. Wrong-thread access fails
   before V8 sees it.
3. **JavaScript never gets a native pointer.** Resources surfaced to JS
   are `External` handles wrapped in capability checks; the JS side
   sees opaque handles, not addresses.
4. **Buffers are copied across.** Native bytes that JS will read are
   copied into V8-owned storage; JS values that the bridge returns to C
   are copied out before V8 handles go out of scope.
5. **Promise drain is bounded and owner-thread.** `engine_dispatch`
   drains microtasks until either the returned Promise settles or the
   bound is hit. Pending or rejected Promises map to a deterministic
   failure diagnostic — no fake success.
6. **C++ exceptions stop at the bridge.** Anything thrown inside
   `src/engine/v8/` is caught and mapped to an `SlDiag` before
   returning to C.
7. **Source maps remap exception traces.** Thrown JS errors get their
   stack remapped through the Plan-recorded source map before showing
   in diagnostics.
8. **Logging stays bounded.** `ctx.log` methods support shallow scalar
   fields only, attach request ID and route metadata from the native
   request context, and submit through the Sloppy-owned logging runtime.
9. **FFI resources stay opaque.** `sloppy/ffi` refs, buffers, string buffers,
   and struct instances are stored behind V8 private `External` values. JS gets
   resource objects and `.ptr` aliases, never raw native addresses.

## Startup snapshots

`SLOPPY_V8_SNAPSHOT_DIR` enables an opt-in startup snapshot cache for
Plan-backed V8 contexts. The blob is keyed by V8 version, an internal snapshot
format string, and the runtime feature mask. Native callbacks reachable from
snapshotted Sloppy intrinsics are recorded in one external reference table that
is shared by snapshot creation and isolate creation. `Isolate::SetData(0,
backend)` is reset after isolate creation, so callbacks resolve the current
engine rather than snapshot-time state.

Runtime state is not snapshotted. Handler maps stay C++-owned and are rebuilt
from app evaluation through `__sloppy_register_handler`. Native resource tables,
async queues, provider executors, filesystem policy, capability registries, and
logging sinks are current-engine state; callback functions can be snapshot
resident, but those runtime objects are reinitialized for each engine.

## Owner-thread model

V8 isolates are single-threaded. Sloppy follows that constraint
strictly:

- The bridge records the OS thread ID that created the isolate.
- Every `engine_dispatch` call asserts that ID against the current
  thread.
- Worker pools (`WorkerPool`) get their own isolates, each owned by a
  worker thread.
- Native callbacks that originate off the owner thread queue their
  result through the platform async backend (`src/core/async_backend.c`)
  and the owner thread picks it up — they don't enter the isolate
  directly.

## Noop engine

When V8 is not built in, `src/engine/engine.c` selects the noop
implementation. It implements every operation by returning an
"unsupported" diagnostic. This lets the metadata commands
(`build`, `routes`, `capabilities`, `audit`, `openapi`, `doctor`) work
without V8.

`sloppy run` with the noop engine fails at `engine_init` with a clear
message, which is what the user sees as "handler execution requires a
V8-enabled build".

## Result conversion

A handler usually returns a result descriptor (`Results.*`). The bridge:

1. Validates the descriptor shape (kind, status, headers, body).
2. Copies any body bytes / strings into Sloppy-owned storage.
3. Releases V8 handles for that response.
4. Returns the descriptor through the C ABI.

Plain JavaScript objects with no Sloppy result-descriptor fields are converted
to `200 application/json` responses by JSON stringifying the object and copying
the bytes into the result arena. Objects that contain Sloppy descriptor-shaped
fields such as `__sloppyResult`, `kind`, `status`, `body`, or `bodyResult`,
including inherited fields, are not treated as plain JSON; they must be valid
descriptors or they fail closed.

If the descriptor is malformed (missing `kind`, bad status code, body
type that doesn't match `kind`), the bridge returns a diagnostic and the
HTTP layer responds 500 with a redacted body.

## FFI Intrinsics

`src/engine/v8/intrinsics_ffi.cc` installs `__sloppy.ffi` only when
`stdlib.ffi` is active. The generated/stdlib JS layer calls it to bind
Plan-visible libraries and to allocate FFI resources. Function wrappers point
at cached native descriptors from `SlFfiRegistry`; symbols and type descriptors
are not resolved on every call.

The bridge validates argument counts and value ranges, marshals call-duration
strings/bytes, calls libffi on the owner thread, and converts supported return
types. It does not expose a raw pointer-call API or callbacks into JS.

## Tests

- **V8 smoke** under `tests/conformance/v8/` exercises bundle eval,
  handler registration, dispatch, exception mapping, Promise drain
  bounds.
- **Bridge unit tests** under `tests/unit/engine/` cover individual
  intrinsic surfaces.
- **Noop engine tests** cover the unsupported-diagnostic path so
  metadata commands stay green without V8.
- **Source-map tests** verify exception remapping against goldens.

## Where the bridge stops

The bridge does not:

- Resolve files or `node_modules`. The compiler emits one bundle with a
  sealed module loader; V8 evaluates that bundle as a unit.
- Talk to the network or filesystem. Capability-checked native
  intrinsics expose those.
- Run outside the owner thread.
- Persist V8 state across `engine_shutdown` and `engine_init`.
- Own logging sink I/O. The bridge only converts JS log calls into native
  structured events; sinks and flushing belong to `src/core/logging.c`.

For the public-facing model see [about/v8-bridge.md](../about/v8-bridge.md).
For the broader lifetime model across app, request, async, and native resource
ownership, see [Memory model, ownership, and safety](memory-model.md).
