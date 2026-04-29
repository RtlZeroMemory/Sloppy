# CLI

Status: Planned / not implemented yet.

Purpose: document future Sloppy CLI commands for build, run, routes, doctor, audit, and
OpenAPI generation.

Planned commands:

```powershell
sloppy routes
sloppy doctor
sloppy audit
sloppy openapi
```

These commands are aspirational and not currently implemented beyond placeholder CLI
smoke behavior.

Benchmarks are currently exposed through `tools/windows/bench.ps1` and the native
`sloppy_bench` CMake target, not through the public `sloppy` CLI.

Related internal docs: `docs/developer-ergonomics.md`, `docs/app-plan.md`,
`docs/compiler.md`.
