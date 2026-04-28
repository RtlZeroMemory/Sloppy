# Sloppy

AI-slop branding, zero-slop architecture.

Sloppy is a planned TypeScript application runtime with a C host kernel, a tiny isolated
C++ V8 engine bridge, and a Rust compiler/build tool named `sloppyc`. The runtime is meant
to feel closer to an app host inspired by ASP.NET Core Minimal APIs than to a JavaScript
platform compatibility layer.

Sloppy is not a Node clone, Bun clone, Deno clone, or Express clone. It will expose custom
Sloppy application APIs, compile an application into a Sloppy Plan, and let the native host
own routing, services, permissions, diagnostics, and request lifecycle behavior.

Windows x64 is the first-class development path; Sloppy is cross-platform by design. The
initial developer loop is Windows, `clang-cl`, `lld-link`, CMake, Ninja, vcpkg manifest mode
for ordinary C dependencies, and prebuilt V8 SDK artifacts later. Platform-specific API
calls must be isolated behind `src/platform/*`.

## Why Sloppy?

Sloppy is an experiment in disciplined AI-assisted systems engineering. The joke is slop;
the standards are not.

The main product wedge is developer ergonomics. Sloppy aims to make TypeScript backend apps
feel designed, not assembled from runtime primitives, npm debris, and framework soup.
Performance matters, but the app-host model is the reason the project exists.

Sloppy is an app-host/framework-centric runtime. The intended default path is builder, app,
routes, route groups, `Results.*`, validation shape, modules, services, config, logging,
data providers, diagnostics, and Sloppy Plan-powered tooling. A raw low-level fetch callback
is not the primary application model.

Windows x64 is the first-class development path. Sloppy is cross-platform by design:
platform-specific API calls belong under `src/platform/*`, and core runtime modules must
remain portable C.

Before implementation starts, the docs are the source of truth. Future GitHub EPICs, PR
prompts, and reviewer prompts should map back to these specifications.

Core specs:

- [Architecture](docs/architecture.md);
- [Compiler and execution model](docs/execution-model.md);
- [Developer ergonomics](docs/developer-ergonomics.md);
- [Platform abstraction](docs/platform-abstraction.md);
- [Agent harness](docs/agent-harness.md);
- [C standards](docs/c-standards.md);
- [Quality gates](docs/quality-gates.md);
- [Roadmap](docs/roadmap.md).

Planned user API shape:

```ts
import { Sloppy, Results } from "sloppy";

const builder = Sloppy.createBuilder();
const app = builder.build();

app.mapGet("/", () => Results.text("Sloppy is alive"));

await app.run();
```

## Sloppy Modules And Data Providers

Sloppy applications will be composed from declarative modules that contribute services,
routes, middleware, permissions, schemas, health checks, and metadata to the Sloppy Plan.
Database providers are modules too.

Planned data provider shape:

```ts
import { sqlite } from "sloppy:data/sqlite";

builder.addModule(sqlite.module({ token: "data.main", path: "app.db" }));
```

SQLite is planned as the first built-in/static provider later. PostgreSQL and SQL Server are
planned provider modules, through libpq and Microsoft ODBC Driver/ODBC respectively. None
of this is v0 foundation runtime work yet.

## What Works Today

- placeholder `sloppy` CLI;
- placeholder `sloppyc` CLI;
- CMake/Ninja build skeleton;
- Rust compiler-tool skeleton;
- PowerShell developer tooling;
- docs, ADRs, and CI skeleton.

## What Intentionally Does Not Work Yet

- V8 integration;
- TypeScript compilation;
- HTTP host;
- routing;
- app modules;
- database providers;
- Sloppy Plan loading.

Current repository status: foundation/spec phase only. Runtime features, V8 integration,
HTTP, routing, SQLite, services, and compiler logic are intentionally not implemented yet.

## Agent-First Development

Sloppy is intentionally built with Codex, but not casually. Repo-local docs are the system
of record, `AGENTS.md` is the map, and quality gates enforce the standards that should not
depend on chat memory. Repeated review feedback should become docs, checks, or tools.

Contributor bootstrap:

```powershell
.\tools\bootstrap.ps1
.\tools\dev.ps1 configure
.\tools\dev.ps1 build
.\tools\dev.ps1 test
```

Run these from a shell with the MSVC and Windows SDK developer environment initialized.
CI does this with a Visual Studio developer command setup step.

The project policy is simple: MVP means narrow, not bad. Feature work should land only
after the supporting standards, tests, diagnostics, and tooling are in place.
