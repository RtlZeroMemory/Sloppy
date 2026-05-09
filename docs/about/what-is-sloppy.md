# What is Sloppy?

Sloppy is a TypeScript backend runtime built around the idea that the
runtime should know what your application is *before* it runs it.

Concretely, Sloppy is three pieces working together:

- **`sloppyc`** — a compiler written in Rust. It reads your source, extracts
  routes, configuration, capabilities, and provider usage into a JSON
  document called the **Plan**, and emits a JavaScript bundle.
- **`sloppy`** — the runtime CLI. A C kernel that loads the Plan, validates
  it, wires up providers and capabilities, and dispatches HTTP requests
  through an isolated V8 bridge.
- **A first-party stdlib** — `Sloppy.create()`, `Results.*`, `data.sqlite`,
  services, config, logging, capabilities. The pieces an HTTP backend
  needs, in one import.

You write code that looks like a small framework:

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/hello/{name}", (ctx) =>
    Results.json({ hello: ctx.route.name })
);

export default app;
```

Underneath that surface, the compiler is reading the file and writing down
everything it can prove about the app. By the time a request arrives, the
runtime already knows the route table, which capabilities are needed, and
which provider implementations to wire up.

## What it isn't

- **Not Node, Bun, or Deno.** Sloppy doesn't aim to run arbitrary npm
  packages. It runs Sloppy applications.
- **Not a library you bolt onto an existing app.** It's a runtime; you
  invoke `sloppy run`, not `node`.
- **Not production-hardened yet.** Pre-alpha.

## What it inspires from

The developer experience target is closer to **ASP.NET Core Minimal APIs**
than to Express middleware soup. One app host, explicit configuration,
typed handlers, first-party diagnostics, provider integration. The runtime
shape is its own — the compiler-driven Plan model is the differentiator.

## Where it is today

Real:

- Compiles a focused TypeScript/JS subset into deterministic Plans.
- Runs handlers through an isolated V8 bridge.
- SQLite end-to-end, with PostgreSQL and SQL Server in opt-in lanes.
- Bounded HTTP/1.1 server, HTTP/2 server over TLS ALPN, h2c prior knowledge
  and Upgrade handling, `HttpClient` h2/h2c support with pooled multiplexing
  and HTTPS `auto` ALPN selection, OpenSSL TLS plumbing, capability declarations,
  service injection, structured logging,
  configuration with typed binding.

Pre-alpha:

- Production hardening (long graceful drain, broader TLS posture).
- The app-host feature surface and compiler subset are intentionally narrow:
  middleware, CORS, request IDs, request logging, controllers, health checks,
  ProblemDetails, services, config, and typed providers have static compiler
  coverage where the Plan can encode them; dynamic shapes fail closed.
- OpenAPI is generated from Plan metadata today; security schemes, richer
  response schemas, and full runtime-pipeline modeling are not represented.
- Cross-platform polish: Windows is the most validated lane today.
- Public release distribution (GitHub Release archives, npm launcher).

[Quickstart →](../quickstart.md)
