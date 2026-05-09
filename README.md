# Sloppy

> **Pre-alpha** · Experimental JavaScript/TypeScript backend runtime

Sloppy is an experimental backend runtime with a different philosophy from Node.js, Bun, or Deno. It is not trying to become a drop-in Node replacement. The goal is different: Sloppy aims to become a serious, self-sufficient backend framework/runtime where the core features needed to build applications are provided by Sloppy itself, instead of being scattered across third-party packages.

The main difference is that **Sloppy is designed to understand your application before it runs it.**

Instead of treating your app as arbitrary JavaScript discovered only at runtime, Sloppy compiles supported source into runtime artifacts. During that step, the compiler can extract routes, handlers, configuration requirements, provider usage, validation metadata, diagnostics, and other app-level intentions into an **application Plan**. That Plan gives the runtime something concrete to validate, inspect, package, and execute.

---

## Why It Exists

Modern backend development has great ideas, but the JavaScript ecosystem often makes you assemble too much yourself. Routing, validation, configuration, dependency injection, diagnostics, database access, OpenAPI, background work, packaging, and testing usually come from many different libraries with different assumptions.

Sloppy is an experiment in taking a more framework-shaped approach. The inspiration is closer to **ASP.NET Core Minimal APIs** than to Express-style middleware soup: one app host, explicit configuration, typed handlers, first-party diagnostics, provider integration, and a runtime that knows what kind of application it is hosting.

The goal is developer ergonomics without giving up runtime structure.

A Sloppy app should eventually feel simple to write:

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/hello/{name}", (ctx) =>
    Results.json({ hello: ctx.route.name })
);

export default app;
```

But under that simple surface, Sloppy wants stronger machinery:

- A compiler that extracts app structure
- A Plan that describes what the app needs
- A native runtime that validates and hosts the app
- First-party framework features instead of dependency sprawl
- Diagnostics designed into the system rather than bolted on later

---

## What Is Different

Sloppy is built around a **compile-first runtime model**.

`sloppyc` compiles supported source into:

```
app.plan.json
app.js
app.js.map
```

`sloppy` can run source input directly or run prebuilt artifacts. Source input is still compiled first. The runtime executes artifacts, not the original source tree.

```
Node/Bun/Deno style:
  run source → discover behavior while executing

Sloppy style:
  compile source → produce Plan/artifacts → validate → execute known app shape
```

That makes Sloppy less flexible in the short term, but much more interesting as a framework/runtime experiment.

**Hard boundaries in the current architecture:**

- The C runtime kernel owns Plan loading, routing, diagnostics, providers, and platform IO
- The V8 bridge is isolated behind the engine boundary instead of leaking V8 types through the runtime
- Sloppy uses its own app-host model instead of depending on Node.js, Bun, Deno, npm, or `node_modules` compatibility
- npm may become an installation channel for the Sloppy CLI, but Sloppy apps do not currently resolve arbitrary npm packages

---

## Current Status

Sloppy is **pre-alpha**.

- The **Windows x64** developer path is currently the most polished. Linux and macOS are product targets, but their local workflows are less complete today.
- Handler execution requires a V8-enabled runtime. Without V8, Sloppy can still build and validate artifacts, but it cannot execute JavaScript handlers.
- **SQLite** is the best default local database path right now. PostgreSQL and SQL Server examples require external database setup and driver/service configuration.
- Production hardening, public release packaging, application dependency support, and several higher-level framework features are still future work.

---

## Install or Try

If `sloppy` is already on your `PATH`, start here:

- [Tutorial: Build your first Sloppy API](#)

Local archive installs are covered here:

- [Install Sloppy](#)

Contributors building from the repository should use:

- [Building from source](#)

---

## First App

**1. Create `sloppy.json`:**

```json
{
  "entry": "src/main.ts",
  "outDir": ".sloppy",
  "environment": "Development"
}
```

**2. Create `src/main.ts`:**

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/health", () => Results.text("ok")).withName("Health.Get");

app.get("/hello/{name}", (ctx) =>
    Results.json({ hello: ctx.route.name })
).withName("Hello.Get");

export default app;
```

**3. Build artifacts:**

```powershell
sloppy build
```

This creates:

```
.sloppy/
  app.plan.json
  app.js
  app.js.map
```

**4. Run one request:**

```powershell
sloppy run --artifacts .sloppy --once GET /hello/Ada
```

Expected response:

```json
{"hello":"Ada"}
```

You can also use source input directly — it still compiles first, then runs the emitted artifacts:

```powershell
sloppy run src/main.ts --once GET /hello/Ada
```

---

## Project Shape

```
my-api/
  sloppy.json        ← entry point and artifact output location
  appsettings.json
  src/
    main.ts          ← app entrypoint
    users.ts
  .sloppy/           ← generated runtime artifacts (not hand-written source)
```

During pre-alpha, Sloppy follows supported Sloppy imports and supported relative modules. It does not resolve `node_modules` or arbitrary npm package imports.

---

## Repository Structure

| Path | Purpose |
|------|---------|
| `src/` | C runtime kernel, app host, platform boundary, data providers, and V8 bridge integration points |
| `compiler/` | Rust `sloppyc` compiler built around Oxc parsing and Sloppy artifact emission |
| `stdlib/sloppy/` | JavaScript bootstrap/runtime support library and public framework surface |
| `examples/` | Runnable or compile-checked application examples |
| `tests/` | Native, conformance, source-input, package, and fixture tests |
| `tools/` | Windows and Unix contributor scripts |
| `docs/` | Product, contributor, and internals documentation |

---

## What Works Today

- Compile supported JavaScript/TypeScript source into Sloppy artifacts
- Run prebuilt artifacts through the app host
- Execute handlers with a V8-enabled runtime
- Extract route and handler metadata into the Plan
- Support a focused app model with routes, groups, results, configuration, validation metadata, DI/service concepts, and provider metadata
- Run SQLite-backed examples locally
- Run PostgreSQL and SQL Server checks when external services are configured
- Package local runtime archives for development and testing workflows

## Current Limits

- Production hardening is future work
- Handler execution requires V8
- Linux and macOS workflows need more validation
- Package archives are still local/experimental
- npm app dependency resolution is not part of the current runtime
- `node_modules` is not resolved by Sloppy apps
- PostgreSQL and SQL Server examples require external database setup
- Framework features such as CORS, global error handling, middleware/filter pipeline, OpenAPI completion, health checks, and richer app testing are being built toward public pre-alpha

---

## Documentation

- [Documentation home](#)
- [Tutorials](#)
- [How-to guides](#)
- [Reference](#)
- [Explanation](#)
- [Contributor docs](#)
- [Internals](#)
- [Glossary](#)

---

## Contributing

- Human contributor guide: [CONTRIBUTING.md](./CONTRIBUTING.md)
- Agent-specific operating rules: [AGENTS.md](./AGENTS.md) · [AGENTS_CONTRIBUTING.md](./AGENTS_CONTRIBUTING.md)

---

## License

See [LICENSE.md](./LICENSE.md).
