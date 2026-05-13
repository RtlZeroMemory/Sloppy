# Internals

Implementation-level documentation for contributors working on the Sloppy
codebase itself. If you're using Sloppy to build an application, you want
[api/](../api/index.md) and [guide/](../guide/index.md) instead.

- [Architecture](architecture.md) — top-level layering: compiler, app host, runtime, bridge
- [Runtime](runtime.md) — startup, dispatch, shutdown
- [Compiler](compiler.md) — `sloppyc` internals
- [Compiler standards](compiler-standards.md) — Rust compiler ownership, extraction, diagnostics, and artifact rules
- [Artifact module loader](module-loader.md) — generated ESM/CommonJS/dependency loader
- [Plan](plan.md) — Plan parsing, validation, schema evolution
- [Logging runtime](logging.md) — structured events, redaction, queues, sinks
- [Runtime diagnostics](diagnostics-runtime.md) — diagnostic reports, breadcrumbs, local crash reports
- [JavaScript stdlib standards](javascript-stdlib-standards.md) — module boundaries, validation, redaction, lifecycle, runtime-classic drift
- [V8 bridge](v8-bridge.md) — boundaries, ownership rules, isolation
- [HTTP runtime](http-runtime.md) — parser, transport, dispatch
- [Native endpoint dispatch](native-endpoint-dispatch.md) — Plan-backed dispatch table metadata
- [Native runtime standards](native-runtime-standards.md) — C/C++ boundaries, safety, lifetime, and testing rules
- [Static assets runtime](static-assets-runtime.md) — static file TestHost, compiler, and package boundaries
- [TestHost](testhost.md) — app-host, artifact, and loopback test harness boundaries
- [WebSocket runtime](websocket-runtime.md) — app-host simulation boundaries and native Upgrade runtime internals
- [TestServices](testservices.md) — experimental Docker-backed dependency test lifecycle
- [Async runtime](async-runtime.md) — owner-thread model, cancellation
- [Provider runtime](provider-runtime.md) — provider executor and bridges
- [Scheduler runtime](scheduler.md) — durable jobs, worker leases, recurring ticks, and scheduler storage
- [Memory model](memory-model.md) — arenas, lifetimes, ownership
- [Platform boundaries](platform-boundaries.md) — what crosses `src/platform/`
- [Security model](security-model.md) — capability checks, redaction, the V8 bridge as a boundary
