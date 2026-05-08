# Sloppy

Sloppy is a pre-alpha TypeScript backend app-host with a C runtime kernel, an
isolated V8 bridge, and a Rust compiler toolchain. It is built around
compiler-emitted artifacts, a validated Sloppy Plan, runtime diagnostics, and
explicit platform boundaries.

The project is not a public alpha release. It is not production ready, not a
Node/Bun/Deno compatibility target, and not a package-manager ecosystem. The
current repository is a runtime foundation and pre-alpha consolidation line.

## What Sloppy Is

Sloppy is intended to host backend applications from compiler-known inputs
rather than discover application structure at runtime. The compiler emits a
Sloppy Plan and JavaScript artifacts; the native app-host validates those
artifacts, initializes runtime services, and dispatches into V8 through the
engine bridge when V8 is enabled.

Sloppy is cross-platform by design. Windows x64 is currently the most complete
validated local development lane, while Linux and macOS are represented by
default non-V8 CI presets and explicit evidence lanes. Runtime behavior is
documented through source-of-truth architecture docs, tests, golden fixtures,
and explicit evidence lanes.

## Current Executable Path

The supported development path is narrow but real:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 doctor
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 package
.\tools\windows\dev.ps1 test-package
```

Compiler/source-input work flows through `sloppyc` and the artifact runtime:

```powershell
sloppy build
sloppy run
sloppy run src/main.ts
sloppy run --artifacts .sloppy --once GET /health
```

The canonical project entrypoint is `src/main.ts` configured by `sloppy.json`.
`sloppy build` and source-input `sloppy run` compile the supported static source graph
into Plan-backed artifacts before execution. `sloppyc build <source> --out <dir>` remains
the lower-level compiler/debug path.

The V8 runtime path requires a resolved V8 SDK and a V8-enabled build. Default
non-V8 evidence does not prove V8 execution.

```powershell
.\tools\windows\resolve-v8-sdk.ps1 -Fetch
.\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8
.\tools\windows\dev.ps1 build -Preset windows-relwithdebinfo
.\tools\windows\dev.ps1 test -Preset windows-relwithdebinfo
```

## Current Status

| Area | Current status |
| --- | --- |
| Compiler and artifacts | `sloppyc` can emit source-input artifacts for the supported subset and the runtime can execute artifact bundles. Full TypeScript checking/lowering is not implemented. |
| Runtime kernel | Core C primitives, diagnostics, memory ownership rules, platform boundaries, and app-host validation are established and covered by tests. |
| V8 bridge | V8 remains isolated behind the engine bridge. Direct handlers and bounded owner-thread microtask drainage are supported only in the scoped V8 lane. |
| HTTP | HTTP/1.1 runtime support is bounded development evidence. Sequential keep-alive and bounded chunked handling exist, but production-edge HTTP, pipelining, streaming public APIs, middleware, and TLS are not claimed. |
| Data providers | SQLite, PostgreSQL, and SQL Server have scoped native/provider and V8-gated bridge lanes. PostgreSQL and SQL Server live evidence remains opt-in and must not be reported as default evidence. |
| Capabilities and security | Capability checks are runtime policy checks. They are not an operating-system sandbox. |
| Packaging | Package smoke checks prove local archive layout and outside-checkout behavior only. They are not release-readiness evidence. |
| Benchmarks | Benchmark harnesses may compile or smoke-run, but no performance claim is made from this repository state. |

## Limits That Matter

- Public alpha remains blocked by documentation, packaging/distribution,
  realistic examples, HTTP maturity, final release checks, and issue-level gate
  work.
- Sloppy is not production ready and does not claim operational hardening.
- Sloppy is not Node, Bun, Deno, Express, or npm compatible.
- Node/npm package-manager behavior is not a goal unless a scoped source doc and
  issue explicitly add it.
- Full TypeScript semantics, dependency resolution, watch mode, hot reload, and
  a public tutorial workflow remain outside the current executable subset.
- Optional lanes such as V8, package smoke, live providers, fuzz/torture,
  sanitizer, and benchmark evidence must be reported separately.

## Developer Workflow

Use the canonical Windows scripts for local work:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 doctor
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
```

Relevant checks must be reported honestly. A skipped, unavailable, or optional
lane is not pass evidence. When behavior changes, the source doc, tests, and
implementation should move together.

## Source Documents

Start with these current source-of-truth documents:

- [Roadmap](docs/roadmap.md)
- [Architecture](docs/architecture.md)
- [Execution model](docs/execution-model.md)
- [Concurrency and async model](docs/concurrency.md)
- [Developer ergonomics](docs/developer-ergonomics.md)
- [Application plan model](docs/app-plan.md)
- [Compiler](docs/compiler.md)
- [Supported compiler syntax](docs/compiler-supported-syntax.md)
- [Testing strategy](docs/testing-strategy.md)
- [Quality gates](docs/quality-gates.md)
- [Project documentation map](docs/project/README.md)

Contributor process lives in [CONTRIBUTING.md](CONTRIBUTING.md). Agent-specific
rules live in [AGENTS.md](AGENTS.md).

## Before Public Alpha

The next work is tracked in the current roadmap:

- HTTP server maturation.
- Inbound TLS after the HTTP server contract is ready.
- Framework v2 ergonomics and compiler-inferred application shape.
- Packaging, build, and distribution readiness.
- Realistic dogfood applications and examples.
- Final public alpha verification, release notes, and versioning.

Until those tracks land and pass their required evidence lanes, this repository
should be read as a serious pre-alpha runtime project, not as a public product
launch.
