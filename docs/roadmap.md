# Roadmap

This roadmap is the current repository-level planning summary. GitHub issues
own live task state; this document explains the product-mode direction and the
evidence boundaries that must hold before public alpha.

## Current Reality

| Area | Current reality |
| --- | --- |
| Core runtime | C primitives, diagnostics, memory ownership, platform boundaries, plan validation, and app-host startup are established foundations. |
| Compiler/artifacts | `sloppyc` can emit artifacts for the supported source subset and the runtime can execute artifact bundles. Full TypeScript semantics are not claimed. |
| V8 | V8 is isolated behind `src/engine/v8/*` and remains an explicit optional lane. Default evidence does not prove V8 behavior. |
| HTTP | Bounded HTTP/1.1 runtime behavior exists for development evidence. Production HTTP, TLS, public streaming APIs, middleware, WebSockets, and HTTP/2 or HTTP/3 are not claimed. |
| Providers | SQLite has scoped native/runtime support. PostgreSQL and SQL Server remain provider-boundary and metadata work without complete JS runtime bridges. |
| Capabilities/security | Runtime capability checks are policy enforcement points, not an OS sandbox. |
| Packaging | Package smoke proves source/package layout mechanics only. It is not release readiness. |
| Public docs | Public docs remain pre-alpha skeletons until the alpha gate explicitly promotes them. |

## Current Phase

The repository is in the pre-alpha product-mode transition. The work in this
phase cleans up documentation, source comments, skills, contributor guidance,
and stale planning records so current docs describe the runtime as it exists
instead of preserving construction history as active guidance.

This phase does not release public alpha, implement packaging, implement TLS,
complete Framework v2, or claim production readiness.

## Next Major Tracks

1. `HTTP-SERVER-01` - mature inbound HTTP/1.1 server behavior, diagnostics,
   lifecycle, bounded request/response handling, tests, and docs.
2. `HTTP-SERVER-TLS-01` - add inbound TLS only after the HTTP server contract is
   ready; this is not HTTP client TLS and not broad production-edge HTTP.
3. `FRAMEWORK-V2-01` - move toward compiler-inferred framework ergonomics while
   keeping the bootstrap/runtime boundary explicit.
4. Packaging, build, and distribution - produce honest experimental packaging
   evidence before any public release artifact claim.
5. Realistic dogfood and examples - add examples that exercise current runtime
   behavior without presenting unfinished Framework v2 APIs as tutorials.
6. Public alpha gate - verify docs, release notes, versioning, packaging,
   security/TLS posture, examples, and required CI before alpha cutover.

## Deferred By Design

The following remain deferred unless a scoped source doc and issue promote them:

- HTTP/2, HTTP/3, WebSockets, SSE, static file serving, and production-edge HTTP
  behavior.
- Package-manager behavior, npm compatibility, and Node/Bun/Deno compatibility.
- Production hardening, operational support claims, and performance claims.
- Broad provider bridges, ORM/migration layers, and live database support beyond
  scoped provider lanes.
- Public tutorials or broad public user documentation before Framework v2 and
  the public alpha gate are complete.

## Evidence Policy

Evidence must name each applicable lane and status: `PASS`, `FAIL`, `SKIPPED`,
`UNAVAILABLE`, `DEFERRED`, or `NOT RUN`. Default non-V8 evidence proves only
the default native/Cargo/CTest/scanner path. V8, package, source-input,
platform-specific, live-provider, fuzz/property, stress/torture, sanitizer, and
benchmark lanes are separate.

Skipped optional lanes are not pass evidence. Benchmark smoke proves harness
execution only; measured Release benchmark evidence is separate from correctness
and never supports performance claims without the command, build context,
hardware/context, workload, and output.

## Historical Records

Historical planning and audit records live under `docs/project/archive/`. They
remain useful evidence, but they are not the current roadmap. Current
architecture and workflow docs are linked from `docs/project/README.md`.
