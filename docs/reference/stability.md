# Stability Reference

Sloppy is pre-alpha. Contracts and behavior can change between revisions.

## Version Fields in Current Artifacts

Current emitted/compiled version markers:

- runtime CLI version string: `0.0.0-foundation`
- compiler version: `sloppyc-0.8.0`
- plan runtime minimum version: `0.1.0`
- plan stdlib version: `0.1.0`

## Plan Schema Stability

Current native parser contract is `schemaVersion: 1`.

Parser behavior:

- unsupported schema versions are rejected
- required fields and known field types are strict
- unknown extra fields are ignored

## Compatibility Checks Enforced at Run Time

`sloppy run` validates artifact compatibility metadata before handler execution:

- target platform/engine fields
- runtime minimum version
- route/handler consistency and related startup validation

## Provider and Runtime Stability

- Provider metadata supports `sqlite`, `postgres`, `sqlserver`.
- Compiler-generated static provider handles are sqlite-only.
- Framework v2 typed provider injection is generated for SQLite,
  PostgreSQL, and SQL Server; execution depends on the active bridge,
  provider config, and live service setup.
- Live provider checks are opt-in and environment-dependent.

## Feature Matrix

This matrix is the compact current-state map. `supported` means the surface is
covered by current source and tests for that lane. `experimental` means the
surface exists but is still pre-alpha and may change. `rejected` means the
compiler fails closed rather than emitting a partial Plan.

| Feature | App-host/test-host | Compiler source input | Generated `app.js` / Plan | Native/V8 `sloppy run` | Notes |
| --- | --- | --- | --- | --- | --- |
| Routing | supported | supported | supported | supported | Direct route registration supports `GET`, `POST`, `PUT`, `PATCH`, and `DELETE`; generated CORS preflight routes bind `OPTIONS` in Plan metadata. |
| Route params | supported | supported | supported | supported | `{name}`, `{name:str}`, and `{name:int}` values are strings in handler context. |
| Query | supported | supported | supported | supported | Last value wins for repeated keys. |
| Headers | supported | supported through typed/header metadata and request context | supported | supported | `--once` cannot supply custom headers. |
| Body helpers | supported | supported through body metadata and generated wrappers | supported | supported | JSON/text/bytes are bounded in memory. |
| Results helpers | supported | supported subset | supported | supported | `text`, `json`, `ok`, `created`, `accepted`, `noContent`, `notFound`, `badRequest`, `problem`, `status`, `html`, and `bytes` have compiler/runtime coverage where fixtures exercise them. |
| ProblemDetails | supported | supported as literal `ProblemDetails.defaults(...)` | supported | supported | Safe handler-error mapping. |
| Middleware | supported | supported static subset | supported | supported | Inline/top-level middleware is emitted into generated handlers; dynamic lookup and unsupported captures fail with `SLOPPYC_E_UNSUPPORTED_MIDDLEWARE`. |
| CORS | supported | supported static subset | supported | supported | Literal `app.useCors({...})` policies emit response wrapping, Plan metadata, and generated `OPTIONS` preflight routes; dynamic policies fail with `SLOPPYC_E_UNSUPPORTED_CORS`. |
| Health | supported | supported for literal static shapes | supported | supported | Compiler emits aggregate/live/ready handlers and health metadata. |
| Services | supported | supported for literal registrations and `Service<T>` | supported | supported through generated wrappers | App-host exposes `ctx.services`; native base context does not. |
| Config/defaults/secrets | supported | supported for config metadata and `Config<"KEY">` defaults | supported | supported | Secret values are not persisted in Plan metadata. |
| SQLite provider | supported | supported | supported | supported with V8/provider bridge | Strongest current provider path. |
| PostgreSQL provider | supported through runtime API and typed injection | supported metadata/generated typed injection | supported generated wrapper | live-provider gated | Requires config, active bridge, and live PostgreSQL service. |
| SQL Server provider | supported through runtime API and typed injection | supported metadata/generated typed injection | supported generated wrapper | live-provider gated | Requires config, ODBC driver, active bridge, and live SQL Server service. |
| Static provider handles | sqlite supported | sqlite supported | sqlite supported | sqlite supported | Static non-SQLite handles fail with `SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE`. |
| Typed bindings | test-host supported | supported | supported | supported | `Route`, `Query`, `Header`, `Body`, `RequestContext`, `Service`, `Config`, provider markers, and `WorkQueue`. |
| Compiler source input | n/a | supported subset | emits Plan, bundle, source map | supported with V8 for execution | Not a full TypeScript type checker or npm resolver. |
| OpenAPI | metadata consumer | Plan-derived | metadata-only | CLI only | Security schemes and full runtime-pipeline semantics are outside current output. |
| CLI `build` | n/a | supported | emits artifacts | no V8 required | Deterministic for the same source, config inputs, compiler, and CLI overrides. |
| Compiler timings | n/a | supported dev flag | timing JSON only | n/a | `sloppyc build --timings-json` is local contributor tooling for phase/counter evidence, not a product API or public performance claim. |
| CLI `run` | n/a | supported source/project handoff | validates artifacts | V8 required for handlers | `--once` creates a minimal synthetic request. |
| CLI `create` | n/a | n/a | copies templates | no V8 required | Built-in templates are packaged with local archives and npm platform packages. |
| CLI `package` | n/a | supported source/project handoff | emits app package directory | no V8 required | Creates a local app package under `.sloppy/package/` by default; not a runtime release archive. |
| CLI `routes` | n/a | Plan-derived | metadata-only | no V8 required | Reads route metadata from Plan. |
| CLI `capabilities` | n/a | Plan-derived | metadata-only | no V8 required | Shows declared capability/provider metadata. |
| CLI `audit` | n/a | Plan-derived | metadata-only | no V8 required | Reports static Plan issues and policy notes. |
| CLI `doctor` | n/a | Plan-derived | metadata-only | no V8 required | Reports artifact and feature readiness. |
| App test host | supported | rejected import | n/a | n/a | `Testing` remains a JS test helper and is not valid compiler input. |
| Logging | supported | supported config metadata | supported | supported | Native runtime owns console/file sinks; memory sink is deterministic app-host test surface. |
| Request IDs | supported middleware | supported static middleware | supported | supported | Compiler input supports static `RequestId.defaults(...)`; generator callbacks remain app-host test-only. Native `ctx.requestId` exists. |
| Request logging | supported middleware | supported static middleware | supported | supported | Compiler input supports static `RequestLogging.defaults(...)`; dynamic option values fail closed. Native `ctx.log` is direct logging. |
| HTTP server | n/a | Plan/server config metadata | supported | experimental dev server | HTTP/1.1 plus server HTTP/2 over TLS ALPN, h2c prior knowledge, and h2c Upgrade. Not a production edge. |
| Inbound TLS | n/a | config metadata | supported paths | experimental | Certificate/key paths are Plan-visible today; diagnostics redact TLS material. |
| HttpClient | supported API | supported from `sloppy/net` | Plan-visible as required feature | experimental bridge | HTTP/1.1 by default; explicit h2/h2c, pooled h2 multiplexing, and HTTPS `auto` ALPN h2 selection are supported. `https://` needs the private outbound TLS bridge. |
| Filesystem stdlib | supported | supported from `sloppy/fs` | `stdlib.fs` feature emitted | V8 bridge required | No Node `fs` compatibility. |
| Network stdlib (TCP, local IPC) | supported | supported from `sloppy/net` | `stdlib.net` feature emitted | V8 bridge required | `HttpClient` supports explicit h2/h2c, pooled h2 multiplexing, and HTTPS `auto` ALPN h2 selection; no standalone DNS, UDP, or WebSocket API. |
| OS stdlib (System, Environment, Process, ProcessHandle, Signals, OsError) | supported | supported from `sloppy/os` | `stdlib.os` feature emitted | V8 bridge required | No Node `process`/`child_process` compatibility; no process identity helpers. |
| Time stdlib | supported | supported from `sloppy/time` | `stdlib.time` feature emitted | V8 bridge for scheduling; pure JS for `Deadline`, `CancellationController`, `Time.fakeClock` | No global `setTimeout`/`setInterval`. |
| Crypto stdlib | supported | supported from `sloppy/crypto` | `stdlib.crypto` feature emitted | V8 bridge required (platform-delegated) | SHA-256/384/512, HMAC-SHA-256, Argon2id, OS CSPRNG, xxHash64. |
| Codec stdlib | supported | supported from `sloppy/codec` | `stdlib.codec` feature emitted | pure JS except `Compression` (V8 bridge) | No Node `Buffer`. |
| Workers stdlib | supported app-host | supported from `sloppy/workers` | `stdlib.workers` feature emitted | pure JS for `BackgroundService`/`WorkQueue`; V8 bridge for `WorkerPool.run` and `Worker.start` | App shutdown does not yet auto-stop background services. |
| Examples/dogfood | supported categories | mixed | mixed | mixed | See `examples/README.md` for proof classification. |
| Package/install path | n/a | n/a | package layout | experimental | Local archives and local npm tarball proof exist; public release/npm packages are not published. |

## Current Limits

- Production hardening, public release polish, and application dependency workflows are separate product work.
- Default local checks do not provide full cross-platform parity coverage.
- Compiler benchmark reports are local measurement artifacts. They are not
  compatibility guarantees or public performance comparisons.
- HTTP/3, gRPC, WebTransport, and server push are not included.
