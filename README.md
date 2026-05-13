<p align="center">
  <img src="docs/public/logo.svg" alt="Sloppy logo" width="160" />
</p>

# Sloppy

[![CI](https://github.com/RtlZeroMemory/Slop/actions/workflows/ci.yml/badge.svg)](https://github.com/RtlZeroMemory/Slop/actions/workflows/ci.yml)
[![CodeQL](https://github.com/RtlZeroMemory/Slop/actions/workflows/codeql.yml/badge.svg)](https://github.com/RtlZeroMemory/Slop/actions/workflows/codeql.yml)
[![npm alpha](https://img.shields.io/npm/v/@slopware/sloppy/alpha?label=npm%20alpha)](https://www.npmjs.com/package/@slopware/sloppy)
![public alpha](https://img.shields.io/badge/status-public%20alpha-yellow)
![license](https://img.shields.io/badge/license-see%20LICENSE-blue)

> Compiler-first TypeScript backend runtime and application framework for APIs, tools, and packaged app artifacts.

Sloppy is a TypeScript runtime built around a compiler-visible app model. You
write supported TypeScript, `sloppyc` lowers the source into a structured Plan,
and the native runtime executes that known shape through an isolated V8 bridge.

Sloppy has two current execution shapes:

- **Web apps** - routes, middleware, Results, OpenAPI, HTTP runtime, and app
  metadata.
- **Program Mode** - route-free console-style tools with `main(args, ctx)`,
  stdlib imports, packaging, and artifact execution.

The Plan contains the parts a backend runtime usually has to discover while the
process is already running: routes, handlers, configuration, capabilities,
auth requirements, middleware, response metadata, providers, and generated
artifacts.

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/health", () => Results.text("ok"));

app.get("/hello/{name}", (ctx) =>
    Results.json({ hello: ctx.route.name })
);

export default app;
```

```sh
sloppy build
sloppy run .sloppy --once GET /hello/Ada
```

```json
{"hello":"Ada"}
```

Program Mode is the route-free shape for small tools and local programs:

```ts
export async function main(args, ctx) {
    console.log(`hello ${args[0] ?? "world"}`);
}
```

```sh
sloppy run src/main.ts -- Ada
```

More detail: [Program Mode](docs/guide/program-mode.md).

Sloppy is public alpha software. The current package is useful for
experiments, examples, and feedback while APIs and artifact formats continue
to settle.

## Start here

The GitHub Pages site is deployed from `main` by the Pages workflow. During
review, or immediately after a docs change merges, the same source pages are
available under [`docs/`](docs/README.md).

- Docs site: <https://rtlzeromemory.github.io/Slop/>
- Quickstart: <https://rtlzeromemory.github.io/Slop/quickstart>
- Install: <https://rtlzeromemory.github.io/Slop/install>
- Tutorials: <https://rtlzeromemory.github.io/Slop/tutorials/>
- API reference: <https://rtlzeromemory.github.io/Slop/api/>
- Sloppy vs Node/Bun/Deno: <https://rtlzeromemory.github.io/Slop/about/sloppy-vs-node-bun-deno>
- Roadmap: <https://rtlzeromemory.github.io/Slop/roadmap>

## Install

Install the public alpha package:

```sh
npm install -g @slopware/sloppy@alpha
```

For editor IntelliSense in an app workspace, install the same package locally
as a development dependency:

```sh
npm install --save-dev @slopware/sloppy@alpha
```

Create, build, inspect, and run the recommended API starter:

```sh
sloppy create my-api
cd my-api
sloppy build
sloppy db migrate .sloppy --provider main
sloppy routes .sloppy
sloppy run .sloppy --once GET /health
sloppy run .sloppy --once GET /users
sloppy package
sloppy run .sloppy/package --once GET /health
```

For an edit-refresh loop from the same project:

```sh
sloppy dev
```

`sloppy dev` rebuilds watched inputs and restarts the local server after
successful builds.

Create and package a route-free Program Mode tool:

```sh
sloppy create hello-tool --template program
cd hello-tool
sloppy run src/main.ts -- --name Ada
sloppy build
sloppy package
sloppy run .sloppy/package -- --name Ada
```

Windows x64, Linux x64 glibc, and macOS are the runtime targets for this
alpha. macOS arm64 and macOS x64 are supported macOS lanes. Linux arm64 and
Windows arm64 do not have alpha npm platform packages yet; use source builds
there.

More detail: [Install](docs/install.md).

## Why Sloppy exists

Most TypeScript backends assemble routing, validation, configuration,
dependency injection, logging, OpenAPI, and data access from many packages with
different assumptions. The runtime learns what the app is by importing files
and watching registration side effects happen.

Sloppy explores a different shape:

- one first-party app host and stdlib;
- one compiler step that writes down the app shape before execution;
- one native runtime that validates the Plan before dispatch;
- tooling that reads app metadata instead of reverse-engineering a live
  process.

Current web-app features include routing, Results, middleware, CORS, health
checks, JWT/API-key/session auth, config metadata, OpenAPI, data providers,
ORM, cache, Redis, outbound HTTP clients, background jobs,
package/dependency graph inspection, and Plan-based audit/doctor commands.

The developer experience is closer to ASP.NET Core Minimal APIs than to an
Express-style middleware stack. The runtime model is its own: a C kernel,
Rust compiler, and V8 isolated behind an explicit bridge.

Sloppy is not full Node compatibility, Bun compatibility, or a drop-in
framework for an existing app. A Sloppy app imports the Sloppy stdlib, may
bundle compatible installed JavaScript packages, and runs with the `sloppy`
CLI.

For a practical comparison, see
[Sloppy vs Node, Bun, and Deno](docs/about/sloppy-vs-node-bun-deno.md).

## What works today

Sloppy ships a first-party backend stack in the runtime — no separate router,
DI container, validation library, OpenAPI plugin, request-logging middleware,
or test harness to wire up:

- routing &amp; results, middleware, CORS, security headers, sessions, auth;
- request IDs, request logging, request context, ProblemDetails;
- config &amp; services, structured logging, health, metrics, management;
- data providers (SQLite, PostgreSQL, SQL Server), cursors, ORM, and `sloppy db` migrations;
- memory/provider/Redis-backed cache, output cache, and cache headers;
- first-party Redis client, Redis-backed cache, and Redis locks;
- named and typed HTTP clients with TestHost mocks and resilience policy;
- OpenAPI emission, static files, realtime &amp; WebSockets, webhooks, workers, durable jobs;
- Program Mode for route-free tools and packaged app artifacts;
- TestHost and TestServices for first-party integration testing.

Surface-by-surface support and current alpha boundaries are tracked in
[docs/reference/stability.md](docs/reference/stability.md).

- **App and routing.** `Sloppy.create()`, route registration, route groups,
  controller-style classes, route parameters, query/header/body bindings, and
  generated route metadata.
- **Middleware and request pipeline.** `app.use`, group middleware, CORS,
  request IDs, request logging, ProblemDetails, and health checks for the
  compiler-supported static shapes.
- **Results and OpenAPI metadata.** `Results.text`, `json`, `status`,
  `problem`, `bytes`, `html`, and related helpers feed response metadata into
  the Plan.
- **Services, config, and logging.** Singleton/scoped/transient services,
  typed config binding, `appsettings.{Environment}.json`, secret redaction,
  and structured logging.
- **Data providers.** SQLite is embedded and has the strongest end-to-end path.
  PostgreSQL and SQL Server are optional provider features with metadata,
  provider-specific diagnostics, and opt-in live lanes for apps that use those
  databases. Cursor APIs support incremental reads without materializing the
  whole result set.
- **Cache and Redis.** Sloppy includes bounded memory cache, provider-backed
  distributed caches, Redis-backed cache, output cache metadata, a first-party
  Redis client, and Redis single-key locks. Redis runtime use requires a Redis
  server and the Sloppy outbound network bridge.
- **Durable jobs.** `sloppy/jobs` provides provider-backed jobs, retries,
  worker leases, recurring five-field UTC cron schedules, distributed locks,
  and a SQLite admin CLI.
- **Outbound HTTP.** `Http` provides named and typed clients, generated clients
  from OpenAPI, retry/circuit-breaker/bulkhead policy, service registration,
  and TestHost mocks over the lower-level `HttpClient` transport.
- **HTTP runtime.** HTTP/1.1, bounded keep-alive, opt-in TLS, and experimental
  HTTP/2 over TLS ALPN plus h2c prior knowledge and Upgrade handling.
- **Network client.** `HttpClient` supports HTTP/1.1, explicit h2/h2c, pooled
  h2 multiplexing, and HTTPS `auto` ALPN selection where the private outbound
  TLS bridge is available.
- **CLI tooling.** `sloppy create`, `build`, `dev`, `run`, `package`, `routes`,
  `health`, `metrics`, `deps`, `capabilities`, `doctor`, `audit`, `openapi`,
  `db status|migrate`, `orm migration add|script|status|apply`, and `jobs`
  SQLite scheduler administration.
- **Program Mode.** Route-free source files can compile to Program Plans with
  opaque metadata and a generated `main`/default/top-level entrypoint.
  `main(args, ctx)` receives arguments after `--` and a Program context.
  Console output, numeric exit codes, stdlib imports, compatible installed
  pure-JavaScript packages, and `sloppy run .sloppy/package -- ...` are
  supported on the alpha npm platform packages and source builds configured
  with handler execution support.
- **Stdlib.** App host, routing, results, config, services, logging,
  capabilities, data, ORM, cache, Redis, jobs, schema, filesystem, network,
  HTTP clients, OS, process boundary, time, crypto, codec, and workers.
- **Templates and examples.** `api`, `minimal-api`, `program`, `cli`,
  `package-api`, and `node-compat` templates, plus source examples under
  [`examples/`](examples/README.md).

Surface-by-surface status is tracked in
[Stability Reference](docs/reference/stability.md).

## Current limits

Sloppy is public alpha software. Keep these boundaries in mind when you build
with it:

Current limits:

- API and artifact formats may change between alpha releases.
- Package/dependency support consumes existing
  installed pure-JavaScript packages when they can be resolved, transformed,
  bundled, and executed inside Sloppy's runtime boundary. It does not install
  from a registry or solve semver ranges.
- Package resolution supports a documented subset of package.json `exports`,
  `imports`, conditions, subpaths, `main`, and `type`; unsupported export
  shapes fail clearly.
- Sloppy does not support Node native addons or N-API yet. Obvious native
  addon package shapes are rejected with pattern-based diagnostics, but that is
  not a complete native-package classifier.
- Sloppy is not a full Node runtime. Node compatibility is partial and grows
  through explicit shims backed by Sloppy Core APIs; importable stubs do not
  imply full HTTP, socket, or native-addon support.

See [Roadmap](docs/roadmap.md) and the
[Node compatibility roadmap](docs/roadmap/node-compatibility.md) for the
dependency story, native interop, and production-hardening direction.

## Repository layout

| Path | Contents |
| --- | --- |
| `src/` | C runtime kernel, app host, platform boundary, HTTP stack, V8 bridge |
| `compiler/` | Rust `sloppyc` compiler |
| `stdlib/sloppy/` | First-party JavaScript/TypeScript stdlib |
| `examples/` | Example apps and compiler/runtime fixtures |
| `templates/` | `sloppy create` templates |
| `tests/` | Unit, integration, conformance, package, and live-provider tests |
| `tools/` | Windows and Unix build, package, test, and release scripts |
| `docs/` | User, contributor, reference, and internals docs |

## Documentation

- [Quickstart](docs/quickstart.md) - create, build, run, use the dev loop, and package a first API
- [Install](docs/install.md) - npm, source builds, and platform notes
- [Tutorials](docs/tutorials/index.md) - guided app-building path
- [API](docs/api/index.md) - first-party stdlib and app APIs
- [CLI](docs/cli/index.md) - `sloppy` and `sloppyc` commands
- [Guides](docs/guide/index.md) - project layout, examples, troubleshooting
- [Sloppy vs Node, Bun, and Deno](docs/about/sloppy-vs-node-bun-deno.md) - runtime model, code examples, CLI tradeoffs
- [Reference](docs/reference/index.md) - Plan, syntax, stability, config
- [Internals](docs/internals/index.md) - runtime/compiler design notes
- [Roadmap](docs/roadmap.md) - what exists now and what comes later

## Contributing and feedback

Reports from real attempts are useful: install problems, confusing
diagnostics, examples that do not match behavior, missing docs, and small API
paper cuts.

- [CONTRIBUTING.md](CONTRIBUTING.md)
- [Contributor docs](docs/contributor/index.md)
- [Issues](https://github.com/RtlZeroMemory/Slop/issues)
- [Security policy](SECURITY.md)

## License

Sloppy is licensed under the [Apache License, Version 2.0](LICENSE).
