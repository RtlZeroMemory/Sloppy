# Sloppy

AI-slop branding, zero-slop architecture.

Sloppy is an experimental TypeScript backend application runtime/app-host. It has a C
runtime kernel, an isolated C++ V8 bridge, a source-controlled JavaScript bootstrap stdlib,
and a Rust compiler/build tool placeholder named `sloppyc`.

Sloppy is not production ready. The repo is still pre-alpha foundation work. It is also not
a Node, Bun, Deno, or Express compatibility project. The intended product shape is a
compiler-planned app host: Sloppy APIs describe routes, modules, services, permissions,
diagnostics, and data providers; `sloppyc` will emit artifacts; the native host will load
the Sloppy Plan and execute handlers through V8.

Windows x64 is the first-class development path. Sloppy is cross-platform by design:
platform-specific API calls belong under `src/platform/*`, and portable runtime modules
must not include OS-specific headers.

## What Works Today

- Windows CMake/Ninja/Cargo developer workflow through `tools/windows`.
- Default hosted CI for Windows clang-cl, Linux clang/gcc, and macOS clang non-V8 builds.
- Portable C core primitives: status, source locations, strings, bytes, checked math,
  assertions, `SlArena`, diagnostics, plan parsing, scope cleanup, loop/async/worker-pool
  skeletons, route parsing, HTTP request-head parsing, and synthetic dispatch helpers.
- Optional V8 bridge smoke when a valid V8 SDK is configured; default builds do not require
  or validate V8.
- Minimal handwritten `app.plan.json` plus `app.js` execution smoke in V8-enabled builds.
- Narrow compiler artifact path: `sloppyc build` can emit deterministic artifacts for the
  current supported source shape, and `sloppy run --artifacts` can execute selected
  V8-gated conformance fixtures.
- Bootstrap ESM stdlib with `Sloppy`, `Results`, `schema`, `data`, builder/app skeletons,
  route groups, modules, config/logging/services, fake data providers, and examples.
- Dev-only HTTP response/context path for current compiler artifacts: route/query/request
  context, result descriptor conversion, and response writing for the narrow supported
  path.
- Native SQLite, PostgreSQL, and SQL Server provider boundaries with C tests. PostgreSQL and
  SQL Server live tests are opt-in through environment variables.
- V8-gated SQLite JavaScript bridge through resource IDs. SQLite capability enforcement is
  still a remaining foundation blocker.
- Metadata-only CLI introspection: `sloppy routes`, `sloppy doctor`, `sloppy audit`, and
  `sloppy openapi`.
- Benchmark harness for current foundations. Smoke/list checks are not performance claims.

## What Does Not Work Yet

- No broad `sloppyc` extraction from application TypeScript beyond the current tiny
  compiler MVP shape.
- No `app.plan.json` emission from the full public API surface.
- No source-input `sloppy run`; the dev-only run path currently loads prebuilt artifacts.
- No production HTTP server or full framework HTTP runtime. Request body parsing, headers in
  handler context, multipart/file upload, middleware, and production hardening remain
  unimplemented.
- No final V8 ESM module graph or async Promise/microtask handler support.
- No JavaScript-to-native PostgreSQL or SQL Server bridge. SQLite is the only current
  V8-gated JS/native bridge, and it still needs capability-policy enforcement.
- No package-manager behavior and no Node compatibility goal.
- No OS sandbox. Native capability metadata/check hooks exist, but bridge enforcement is not
  complete until real SQLite access calls the policy hook.
- No public alpha distribution, installers, signing/notarization, package-manager
  integration, or auto-update. Experimental local ZIP/TAR package tooling exists only to
  validate the current artifact layout.
- No public performance comparison claims.

## Example Shape

The intended user-facing API still looks like this:

```ts
import { Sloppy, Results } from "sloppy";

const builder = Sloppy.createBuilder();
const app = builder.build();

app.mapGet("/", () => Results.text("Sloppy is alive"));

await app.run();
```

Today, `app.run()` and the bare `"sloppy"` import are future behavior. Checked-in examples
under `examples/` use relative imports from `stdlib/sloppy/` and are static/bootstrap
API-shape examples unless a page explicitly says otherwise.

## Current Roadmap State

The initial EPIC-00 through EPIC-20 roadmap produced a broad foundation: native core,
minimal plan loader, V8 smoke, handwritten execution, concurrency skeletons, HTTP/router
foundation, bootstrap stdlib/app-host ergonomics, modules, data/provider foundations,
metadata CLI tools, and benchmarks.

The EPIC-21 through EPIC-26 batch produced the compiler MVP, dev-only artifact run path,
HTTP response/request-context MVP, classic bootstrap runtime handoff, experimental local
packaging, and default non-V8 hosted CI. MAIN and MAIN.1 then hardened the narrow path
through PRs #240-#255.

The next strategic phase is Slop Engine foundation completion, not public alpha docs and
not benchmark marketing. The foundation blockers are compiler breadth for supported apps,
real V8 Promise/microtask handling, full framework HTTP API runtime, SQLite end-to-end with
capability enforcement, lifecycle/resource cleanup, conformance examples, and packaged
runtime evidence. PostgreSQL and SQL Server JS bridges are deferred until SQLite and the
engine foundation are solid.

The current planning docs split the next work into:

- [ROADMAP MAIN](docs/project/roadmap-main.md): final verification of the minimal
  `sloppyc build` plus `sloppy run --artifacts` alpha path, without recreating completed
  EPIC-21 through EPIC-26 work.
- [ROADMAP MAIN.1](docs/project/roadmap-main-1-hardening.md): hardening skeleton/MVP
  systems to alpha-production quality. This is now historical input to the ENGINE roadmap.
- [Slop Engine final shape](docs/project/slop-engine-final-shape.md): intended engine and
  framework foundation before higher-level framework perks.
- [Slop Engine layered roadmap](docs/project/slop-engine-layered-roadmap.md): Layer 0
  cleanup through Layer 10 public alpha readiness gate.

Public alpha docs remain blocked until the Slop Engine foundation examples and evidence
gates pass or are explicitly deferred with honest exclusions. Benchmarks remain non-claim
evidence only.

See [docs/roadmap.md](docs/roadmap.md),
[docs/project/current-issue-state-audit.md](docs/project/current-issue-state-audit.md),
[docs/project/strategic-current-state-audit.md](docs/project/strategic-current-state-audit.md),
and [docs/project/main-main1-scope.md](docs/project/main-main1-scope.md).

## Core Specs

- [Architecture](docs/architecture.md)
- [Compiler and execution model](docs/execution-model.md)
- [Developer ergonomics](docs/developer-ergonomics.md)
- [Platform abstraction](docs/platform-abstraction.md)
- [App Plan](docs/app-plan.md)
- [Data providers](docs/data-providers.md)
- [Quality gates](docs/quality-gates.md)
- [Roadmap](docs/roadmap.md)

## Developer Workflow

Canonical Windows workflow:

```powershell
.\tools\windows\bootstrap.ps1
.\tools\windows\dev.ps1 configure
.\tools\windows\dev.ps1 build
.\tools\windows\dev.ps1 test
.\tools\windows\dev.ps1 format-check
.\tools\windows\dev.ps1 lint
```

Rust compiler-tool gates:

```powershell
cargo fmt --manifest-path compiler/Cargo.toml -- --check
cargo clippy --manifest-path compiler/Cargo.toml -- -D warnings
cargo test --manifest-path compiler/Cargo.toml
```

Run these from a shell with the MSVC and Windows SDK developer environment initialized.
The in-repo Windows wrapper attempts to import/supplement that environment where possible.

Experimental local package smoke:

```powershell
.\tools\windows\package.ps1 -Configuration Release -Smoke
```

This creates an ignored archive under `artifacts/packages/`, writes `SHA256SUMS.txt`, and
extracts the ZIP outside the checkout to run basic `sloppy` and `sloppyc` CLI smoke checks.
It is not a public release workflow and does not prove V8 execution or live provider
availability. Linux/macOS TAR package smoke has local tooling under `tools/unix/`, but it
is not a required CI gate on hosted runners yet.

## V8 And Providers

Default builds leave V8 disabled. Passing default CTest does not prove the V8 bridge or
handwritten V8 execution path passed. V8 work must be validated with an approved SDK and
reported separately. The GitHub Actions V8 job is manual and reports skipped/not configured
unless a runner-local SDK path is supplied.

PostgreSQL and SQL Server live tests are also gated. Default tests cover non-live/provider
diagnostic behavior; they do not prove a live database server path unless the relevant
environment variables were configured and reported. Linux/macOS default CI does not make
SQL Server ODBC live execution mandatory.

## Agent-First Development

Sloppy is intentionally built with Codex, but not casually. Repo-local docs are the system
of record, `AGENTS.md` is the map, and quality gates enforce standards that should not
depend on chat memory. Repeated review feedback should become docs, checks, or tools.

MVP means narrow, not bad. Runtime features should land only with the supporting standards,
tests, diagnostics, docs, and tooling.
