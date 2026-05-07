# Execution Model

Sloppy executes compiler-known application artifacts through a native app-host. The runtime
validates the Sloppy Plan, activates required features, stages bootstrap assets, initializes
the engine, and dispatches requests through documented native/JavaScript boundaries.

## Current Executable Path

The supported path is:

```text
source app -> sloppyc build -> app.plan.json/app.js/app.js.map -> sloppy run artifacts
```

`sloppy run <source.js>` is a development shortcut over the same artifact path. It invokes
`sloppyc build`, writes artifacts, validates them, and runs the artifact runtime. Runtime
execution requires a V8-enabled build.

Default non-V8 gates prove compiler, Plan, CTest, scanner, and native default behavior.
They do not prove V8 execution.

## Startup Sequence

1. Parse CLI/config input.
2. Resolve source-input or artifact mode.
3. Compile source input when requested.
4. Read and validate `app.plan.json`.
5. Validate artifact paths, hashes, runtime compatibility, route/provider/capability
   metadata, and required features.
6. Stage the bootstrap stdlib/runtime assets.
7. Initialize the native app host and V8 engine when the lane requires it.
8. Evaluate generated artifacts.
9. Register handlers through Sloppy-owned V8 intrinsics.
10. Dispatch bounded requests through native route/runtime boundaries.

The runtime must fail closed on malformed plans, missing artifacts, hash mismatches,
unsupported features, missing V8 support, invalid handler registration, and bridge
activation gaps.

## Engine Boundary

The noop engine is always available and returns unsupported diagnostics for executable
handler calls. V8 execution is available only in V8-enabled builds. V8 types stay inside
`src/engine/v8/*`; public C headers expose Sloppy-owned ABI types only.

Executable generated handlers use registered handler dispatch. The legacy numeric
`sl_engine_call_handler` ABI remains unsupported for the noop engine and must not be
reported as a JavaScript execution path.

Direct async handler support is bounded: returned Promises must settle during the
owner-thread microtask drain. Broader event-loop behavior, fetch/timers/process/Node
compatibility, and arbitrary pending native async remain out of scope.

## HTTP Dispatch

The runtime builds a native route table from validated Plan metadata. Supported requests are
parsed through bounded HTTP parser/body-reader logic, matched to route metadata, and
converted into the current request context shape before V8 handler dispatch.

Current transport support includes bounded HTTP/1.1, configured request/connection limits,
request timeout/disconnect/shutdown terminal paths, bounded sequential keep-alive, scoped
chunked handling, opt-in inbound TLS wrapping, supported result descriptor conversion, and
safe framework error responses for invalid results or handler failures.

The current path does not claim production TLS hardening, HTTP/2, HTTP/3, WebSockets,
public streaming APIs, middleware, production graceful drain, reverse proxy behavior, or
benchmark performance.

## Request Context

The current handler context may include:

- `route` values from matched route parameters;
- decoded scalar query values with last-wins repeated-key behavior;
- `request.method`, `request.path`, and `request.rawTarget`;
- bounded request headers and body helpers where the active lane supports them;
- future `signal`, `deadline`, and request-owned resource shapes as source docs promote
  them.

Route params and query values are strings unless a future source doc promotes typed binding.
Unsupported body framing, oversized bodies, malformed JSON, unsupported content types, and
invalid route metadata fail before handler execution.

## Providers And Capabilities

Provider metadata and capability metadata are validated from the Plan before runtime work.
SQLite bridge calls check database capability metadata in scoped V8 lanes. PostgreSQL,
SQL Server, filesystem, and network JavaScript bridges remain separate work and must not be
claimed from metadata-only evidence.

Capability checks are runtime policy checks, not an OS sandbox.

## Source Maps And Diagnostics

Compiler output includes deterministic source metadata and source maps for supported
fixtures. Diagnostics must preserve stable codes, source context where available, redaction,
and actionable hints. Missing source-map remapping or unsupported source syntax should fail
honestly instead of being hidden by artifact success.

## Shutdown And Cleanup

Shutdown stops accepting new work, rejects new request admission, cancels or times out
active request lifecycles where configured, closes active transport connections, and drains
cleanup callbacks. This is immediate-cancel/drain-lite behavior, not production graceful
drain.

Request/app/resource cleanup must be once-only. Late native completions may only perform
cleanup and must not double-settle JavaScript-visible state.

## Non-Goals

- Node/Bun/Deno/npm compatibility.
- Production-edge HTTP behavior.
- Public alpha readiness.
- Package/release readiness.
- Performance claims from smoke, list, or default evidence.
- Hidden native pointers in JavaScript.
- V8 or OS details leaking outside their documented boundaries.

## Source Docs

- `docs/app-plan.md`
- `docs/compiler.md`
- `docs/compiler-supported-syntax.md`
- `docs/concurrency.md`
- `docs/security-permissions.md`
- `docs/diagnostics.md`
- `docs/project/http-transport-runtime-architecture.md`
- `docs/project/provider-execution-runtime-architecture.md`
