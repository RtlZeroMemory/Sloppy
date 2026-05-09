# Sloppy

Sloppy is a pre-alpha TypeScript backend application runtime and app host.

It takes supported TypeScript or JavaScript app source, compiles it into Sloppy
artifacts, validates the app graph, and runs request handlers through the native
runtime.

## Why It Exists

Backend runtimes usually make the application graph a side effect of executing
framework code. Sloppy makes that graph an artifact. Routes, handlers,
providers, capabilities, diagnostics, and runtime limits are extracted into a
Plan before execution so tooling and the host can reason about the app directly.

## What Is Different

- `sloppyc` compiles source into `app.plan.json`, `app.js`, and `app.js.map`.
- `sloppy` runs either source input or prebuilt artifacts.
- The C runtime kernel owns Plan loading, routing, diagnostics, providers, and
  platform IO.
- The V8 bridge is isolated under the engine boundary instead of leaking V8
  types through the runtime.
- Sloppy uses its own app-host model rather than a Node, Bun, Deno, npm, or
  `node_modules` compatibility layer.

## Current Status

Sloppy is pre-alpha. Windows x64 is the most complete validated local
development path today. Linux and macOS remain product targets, but their local
developer workflows are less complete.

Runtime handler execution requires a V8-enabled build or package. Provider,
package, live-database, benchmark, and platform work may need extra setup or
separate checks.

## Install Or Try

If `sloppy` is already on your `PATH`, start with the tutorial:

- [Tutorial: Build your first Sloppy API](docs/tutorials/first-api.md)

Local archive installs are covered here:

- [Install Sloppy](docs/how-to/install-sloppy.md)

Contributors building from the repository should use:

- [Building from source](docs/contributor/building-from-source.md)

## First App

Create `src/main.ts`:

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/health", () => Results.text("ok")).withName("Health.Get");
app.get("/hello/{name}", (ctx) => Results.json({ hello: ctx.route.name }))
    .withName("Hello.Get");

export default app;
```

Build artifacts:

```powershell
sloppy build
```

Run one request:

```powershell
sloppy run --artifacts .sloppy --once GET /hello/Ada
```

Expected body:

```json
{"hello":"Ada"}
```

## Project Structure

Important repository areas:

| Path | Purpose |
| --- | --- |
| `src/` | C runtime kernel, app host, platform boundary, data providers, and V8 bridge integration points. |
| `compiler/` | Rust `sloppyc` compiler built around Oxc parsing and Sloppy artifact emission. |
| `stdlib/sloppy/` | JavaScript bootstrap/runtime support library and public framework surface. |
| `examples/` | Runnable or compile-checked application examples. |
| `tests/` | Native, conformance, source-input, package, and fixture tests. |
| `tools/` | Windows and Unix contributor scripts. |
| `docs/` | Product, contributor, and internals documentation. |

## What Works Today

- Production hardening is still future work.
- Runtime handler execution requires V8.
- Package archives are local/experimental distribution artifacts.
- The npm launcher package is not a general application dependency manager.
- Third-party app package resolution is outside the current runtime surface.
- V8 execution, package archives, live providers, stress tests, fuzzing, and
  benchmarks use separate setup from the default non-V8 build.

## Docs

- [Documentation home](docs/README.md)
- [Tutorials](docs/tutorials/)
- [How-to guides](docs/how-to/)
- [Reference](docs/reference/)
- [Explanation](docs/explanation/)
- [Contributor docs](docs/contributor/)
- [Internals](docs/internals/)
- [Glossary](docs/glossary.md)

## Contributor Workflow

Human contributor guide:

- [CONTRIBUTING.md](CONTRIBUTING.md)

Agent-specific operating rules:

- [AGENTS.md](AGENTS.md)
- [AGENTS_CONTRIBUTING.md](AGENTS_CONTRIBUTING.md)

## License

[LICENSE.md](LICENSE.md)
