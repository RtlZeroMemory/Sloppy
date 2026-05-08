# V8 Engine Module

## Purpose

The V8 engine module implements Sloppy's optional JavaScript execution backend behind an
engine-neutral C ABI.

## Current Status

When the V8 SDK is resolved and V8 is enabled, the bridge supports classic generated
scripts, registered handler execution, request-context materialization, `Results.*`
descriptor conversion, bounded direct Promise settlement, exception diagnostics,
source-map primary-span remapping, runtime feature-gated intrinsics, and the current
SQLite, PostgreSQL, and SQL Server provider bridges. SQLite provider work is serialized
through the provider executor; PostgreSQL uses nonblocking libpq readiness watches; SQL
Server advances asynchronous ODBC connection/statement state through V8 continuations only
when the driver supports it.

Default non-V8 gates do not prove this behavior.

## Bridge Performance Shape

The HTTP bridge keeps fixed request/result property names, private keys, and request
facade helper functions in per-engine V8 handles tied to the owning isolate. These caches
must be reset before isolate disposal and must never be shared across isolates.

Request, body, header, and signal helper methods are installed through frozen
bridge-owned prototypes instead of being rebuilt as own functions for every request.
Cached helper functions are prototype-less so JS cannot mutate a bridge-created helper
prototype as a side channel. JS code must rely on the documented helper behavior, not
own-property placement or function identity.

Request headers are stored in an internal byte snapshot and materialized lazily:
`headers.get(name)` performs a case-insensitive lookup without building the full
`entries()` array, while `headers.entries()` materializes the current deterministic entry
shape only when requested. Header snapshots contain copied bytes, not native pointers.

Request body bytes are copied into V8-owned `ArrayBuffer` storage when the body exists.
Body text is created lazily and cached on the facade the first time `text()` or `json()`
needs it. Empty request bodies do not allocate an eager empty body buffer. Borrowed native
request views are not exposed to JS or retained across async boundaries.

The request-context scheme is native metadata, not a parser or socket handle. Cleartext
HTTP contexts expose `http`; HTTPS contexts expose `https` and `connection.secure === true`
after the transport completes the TLS handshake. The bridge does not expose OpenSSL,
socket, libuv, or TLS handles to JavaScript.

Native async continuations are still posted back through the Sloppy async loop and settle
Promises on the V8 owner thread. Continuation state is synchronized so concurrent native
post attempts settle at most once.

V8 Fast API Calls are available in the currently resolved SDK, but they are experimental
and only fit simple callbacks that do not allocate, enter JS, trigger GC, or use V8 APIs.
Current measured bridge cost is dominated by request/result materialization rather than a
single simple scalar intrinsic, so Fast API migration is deferred until a real hot path
matches those constraints.

## Invariants

- V8 types stay under `src/engine/v8/`.
- Public headers and core modules expose no V8 handles or values.
- One owner thread enters an isolate/context.
- Wrong-thread entry fails before touching V8 state.
- JS never receives raw native pointers.
- Returned strings/results are copied out before crossing the C ABI.
- Pending or rejected Promises fail deterministically; they are not serialized as success.
- Lazy bridge facades may cache V8 values, but they must not retain borrowed native
  request, arena, platform, or resource pointers.
- Native worker threads may post completion data, but Promise settlement remains on the V8
  owner thread.

## Non-Claims

The current bridge is not a Node runtime, npm package loader, general ESM module cache,
browser-compatible JavaScript environment, production async runtime, ORM, migration layer,
or provider readiness claim without the named provider evidence lane.

## Tests

V8 tests are a separate evidence lane. V8-gated evidence must name the resolver, preset,
build, and test command used.

The internal bridge benchmark smoke lane lives in `sloppy_bench --include-v8`. Smoke mode
proves benchmark reachability only; measured bridge reports belong in PR evidence and must
not be converted into public performance claims.
