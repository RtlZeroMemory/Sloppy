# Post-ENGINE-16 Runtime Modularity Audit

Status: 2026-05-05 implementation audit. ENGINE-27.A/B records the first runtime feature
registry and Plan-driven activation path. ENGINE-27.C/D adds concrete provider,
transport, and stdlib descriptor metadata and passes the active feature set into the V8
bridge so feature-owned intrinsics are registered only when the active Plan requires them.
Later ENGINE-27 slices still own diagnostic goldens and package policy.

## Source Inputs

- Build/module shape: `CMakeLists.txt`, `include/sloppy/*`, `src/core/*`,
  `src/platform/libuv/*`, `src/engine/v8/*`, `src/data/*`.
- Runtime/stdlib/compiler shape: `stdlib/sloppy/*`, `compiler/src/*`, Plan fixtures,
  examples, and module docs.

## Current State

| Feature family | Current composition | Activation model | Risk |
| --- | --- | --- | --- |
| Core runtime | `sloppy_core` includes core primitives, Plan, diagnostics, resource/app-host, HTTP backend/transport, capability registry, provider executor, and data providers. | Always compiled into the core library when dependencies are available. | Core library is growing into a broad "everything included" unit. |
| V8 bridge | `SLOPPY_ENABLE_V8` gates `src/engine/v8/*`. When Plan activation passes an active feature set, the bridge installs handler registration for `stdlib.framework/app` and SQLite provider intrinsics only when their features are active. | Configure-time V8 gate plus Plan-driven intrinsic registration for app-host startup. Low-level engine smoke paths that omit a feature set retain legacy install behavior only as bridge tests. | Optional lane is honest; future slices still need formal missing-intrinsic diagnostics/goldens. |
| HTTP transport | HTTP parser/backend/transport are core sources; libuv implementation is under `src/platform/libuv`. | Compiled as part of current runtime; Plan routes activate `http` and `transport.libuv` descriptors before runtime startup. | Package include-only-used policy is not yet implemented. |
| SQLite provider | Native provider and V8 SQLite intrinsic exist. | Native provider is still compiled, but `provider.sqlite` now has a descriptor for `sloppy/providers/sqlite` and `__sloppy.data.sqlite`; app-host V8 startup registers the intrinsic only when Plan provider metadata activates SQLite. Runtime capability checks still gate provider work. | Synchronous V8 bridge blocks owner thread until ENGINE-28 routes it through the executor. |
| PostgreSQL provider | Native libpq provider and tests exist. JS stdlib exposes metadata and honest bridge-unavailable errors. | Native provider compiled by current build; no V8 bridge. | Native dependencies can bloat non-using apps; future bridge must not bypass executor/capability model. |
| SQL Server provider | Native ODBC provider is compiled where enabled; stdlib exposes metadata and honest bridge-unavailable errors. | Platform/config gate plus native tests; no V8 bridge. | Same bloat/dependency risk, especially across non-Windows jobs. |
| Bootstrap stdlib | Source-controlled JS facade is staged/copied; compiler rewrites supported public imports into classic runtime artifacts. | Broad stdlib asset set is staged, but descriptors now record current stdlib import ownership for app, results, schema, config, data, and provider modules. | Packaging cannot yet include only used framework/provider modules. |
| Plan metadata | Plan v1 carries routes, handlers, data providers, capabilities, required features, and metadata consumed by CLI tooling. | ENGINE-27.A/B derives active features from target/route/provider metadata and explicit `requiredFeatures[]` before runtime initialization; ENGINE-27.C/D threads that active set into V8 creation. | Feature activation and intrinsic gating are now explicit, but package/link trimming remains later work. |

## Feature Boundaries Observed

- V8 types are contained under `src/engine/v8/*`; the public engine ABI remains C-shaped.
- Libuv handles stay under `src/platform/libuv/*`; public HTTP transport headers expose
  Slop-owned structs and opaque platform pointers.
- Provider-specific bridge code is split from `engine_v8.cc` into `intrinsics_sqlite.cc`,
  with `intrinsics.cc` as the aggregator.
- The Rust compiler recognizes a supported `"sloppy"` facade and
  `"sloppy/providers/sqlite"` import; unsupported bare imports fail honestly.
- Plan metadata can describe data providers/capabilities and Strong Plan completeness.
  ENGINE-27.A/B adds `include/sloppy/features.h` and `src/core/features.c` as the first
  runtime registry that maps Plan target/routes/providers/`requiredFeatures[]` to active
  runtime features.

## Always-On / Bloat Risks

| Risk | Evidence | Roadmap-2 answer |
| --- | --- | --- |
| Provider code and dependencies are linked even when unused. | `CMakeLists.txt` includes `src/data/sqlite.c`, `src/data/postgres.c`, and `src/data/sqlserver.c` in core sources. | ENGINE-27.C records descriptors; ENGINE-27.F still owns package include policy. |
| V8 intrinsic surface is installed as a group. | App-host startup passes `SlRuntimeFeatureSet` through `SlEngineOptions`; `src/engine/v8/intrinsics.cc` skips SQLite registration unless `provider.sqlite` is active. | ENGINE-27.E should formalize inactive-intrinsic diagnostics/goldens. |
| Plan metadata is validated but not used to initialize only required runtime features. | ENGINE-27.A/B validates Plan-driven feature activation before runtime initialization; ENGINE-27.C/D uses the set for V8 intrinsic registration. | Remaining work is package include policy and broader future feature consumers. |
| Stdlib assets are staged broadly. | Bootstrap stdlib is copied as a whole and compiler rewrites only supported imports today; descriptors now map current stdlib/provider imports to runtime features. | ENGINE-27.F should define packaging policy without claiming trimming yet. |
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
