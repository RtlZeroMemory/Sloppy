# Post-ENGINE-16 Runtime Modularity Audit

Status: 2026-05-05 planning/consolidation audit. This records current runtime feature
composition and the recommended Roadmap-2 modularity direction without changing code.

## Source Inputs

- Build/module shape: `CMakeLists.txt`, `include/sloppy/*`, `src/core/*`,
  `src/platform/libuv/*`, `src/engine/v8/*`, `src/data/*`.
- Runtime/stdlib/compiler shape: `stdlib/sloppy/*`, `compiler/src/*`, Plan fixtures,
  examples, and module docs.

## Current State

| Feature family | Current composition | Activation model | Risk |
| --- | --- | --- | --- |
| Core runtime | `sloppy_core` includes core primitives, Plan, diagnostics, resource/app-host, HTTP backend/transport, capability registry, provider executor, and data providers. | Always compiled into the core library when dependencies are available. | Core library is growing into a broad "everything included" unit. |
| V8 bridge | `SLOPPY_ENABLE_V8` gates `src/engine/v8/*`. When enabled, the bridge installs handler registration plus provider intrinsics under `__sloppy`. | Configure-time V8 gate; provider intrinsic registration is not Plan/import/use driven. | Optional lane is honest, but enabled V8 builds register more feature surface than a specific Plan may need. |
| HTTP transport | HTTP parser/backend/transport are core sources; libuv implementation is under `src/platform/libuv`. | Compiled as part of current runtime; used by CLI/source-input runtime paths. | Transport is not yet feature-described for package include-only-used policy. |
| SQLite provider | Native provider and V8 SQLite intrinsic exist. | Native provider always compiled; JS bridge installed whenever V8 intrinsics are installed. Runtime capability check gates provider work. | Synchronous V8 bridge blocks owner thread and is not feature-activated only when Plan/import requires SQLite. |
| PostgreSQL provider | Native libpq provider and tests exist. JS stdlib exposes metadata and honest bridge-unavailable errors. | Native provider compiled by current build; no V8 bridge. | Native dependencies can bloat non-using apps; future bridge must not bypass executor/capability model. |
| SQL Server provider | Native ODBC provider is compiled where enabled; stdlib exposes metadata and honest bridge-unavailable errors. | Platform/config gate plus native tests; no V8 bridge. | Same bloat/dependency risk, especially across non-Windows jobs. |
| Bootstrap stdlib | Source-controlled JS facade is staged/copied; compiler rewrites supported public imports into classic runtime artifacts. | Broad stdlib asset set is staged; compiler import support is intentionally narrow. | Packaging cannot yet include only used framework/provider modules. |
| Plan metadata | Plan v1 carries routes, handlers, data providers, capabilities, required features, and metadata consumed by CLI tooling. | Plan validates shape and required-feature compatibility; runtime feature activation is not driven from it yet. | Strong metadata exists, but runtime initialization still does not consume it as a feature graph. |

## Feature Boundaries Observed

- V8 types are contained under `src/engine/v8/*`; the public engine ABI remains C-shaped.
- Libuv handles stay under `src/platform/libuv/*`; public HTTP transport headers expose
  Slop-owned structs and opaque platform pointers.
- Provider-specific bridge code is split from `engine_v8.cc` into `intrinsics_sqlite.cc`,
  with `intrinsics.cc` as the aggregator.
- The Rust compiler recognizes a supported `"sloppy"` facade and
  `"sloppy/providers/sqlite"` import; unsupported bare imports fail honestly.
- Plan metadata can describe data providers/capabilities and Strong Plan completeness, but
  there is no runtime feature registry that maps Plan/import/use to initialization.

## Always-On / Bloat Risks

| Risk | Evidence | Roadmap-2 answer |
| --- | --- | --- |
| Provider code and dependencies are linked even when unused. | `CMakeLists.txt` includes `src/data/sqlite.c`, `src/data/postgres.c`, and `src/data/sqlserver.c` in core sources. | ENGINE-27.A/C/F should define feature descriptors and package include policy. |
| V8 intrinsic surface is installed as a group. | `sl_v8_install_intrinsics` installs `__sloppy` and calls provider intrinsic aggregation. | ENGINE-27.D should make intrinsic registration feature-gated. |
| Plan metadata is validated but not used to initialize only required runtime features. | Plan parser and CLI consumers use metadata; runtime core does not have a feature activation registry. | ENGINE-27.B should make Plan-driven feature activation explicit. |
| Stdlib assets are staged broadly. | Bootstrap stdlib is copied as a whole and compiler rewrites only supported imports today. | ENGINE-27.C/F should connect stdlib module descriptors and packaging policy. |
| Missing-feature diagnostics are ad hoc by layer. | Non-V8, non-SQLite bridge, and unsupported import errors are honest but not from one registry. | ENGINE-27.E should define stable missing-feature diagnostics. |

## Recommended Future Model

Roadmap-2 should make these runtime feature units explicit:

- `slop-core`;
- `slop-v8`;
- `slop-http`;
- `slop-transport-libuv`;
- `slop-provider-sqlite`;
- `slop-provider-postgres` later;
- `slop-provider-sqlserver` later;
- framework stdlib modules;
- Plan/import/use-driven feature activation.

This is a feature registry and activation model, not a packaging release claim. It must not
turn into Node/npm compatibility, plugin loading, public alpha docs, or broad code motion.

## Recommended Roadmap-2 Work

- ENGINE-27.A: Runtime Feature Registry.
- ENGINE-27.B: Plan-Driven Feature Activation.
- ENGINE-27.C: Provider/Transport/Stdlib Feature Descriptors.
- ENGINE-27.D: V8 Intrinsic Registration by Feature.
- ENGINE-27.E: Missing Feature Diagnostics.
- ENGINE-27.F: Package Include-Only-Used Feature Policy.
