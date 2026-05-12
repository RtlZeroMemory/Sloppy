# Memory model, ownership, and safety

Sloppy's runtime uses explicit ownership rules, bounded native data structures,
and narrow JS/native transfer points. This page explains what owns each major
piece of memory, how long it lives, and what contributors must preserve.

This is a practical engineering model, not a formal proof. Sloppy contains a C
runtime, a Rust compiler, a C++ V8 bridge, JavaScript stdlib code, platform
backends, and third-party native libraries. The C and C++ parts still need
discipline, tests, sanitizers, fuzzing, and review.

## Short version

- The Rust compiler owns source analysis and emits artifacts.
- The Plan is loaded into app-lifetime native memory and treated as read-only
  after startup validation.
- Route tables and app metadata live for the app lifetime.
- Each request gets request-lifetime memory and cleanup scope.
- Pointers into request memory must not escape the request.
- V8 owns the JS heap. Native code must not store raw `v8::Local` handles in
  long-lived C structs.
- Data crossing the JS/native boundary is copied when lifetimes differ.
- JS-visible native resources use opaque slot/generation handles, not native
  pointers.
- JS-visible FFI resources use opaque bridge objects; `.ptr` is a passable
  resource alias, not a numeric native address.
- Async completions settle JS-visible state at most once on the V8 owner thread.
- HTTP/2 stream state must be independent inside a shared connection/session.

## Layers

```text
+-------------------+      artifacts       +-------------------+
| Rust sloppyc      | -------------------> | app.plan.json     |
| parser/extractor  |                      | app.js            |
+-------------------+                      | app.js.map        |
                                           +---------+---------+
                                                     |
                                                     v
+-------------------+      loads/validates +-------------------+
| C runtime         | -------------------> | Plan arena        |
| app host / HTTP   |                      | route table       |
| logging / bridges |                      | capabilities     |
+---------+---------+                      +---------+---------+
          |
          v
+-------------------+        bridge        +-------------------+
| V8 isolate        | <------------------> | Platform/native   |
| JS heap / stdlib  |                      | sockets/files/proc|
| handlers/programs |                      | provider handles  |
+-------------------+                      +-------------------+
```

| Layer | Owns | Lifetime |
| --- | --- | --- |
| `sloppyc` compiler | Parser data, compiler graph, emitted artifacts | Build command |
| Plan parser/runtime | Validated `SlPlan`, strings, route/provider/capability metadata | App run |
| App host | Feature activation, app scope, route table, logging runtime, engine handle | App run |
| HTTP transport | Connection state, parser buffers, protocol state machines | Connection/session |
| Request dispatch | Request context, request arena, request scope cleanups | One request/stream |
| V8 bridge | Isolate, context, persistent bridge state, JS heap references | Engine lifetime |
| Resource table | Opaque native handles visible to JS | Resource table lifetime, per handle until close |
| FFI registry/resources | Cached library/symbol/call descriptors and owned ref/buffer/struct storage | Engine lifetime; individual resources until `dispose()` |
| Async backend | Completion records, readiness watches, cleanup hooks | Loop/resource lifetime |
| Logging runtime | Fixed event records, bounded queue, sinks | App run |
| Platform layer | OS/libuv/OpenSSL/driver objects behind Sloppy APIs | Owning resource lifetime |

## Ownership rules

The runtime uses a few common ownership shapes:

| Shape | Storage | Valid until | Typical use |
| --- | --- | --- | --- |
| `SlStr`, `SlBytes` | Borrowed view | Owner-defined lifetime | Parser inputs, temporary views |
| Arena copy | `SlArena` | Arena reset/dispose | Plan data, diagnostics, request data |
| Builder output | Arena-backed growable buffer | Owning arena lifetime | JSON, diagnostics, responses |
| Scope cleanup | `SlScope` registration | Scope close | Request/app/resource cleanup |
| Resource ID | Resource table slot/generation | Close/dispose or generation mismatch | JS-visible file/socket/process/provider handles |
| V8 local handle | V8 handle scope | Current handle scope | Temporary JS values |
| V8 persistent/global handle | V8 bridge-owned storage | Bridge-defined reset/shutdown | Long-lived bridge functions/keys |
| Platform handle | Platform/resource object | Close/dispose callback | Files, sockets, processes, TLS state |

General rules:

- A borrowed view must not outlive its documented owner.
- Arena memory is scoped memory, not independently closable resource ownership.
- Any data crossing an async boundary must point into memory that outlives that
  operation, or it must be copied into operation-owned storage.
- Cleanup callbacks run once. Request/app scopes use LIFO cleanup ordering.
- JavaScript never receives raw native pointers.
- External inputs are validated before they become trusted runtime state.
- Size arithmetic and buffer writes should use checked helpers and bounded
  builders.

Compact native structs are allowed only when they preserve the same ownership model. A
smaller type must not make borrowed data look owned, hide which payload is active, or let a
pointer escape a shorter lifetime.

The normal tools are field reordering, tagged unions, private flag masks, and
`_Alignof`-based typed allocation. These keep the memory model readable: a contributor can
still see which object owns the bytes, how long the view lives, and which member is active.

Packed runtime structs, NaN-boxed values, and tagged native pointers are not part of the
current memory model. They are not banned because performance does not matter; they are
deferred because they change how debugging, sanitizers, and portability work. Future work
must design, measure, and document those tradeoffs before using them.

## Plan and artifact lifetime

The compiler writes the Plan, bundle, source map, and optional dependency graph
before runtime execution. `sloppy run` then loads and validates the artifacts
before entering V8.

```text
source files
   |
   v
sloppyc build
   |
   +--> app.plan.json
   +--> app.js
   +--> app.js.map
   +--> deps.graph.json when dependency metadata exists
          |
          v
runtime startup
   |
   +--> parse JSON into Plan arena
   +--> validate schema, hashes, target, routes, handlers, features
   +--> build app-lifetime route table
   +--> initialize engine bridge
```

Plan data is app-lifetime data. After startup validation:

- the Plan is read-only runtime input;
- route table entries reference Plan data or app-owned copies;
- dependency graph metadata is read-only metadata for CLI/runtime decisions;
- metadata commands can read the Plan without entering V8;
- request code must not mutate Plan-owned strings, arrays, or route metadata.

If a request, async operation, or bridge call needs to hold Plan-derived data
past an immediate call, it either references app-lifetime Plan memory or copies
the value into a longer-lived owner.

## Request lifetime

For HTTP/1.1, a connection processes one request at a time. For HTTP/2, each
stream maps to an independent request lifecycle after the HTTP/2 dispatcher has
assembled validated headers and DATA bytes.

```text
socket bytes / HTTP2 stream events
   |
   v
parse request head and body
   |
   v
request arena + request scope
   |
   +--> method, target, headers, query, body views/copies
   +--> route match and typed binding metadata
   +--> request services/provider operation cleanup
   |
   v
V8 context materialization
   |
   v
handler returns Results descriptor
   |
   v
copy response body/headers into native-owned response storage
   |
   v
write response
   |
   v
close request scope, release request memory
```

Request-specific memory dies after the request. Contributors must not:

- store a pointer into the request arena on an app-lifetime object;
- pass request-owned views to off-thread work unless the operation owns a safe
  copy or retains a scope designed for that lifetime;
- let a JS value outlive the request unless the bridge policy gives it a safe
  owner;
- allow cleanup to run twice on timeout, cancellation, handler failure, and
  normal response completion races.

JS values that outlive request dispatch must be copied or represented by an
owned resource handle. A borrowed request body view is not a durable cache.

## V8 bridge lifetime

The V8 bridge owns the isolate, context, bridge private keys, persistent
functions, and JS heap participation. C code outside `src/engine/v8/` sees only
engine-neutral Sloppy types.

```text
engine init
   |
   +--> create V8 isolate/context on owner thread
   +--> install Sloppy intrinsics
   +--> evaluate app.js
   +--> register handlers/program entrypoint
   |
   v
dispatch/program call
   |
   +--> open handle scope
   +--> create JS context objects from native data
   +--> call handler/main
   +--> bounded microtask drain
   +--> validate result shape
   +--> copy result out to native memory
   |
   v
engine shutdown
   |
   +--> reset persistent bridge handles
   +--> dispose isolate-owned state
```

V8 invariants:

- one owner thread per isolate;
- wrong-thread entry fails before touching V8;
- temporary `v8::Local` handles stay inside handle scopes;
- long-lived V8 references require bridge-owned persistent/global handles and a
  documented reset path;
- C++ exceptions are caught at the bridge and mapped to diagnostics;
- Promise settlement is bounded and owner-thread only.

V8's heap and Sloppy's native arenas are different memory systems. Native code
must not assume V8 GC can manage native allocations, and JS code must not see
native addresses.

## JS/native data transfer

The bridge copies when ownership or lifetime changes.

```text
native -> JS

native bytes/string
   |
   v
validate and normalize
   |
   v
create V8 value or ArrayBuffer with copied contents
   |
   v
JS handler/program
```

```text
JS -> native

JS result object
   |
   v
validate descriptor shape
   |
   v
copy status, headers, body into native-owned memory
   |
   v
write HTTP response or program event output
```

Rules:

- Validate JS object shape before trusting fields.
- Copy strings and bytes out of V8 before the handle scope ends.
- Copy native strings and bytes into V8-owned values before JS reads them.
- Do not store raw V8 handles in long-lived C structs.
- Do not expose zero-copy shared buffers across the bridge unless a future
  policy defines exact ownership, pinning, and cleanup rules.

## Native resources and handles

Native resources surfaced to JavaScript use a resource table. JS holds an
opaque ID; native code owns the actual resource pointer and cleanup callback.

```text
JS FileHandle { slot: 42, generation: 7 }
          |
          v
Native resource table
  slot 42:
    generation 7
    kind file
    state open
    owner runtime

After close:

Native resource table
  slot 42:
    generation 8
    state empty

Old JS handle { slot: 42, generation: 7 } is rejected.
```

The table validates slot, generation, kind, and liveness on every lookup.
Closing a handle advances the generation so stale handles cannot become valid
when a slot is reused. Diagnostics expose IDs and kinds, not native pointer
values.

This pattern is used for filesystem handles, watchers, TCP connections,
listeners, local IPC resources, processes, provider connections, background
services, queues, worker pools, and worker isolates where those resources are
bridged to JS.

## Async operations

Native work can outlive the JS call stack that submitted it. The async backend
therefore owns completion records and cleanup paths explicitly.

```text
JS starts async operation
   |
   v
bridge validates args and creates operation record
   |
   v
native/platform work owns stable buffers and resource refs
   |
   v
completion posted to owner-thread async loop
   |
   +--> if request/operation is still live:
   |      resolve/reject Promise on V8 owner thread
   |
   +--> if request/operation is terminal:
          run late-completion cleanup only
```

The invariant is single settlement. Cancellation, timeout, request cleanup, and
driver completion may race, but only one path may update JS-visible state. Late
completions free native resources and release retained scopes; they do not
resurrect request state or settle a Promise again.

Queued completion payloads must point to owned/stable native memory. They must
not point to borrowed request views that can disappear before owner-thread
dispatch.

## HTTP/1.1 and HTTP/2 session memory

HTTP/1.1 keeps connection parser state and response buffers on the connection.
Requests are sequential on a keep-alive connection. Request memory is released
after each request before the connection waits for the next one.

HTTP/2 has a shared connection/session plus independent stream lifetimes:

```text
HTTP/2 connection/session arena
   |
   +--> SETTINGS / HPACK / flow-control state
   +--> outbound frame buffer
   +--> event batch storage
   |
   +--> stream 1 state
   |      headers, DATA buffers, request lifecycle
   |
   +--> stream 3 state
   |      headers, DATA buffers, request lifecycle
   |
   +--> stream 5 state
          headers, DATA buffers, request lifecycle
```

Important rules:

- Stream reset must not corrupt sibling stream state.
- Per-stream request cleanup must release only that stream's request resources.
- GOAWAY retires the session and prevents new work beyond the accepted stream
  boundary.
- Flow-control and HPACK state belong to the session; request contexts must not
  keep pointers into cleared HTTP/2 event batches.
- DATA frame buffers used by handlers must be copied or owned by the request
  lifecycle, not borrowed from an event list that may be cleared.

HTTP/2 event lists are borrowed session-owned views. They are valid only until
the session clears events, is disposed, or its owning arena is reset.

## Logging memory

The logging runtime is native app-lifetime infrastructure. It receives events
from JS and native request contexts, applies redaction, and sends bounded event
records to sinks.

```text
JS ctx.log / native logger
   |
   v
validate shallow scalar fields
   |
   v
copy into fixed-size SlLogEvent
   |
   v
apply redaction
   |
   v
bounded runtime queue
   |
   v
dispatcher thread
   |
   +--> memory sink
   +--> console sink
   +--> JSONL file sink
```

Events are copied before queue admission. The request path does not hand sink
threads borrowed JS objects or request-arena field objects. Sinks own their
internal buffers and flush/close during runtime shutdown. Disabled log levels
return before expensive field conversion and queue work.

## Program Mode lifecycle

Program Mode uses the same artifact and V8 boundary as web apps, but it runs a
route-free entrypoint instead of building a web route table.

```text
load app.plan.json
   |
   v
validate kind: "program" and artifacts
   |
   v
load app.js into V8
   |
   v
stage args/context
   |
   v
install temporary Sloppy console
   |
   v
run top-level code, named main, or default function
   |
   v
collect stdout/stderr events and exit code
   |
   v
restore console
   |
   v
cleanup engine/program resources
```

Program Mode is V8-gated for execution. Current console output is collected
while the entrypoint runs and written after completion; it is not a streaming
terminal interface. Program stdlib imports still use the same runtime feature
metadata and bridge/resource rules as web apps.

## Safety guarantees

Sloppy is written with explicit ownership rules and guardrails:

- Plan data is parsed and validated before runtime dispatch.
- Runtime feature requirements are checked against Plan metadata before
  execution.
- Route tables and app metadata are app-lifetime and treated as read-only after
  startup.
- Request-specific native memory is scoped to the request lifecycle.
- Native code should not store borrowed request memory beyond request lifetime.
- Public JS/native bridge entrypoints validate shapes before use.
- JS/native data is copied when lifetimes differ.
- Native resources exposed to JS use opaque IDs and generation checks where
  implemented.
- Arenas make app/request/session allocation boundaries explicit.
- Checked arithmetic and bounded builders are used for size-sensitive paths.
- The V8 bridge is isolated behind a named boundary.
- Targeted tests, sanitizer lanes, fuzz seeds, stress checks, and golden tests
  help catch regressions as those lanes are added and expanded.

These are engineering guarantees about intended runtime behavior and review
rules. They are not a claim that leaks, use-after-free bugs, races, or
native-library defects are impossible.

## Non-guarantees

Sloppy does not currently promise:

- stable public alpha, pre-production internals or artifact formats;
- Rust-style ownership checking for the C/C++ runtime;
- formal verification;
- absence of leaks, use-after-free bugs, data races, or logic bugs;
- that third-party native libraries share Sloppy's ownership model;
- that V8 heap behavior is the same as Sloppy native arena behavior;
- sandboxing of arbitrary code in this alpha;
- an OS sandbox from capability metadata unless a specific enforcement surface
  implements it;
- safe execution of untrusted Program Mode code.

Program Mode process, filesystem, and network APIs are powerful. Treat apps
that use them as trusted code unless they run under an external OS/container
policy that provides isolation.

## Contributor checklist

Before merging native/runtime code, check:

- What owns this memory?
- What lifetime does it have?
- Can it outlive the request, stream, operation, or scope?
- Is every size calculation checked?
- Is every external input validated?
- Are JS values copied before leaving V8 scope?
- Are native resources closed on all error paths?
- Is cancellation/shutdown safe?
- Can stale handles be reused?
- Do sibling HTTP/2 streams remain independent?
- Does ASAN, fuzz, stress, or targeted unit coverage exercise this path?

## See also

- [Architecture](architecture.md)
- [Runtime](runtime.md)
- [V8 bridge](v8-bridge.md)
- [Async runtime](async-runtime.md)
- [HTTP runtime](http-runtime.md)
- [Logging runtime](logging.md)
- [Platform boundaries](platform-boundaries.md)
- [Security model](security-model.md)
