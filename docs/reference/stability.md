# Stability Reference

Sloppy is public alpha, pre-production software. It is ready for experiments,
demos, feedback, and early exploration, but not production deployments yet.
Contracts and behavior can change between alpha revisions.

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
- Typed-handler provider injection is generated for SQLite, PostgreSQL,
  and SQL Server. SQLite is embedded. PostgreSQL and SQL Server execution
  depends on optional provider dependencies, provider config, and live service
  setup only when those providers are used.
- Live provider checks are opt-in and environment-dependent.

## Feature Matrix

This matrix is the compact current-state map. `supported` means the surface is
covered by current source and tests in that column. `experimental` means the
surface exists in the public alpha, pre-production runtime but may change. `rejected` means the
compiler refuses the input rather than emitting a partial Plan.

| Feature | App-host/test-host | Compiler source input | Generated `app.js` / Plan | Native/V8 `sloppy run` | Notes |
| --- | --- | --- | --- | --- | --- |
| Routing | supported | supported | supported | supported | Direct route registration supports `GET`, `POST`, `PUT`, `PATCH`, and `DELETE`; generated CORS preflight routes bind `OPTIONS` in Plan metadata. Dispatch precedence is static before parameter, constrained before unconstrained, longer/more-specific before shorter, then source order. |
| Route params | supported | supported | supported | supported | `{name}`, `{name:str}`, `{name:int}`, `{name:uuid}`, `{name:alpha}`, and `{name:float}` values are strings in handler context. |
| Query | supported | supported | supported | supported | Last value wins for repeated keys. |
| Headers | supported | supported through typed/header metadata and request context | supported | supported | `--once` can supply bounded custom headers through `--header`. |
| Body helpers | supported | supported through body metadata and generated wrappers | supported | supported | JSON/text/bytes/forms/multipart are bounded in memory. |
| Static files | API shape supported | supported for literal `app.useStaticFiles(...)` options | generated `GET` routes and asset graph records | supported through generated handlers | Build/package-time snapshot for supported file extensions; no directory listings or runtime file watching. |
| Cookies | supported | supported metadata for `ctx.cookies.get(...)` and auth session cookies | supported | supported | Auth session cookies are signed; general-purpose cookie signing/encryption remains application-owned. |
| Schema validation | supported | supported for static `Schema.*` declarations, `ctx.body.validate(...)`, `.accepts(...)`, and `.returns(...)` identifiers | supported Plan/OpenAPI metadata with route JSON request/response plans | supported for native schema-backed JSON request validation through `ctx.body.validate(...)` and `.accepts(...)`; supported `.returns(...)` response schemas use the bounded native JSON response writer | Runtime schemas cover strings, numbers, integers, booleans, arrays, objects, enums, literals, optional, nullable, defaults, and common string/number bounds. Native response writing supports objects, arrays, nested objects, strings, finite numbers, integers, booleans, nulls, nullable values, and optional object fields; unsupported response schema shapes carry explicit fallback reasons. |
| Results helpers | supported | supported subset | supported | supported | `text`, `json`, `ok`, `created`, `accepted`, `noContent`, `notFound`, `badRequest`, `unauthorized`, `problem`, `status`, `html`, `bytes`, and bounded `stream` have compiler/runtime coverage where fixtures exercise them. Literal/static `Results.json` and `Results.ok` can be emitted as native preencoded JSON responses; supported schema-backed dynamic JSON result bodies can use native bounded response serialization. Unsupported dynamic JSON result bodies use the alpha Sloppy serializer for dates, BigInts, bytes, undefined fields, class instances, errors, and circular-reference failures. `Results.stream` is a bounded descriptor builder serialized through native Core stream paths, not a WHATWG or Node stream. |
| Native Core streams | n/a | n/a | n/a | experimental native runtime capability | `SlReadableStream`, `SlWritableStream`, bounded chunk-list/memory adapters, HTTP response stream adapter, request-body adapter, pump, stats, cancellation, partial-write, and backpressure statuses are used by native runtime components. No public JavaScript stream handle is exposed. |
| Error policy | supported | planned | app-host metadata only | app-host only | `app.useErrors(...)` and `app.mapError(...)` cover app-host problem responses, typed mappings, request IDs, redacted provider errors, and safe error logs. Compiler/OpenAPI/audit extraction is planned beyond the compatibility `ProblemDetails.defaults(...)` path. |
| ProblemDetails | supported | supported as literal `ProblemDetails.defaults(...)` | supported | supported | Compatibility surface for safe handler-error mapping. Prefer `app.useErrors(...)` in new app-host code. |
| Realtime routes | experimental SSE API shape; app-host WebSocket primitives; in-process hubs | supported for literal `app.sse(...)`, `app.ws(...)`, and `app.websocket(...)` route registrations | route `kind`, `runtime.realtime`, routes CLI, and OpenAPI extensions supported | SSE uses bounded `Results.stream` through native Core stream serialization; native WebSocket upgrade execution unavailable and returns `501` | App-host/TestHost WebSockets support accept, text/JSON/binary messages, schema validation, close, origin checks, auth checks, message limits, and bounded send queues. Artifact/package TestHost WebSockets report `SLOPPY_E_TESTHOST_WEBSOCKET_UNSUPPORTED` until a runtime lane supports real upgrades. Hubs are in-process bootstrap helpers, not external brokers. |
| Middleware | supported | supported static subset | supported | supported | Inline/top-level middleware is emitted into generated handlers; dynamic lookup and unsupported captures fail with `SLOPPYC_E_UNSUPPORTED_MIDDLEWARE`. |
| CORS | supported | supported static subset | supported | supported | Literal `app.useCors({...})` policies emit response wrapping, Plan metadata, and generated `OPTIONS` preflight routes; dynamic policies fail with `SLOPPYC_E_UNSUPPORTED_CORS`. |
| Auth | supported | supported static subset | supported | supported with V8 stdlib crypto/codec/os features | JWT bearer validates HS256 and static-key RS256 where WebCrypto is available; API keys support header validation, optional authorization-scheme keys, and static hashed keys; signed cookie sessions support sign-in/sign-out and CSRF; route/group auth supports schemes, scopes, roles, claims, policies, anonymous overrides, Plan metadata, and OpenAPI security schemes. OIDC discovery, remote JWKS fetch, OAuth flows, refresh tokens, and user management are not included. |
| Health and management | supported | supported for legacy `app.mapHealthChecks(...)`; `app.health()` and `app.management()` are app-host APIs | legacy health metadata supported | supported for legacy health routes and app-host generated handlers | `Health`, `Metrics`, `app.health()`, and `app.management()` provide bootstrap app-host operations endpoints. Compiler extraction for the legacy health API remains separate from the richer app-host operations API. |
| Services | supported | supported for literal registrations and `Service<T>` | supported | supported through generated wrappers | App-host exposes `ctx.services`; native base context does not. |
| Config/defaults/secrets | supported | supported for config metadata and `Config<"KEY">` defaults | supported | supported | Secret values are not persisted in Plan metadata. |
| SQLite provider | supported | supported | supported | supported with V8/provider bridge | Strongest current provider path. Query materialization is bounded by default and supports per-call `maxRows`; finite `timeoutMs`/deadline budgets can interrupt query/queryRaw and cursor work; public cursor APIs stream rows incrementally. |
| PostgreSQL provider | supported through runtime API and typed injection | supported metadata/generated typed injection | supported generated wrapper | live-provider gated | Optional provider. Requires config, PostgreSQL client support, active bridge, and live PostgreSQL service when used. Current alpha packages do not yet ship package-local libpq. Query materialization is bounded by default and supports per-call `maxRows`; finite `timeoutMs`/deadline budgets can interrupt query/queryRaw and cursor work; public cursor APIs stream rows incrementally. |
| SQL Server provider | supported through runtime API and typed injection | supported metadata/generated typed injection | supported generated wrapper | live-provider gated | Optional provider. Requires config, Microsoft ODBC Driver 17 or 18, active bridge, and live SQL Server service when used. The core package does not bundle Microsoft's ODBC driver. Query materialization is bounded by default and supports per-call `maxRows`; finite `timeoutMs`/deadline budgets can interrupt query/queryRaw and cursor work; public cursor APIs stream rows incrementally. |
| ORM | supported app-host API | import recognized; static AST extraction where provable | `features.orm`, static `orm.tables`/`orm.relations` where proven, and partial metadata for runtime-dynamic shapes | supported when provider/runtime bridge is available | `sloppy/orm` provides table/column DSL, schemas, CRUD, query builder, includes, transactions, migration snapshot/diff helpers, raw SQL, cursors, and Sloppy-native ORM errors. Runtime use works dynamically; Plan table/relation extraction is partial when definitions are too dynamic to prove statically. |
| Static provider handles | sqlite supported | sqlite supported | sqlite supported | sqlite supported | Static non-SQLite handles fail with `SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE`. |
| Typed bindings | test-host supported | supported | supported | supported | `Route`, `Query`, `Header`, `Body`, `RequestContext`, `Service`, `Config`, provider markers, and `WorkQueue`. |
| Compiler source input | n/a | supported subset | emits Plan, bundle, source map | supported with V8 for execution | Not a full TypeScript type checker or npm resolver. |
| Program Mode | n/a | supported route-free subset | emits `kind: "program"` Plan and generated entrypoint | supported with V8 | `main(args, ctx)`, default function entrypoints, top-level-only modules, console stdout/stderr, numeric exit codes, stdlib imports, compatible bundled packages, and packaged runs are covered. No full Node globals, native addons, or raw terminal API. |
| OpenAPI | metadata consumer | Plan-derived | metadata-only | CLI only | Route, response, validation, and auth security metadata are emitted from the Plan-supported subset. Full runtime-pipeline semantics remain outside current output. |
| Content negotiation | strict/fallback app option supported | metadata only | response media types recorded | fallback by helper media type; stricter native negotiation is planned | App/test host supports opt-in strict `Accept` checks. Generated/native runs keep deterministic helper media types and request `Content-Type` validation. |
| CLI `build` | n/a | supported | emits artifacts | no V8 required | Deterministic for the same source, config inputs, compiler, and CLI overrides. |
| Compiler timings | n/a | supported dev flag | timing JSON only | n/a | `sloppyc build --timings-json` is local contributor tooling for phase/counter evidence, not a product API or public performance claim. |
| CLI `run` | n/a | supported source/project handoff | validates artifacts | V8 required for handlers | `--once` creates a bounded synthetic request; repeatable headers plus text, file, and JSON bodies are supported for static route metadata and artifact/package smoke tests. |
| CLI `dev` | n/a | supported source/project handoff | rebuilds artifacts and optional OpenAPI | experimental V8-backed dev server | Polling watch mode rebuilds source/config/static/template/migration inputs, restarts after successful builds, and keeps the previous server running after build failures. Not a production supervisor. |
| CLI `create` | n/a | n/a | copies templates | no V8 required | Built-in templates include `api`, `minimal-api`, `program`, `cli`, `package-api`, and `node-compat`. |
| CLI `package` | n/a | supported source/project handoff | emits app package directory | no V8 required | Creates a local app package under `.sloppy/package/` by default; dependency graph assets and local FFI libraries configured through `ffiLibraries` are copied with package manifest hashes/metadata. Not a runtime release archive. |
| CLI `db` | n/a | Plan and `sloppy.json` migration metadata | reads package manifest migrations | no V8 required; SQLite needs no external DB driver; PostgreSQL/SQL Server need optional provider config only when selected | `status` and `migrate` work for SQLite, PostgreSQL, and SQL Server artifacts/packages. PostgreSQL and SQL Server report provider-specific diagnostics when their optional driver/service dependencies are missing. |
| CLI `orm migration` | n/a | Plan and `sloppy.json` migration metadata | reads package manifest migrations | no V8 required; same provider rules as `sloppy db` | `script` and `add` generate FK-safe reviewable SQL from compiler-emitted ORM table metadata; `status` aliases `sloppy db status`; `apply` aliases `sloppy db migrate`. JavaScript migration helpers are also available through `orm.migrations.script/snapshot/diff`. |
| CLI `routes` | n/a | Plan-derived | metadata-only | no V8 required | Reads route metadata from Plan. |
| CLI `deps` | n/a | Plan-derived | metadata-only | no V8 required | Reads dependency graph metadata from Plan or `deps.graph.json`. |
| CLI `capabilities` | n/a | Plan-derived | metadata-only | no V8 required | Shows declared capability/provider metadata. |
| CLI `audit` | n/a | Plan-derived | metadata-only | no V8 required | Reports static Plan issues and policy notes. |
| CLI `doctor` | n/a | Plan-derived | metadata-only | no V8 required | Reports artifact and feature readiness. Missing PostgreSQL or SQL Server dependencies are optional provider guidance unless the app uses that provider. |
| App test host | supported | rejected import | n/a | n/a | `Testing` remains a JS test helper and is not valid compiler input. The test host records route-pattern HTTP metrics for matched app routes. |
| TestServices | experimental in JS tests | rejected import | n/a | Docker and provider bridge required | Starts real PostgreSQL/SQL Server containers through Docker CLI and verifies readiness through the matching data provider bridge. Default CI keeps live containers opt-in. |
| Logging | supported | supported config metadata | supported | supported | Native runtime owns console/file sinks; memory sink is deterministic app-host test surface. |
| Request IDs | supported middleware | supported static middleware | supported | supported | Compiler input supports static `RequestId.defaults(...)`; generator callbacks remain app-host test-only. Native `ctx.requestId` exists. |
| Request logging | supported middleware | supported static middleware | supported | supported | Compiler input supports static `RequestLogging.defaults(...)`; dynamic option values are rejected at build time. Native `ctx.log` is direct logging. |
| HTTP server | n/a | Plan/server config metadata | supported | experimental dev server | HTTP/1.1 plus server HTTP/2 over TLS ALPN, h2c prior knowledge, and h2c Upgrade. Not a production edge. |
| Inbound TLS | n/a | config metadata | supported paths | experimental | Certificate/key paths are Plan-visible today; diagnostics redact TLS material. |
| HttpClient / Http factory | supported API | low-level `HttpClient` supported from `sloppy/net`; static factory metadata is Plan-visible for literal client declarations | low-level and factory feature metadata are Plan-visible where statically provable | experimental bridge | `Http` from `sloppy/http` provides named clients, typed clients, generated OpenAPI clients, resilience policies, service registration, TestHost mocks across app/artifact/package modes, metrics, diagnostics, and health snapshots over the low-level pooled `HttpClient`. HTTP/1.1 by default; explicit h2/h2c, pooled h2 multiplexing, and HTTPS `auto` ALPN h2 selection are supported. `https://` needs the private outbound TLS bridge. |
| Filesystem stdlib | supported | supported from `sloppy/fs` | `stdlib.fs` feature emitted | V8 bridge required | Node `fs` compatibility is a partial shim over supported Sloppy filesystem behavior, not full Node `fs`. |
| Network stdlib (TCP, local IPC) | supported | supported from `sloppy/net` | `stdlib.net` feature emitted | V8 bridge required | `HttpClient` supports explicit h2/h2c, pooled h2 multiplexing, and HTTPS `auto` ALPN h2 selection; no standalone DNS or UDP API. WebSocket route metadata lives under Realtime, with native upgrade execution unavailable. |
| OS stdlib (System, Environment, Process, ProcessHandle, Signals, OsError) | supported | supported from `sloppy/os` | `stdlib.os` feature emitted | V8 bridge required | `node:process` is partial compatibility. Bundled package programs may receive temporary `global`, `process`, and `Buffer` globals; no process-wide Node identity or `child_process` compatibility. |
| Time stdlib | supported | supported from `sloppy/time` | `stdlib.time` feature emitted | V8 bridge for scheduling; pure JS for `Deadline`, `CancellationController`, `Time.fakeClock` | `node:timers` is partial and maps only where timer globals are available. |
| Crypto stdlib | supported | supported from `sloppy/crypto` | `stdlib.crypto` feature emitted | V8 bridge required (platform-delegated) | SHA-256/384/512, HMAC-SHA-256, Argon2id, OS CSPRNG, xxHash64. |
| Codec stdlib | supported | supported from `sloppy/codec` | `stdlib.codec` feature emitted | pure JS except `Compression` (V8 bridge) | `node:buffer` is partial compatibility and does not claim full Node Buffer identity. |
| Workers stdlib | supported app-host | supported from `sloppy/workers` | `stdlib.workers` feature emitted | pure JS for `BackgroundService`/`WorkQueue`; V8 bridge for `WorkerPool.run` and `Worker.start` | App shutdown does not yet auto-stop background services. |
| Native FFI stdlib | n/a | supported static `sloppy/ffi` declarations | `stdlib.ffi`, `native.ffi`, and `native.ffiStructs` emitted | experimental V8/libffi bridge | P/Invoke-style typed C ABI calls, refs, buffers, and pointer-based sequential structs. Unsafe boundary; wrong signatures can crash. No callbacks, variadic functions, C++ ABI, struct-by-value, async FFI, native addons, or raw pointer-call API. |
| Examples and evidence catalog | supported categories | mixed | mixed | mixed | See `examples/README.md` for coverage classification. |
| Package/dependency graph | n/a | experimental installed pure-JavaScript package resolver | bundled module graph, `dependencyGraph`, optional `deps.graph.json`, `sloppy deps --explain` | supported with V8 for compatible bundled modules | No registry install, semver solving, native addons, full Node HTTP/socket parity, or unrestricted runtime discovery. Resolver behavior is exercised by the committed `tests/fixtures/npm-compat/` matrix; the matrix is the regression baseline for currently tested package shapes and does not promise behavior for shapes outside it. |
| Package/install path | n/a | n/a | package layout | experimental | Windows x64, Linux x64 glibc, and macOS npm alpha packages install the launcher, native runtime, stdlib, templates, selected docs/examples, and V8-backed handler execution. |

## Current Limits

- Production hardening and application dependency workflows are separate product work.
- Default local checks do not provide full cross-platform parity coverage.
- Compiler benchmark reports are local measurement artifacts. They are not
  compatibility guarantees or public performance comparisons.
- HTTP/3, gRPC, WebTransport, and server push are not included.
