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
| Routing | supported | supported | supported | supported | `GET`, `POST`, `PUT`, `PATCH`, and `DELETE` route metadata. |
| Route params | supported | supported | supported | supported | `{name}`, `{name:str}`, and `{name:int}` values are strings in handler context. |
| Query | supported | supported | supported | supported | Last value wins for repeated keys. |
| Headers | supported | supported through typed/header metadata and request context | supported | supported | `--once` cannot supply custom headers. |
| Body helpers | supported | supported through body metadata and generated wrappers | supported | supported | JSON/text/bytes are bounded in memory. |
| Results helpers | supported | supported subset | supported | supported | `text`, `json`, `ok`, `created`, `accepted`, `noContent`, `notFound`, `badRequest`, `problem`, `status`, `html`, and `bytes` have compiler/runtime coverage where fixtures exercise them. |
| ProblemDetails | supported | supported as literal `ProblemDetails.defaults(...)` | supported | supported | Safe handler-error mapping. |
| Middleware | supported | rejected | not emitted | app-host only | `SLOPPYC_E_UNSUPPORTED_MIDDLEWARE`. No separate endpoint-filter API yet. |
| CORS | supported | rejected | not emitted | app-host only | `SLOPPYC_E_UNSUPPORTED_CORS`; app-host creates preflight routes. |
| Health | supported | supported for literal static shapes | supported | supported | Compiler emits aggregate/live/ready handlers and health metadata. |
| Services | supported | supported for literal registrations and `Service<T>` | supported | supported through generated wrappers | App-host exposes `ctx.services`; native base context does not. |
| Config/defaults/secrets | supported | supported for config metadata and `Config<"KEY">` defaults | supported | supported | Secret values are not persisted in Plan metadata. |
| SQLite provider | supported | supported | supported | supported with V8/provider bridge | Strongest current provider path. |
| PostgreSQL provider | supported through runtime API and typed injection | supported metadata/generated typed injection | supported generated wrapper | live-provider gated | Requires config, active bridge, and live PostgreSQL service. |
| SQL Server provider | supported through runtime API and typed injection | supported metadata/generated typed injection | supported generated wrapper | live-provider gated | Requires config, ODBC driver, active bridge, and live SQL Server service. |
| Static provider handles | sqlite supported | sqlite supported | sqlite supported | sqlite supported | Static non-SQLite handles fail with `SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE`. |
| Typed bindings | test-host supported | supported | supported | supported | `Route`, `Query`, `Header`, `Body`, `RequestContext`, `Service`, `Config`, provider markers, and `WorkQueue`. |
| Compiler source input | n/a | supported subset | emits Plan, bundle, source map | supported with V8 for execution | Not a full TypeScript type checker or npm resolver. |
| OpenAPI | metadata consumer | Plan-derived | metadata-only | CLI only | Security schemes and app-host-only behavior are not emitted. |
| CLI `build` | n/a | supported | emits artifacts | no V8 required | Deterministic for the same source, config inputs, compiler, and CLI overrides. |
| CLI `run` | n/a | supported source/project handoff | validates artifacts | V8 required for handlers | `--once` creates a minimal synthetic request. |
| CLI `routes` | n/a | Plan-derived | metadata-only | no V8 required | Reads route metadata from Plan. |
| CLI `capabilities` | n/a | Plan-derived | metadata-only | no V8 required | Shows declared capability/provider metadata. |
| CLI `audit` | n/a | Plan-derived | metadata-only | no V8 required | Reports static Plan issues and policy notes. |
| CLI `doctor` | n/a | Plan-derived | metadata-only | no V8 required | Reports artifact and feature readiness. |
| App test host | supported | rejected import | not emitted | n/a | `Testing` is for JS tests around the app-host surface. |
| Logging | supported | supported config metadata | supported | supported | Native runtime owns console/file sinks; memory sink is deterministic app-host test surface. |
| Request IDs | supported middleware | rejected middleware | not emitted | native request IDs exposed | Compiler input rejects `RequestId.defaults(...)`; native `ctx.requestId` exists. |
| Request logging | supported middleware | rejected middleware | not emitted | direct `ctx.log` supported | Compiler input rejects `RequestLogging.defaults(...)`. |
| HTTP server | n/a | Plan/server config metadata | supported | experimental dev server | HTTP/1.1 only, bounded, not a production edge. |
| Inbound TLS | n/a | config metadata | supported paths | experimental | Certificate/key paths are Plan-visible today; diagnostics redact TLS material. |
| HttpClient | supported API | supported from `sloppy/net` | Plan-visible as required feature | experimental bridge | `https://` needs the private outbound TLS bridge. |
| Examples/dogfood | supported categories | mixed | mixed | mixed | See `examples/README.md` for proof classification. |
| Package/install path | n/a | n/a | package layout | experimental | Local archives exist; public release/npm launcher are not published. |

## Current Limits

- Production hardening and public release polish are still future work.
- Application dependency workflows are still future work.
- Default local checks do not provide full cross-platform parity coverage.
