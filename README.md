<img width="1267" height="370" alt="SLOPPY" src="https://github.com/user-attachments/assets/5741c460-6617-4a70-92e0-0e00c4579a4d" />

# Sloppy

[![CI](https://github.com/RtlZeroMemory/Slop/actions/workflows/ci.yml/badge.svg)](https://github.com/RtlZeroMemory/Slop/actions/workflows/ci.yml)
[![CodeQL](https://github.com/RtlZeroMemory/Slop/actions/workflows/codeql.yml/badge.svg)](https://github.com/RtlZeroMemory/Slop/actions/workflows/codeql.yml)
[![npm alpha](https://img.shields.io/npm/v/@rtlzeromemory/sloppy/alpha?label=npm%20alpha)](https://www.npmjs.com/package/@rtlzeromemory/sloppy)
![pre-alpha](https://img.shields.io/badge/status-pre--alpha-yellow)
![license](https://img.shields.io/badge/license-see%20LICENSE-blue)

> Pre-alpha compiler-first TypeScript backend runtime and app framework.

Sloppy is an experimental backend runtime built around a compiler-first app
model. You write supported TypeScript, `sloppyc` lowers the app into a
structured application Plan, and the native runtime executes that known shape
through an isolated V8 bridge.

The Plan contains the parts a backend runtime usually has to discover while the
process is already running: routes, handlers, configuration, capabilities,
middleware, response metadata, providers, and generated artifacts.

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
sloppy run --once GET /hello/Ada
```

```json
{"hello":"Ada"}
```

Funny name. Serious engineering. Pre-alpha means APIs and artifact formats can
change between alpha revisions.

## Start here

The GitHub Pages site is deployed from `main` by the Pages workflow. During
review, or immediately after a docs change merges, the same source pages are
available under [`docs/`](docs/README.md).

- Docs site: <https://rtlzeromemory.github.io/Slop/>
- Quickstart: <https://rtlzeromemory.github.io/Slop/quickstart>
- Install: <https://rtlzeromemory.github.io/Slop/install>
- Tutorials: <https://rtlzeromemory.github.io/Slop/tutorials/>
- API reference: <https://rtlzeromemory.github.io/Slop/api/>
- Roadmap: <https://rtlzeromemory.github.io/Slop/roadmap>

## Install

The public alpha package is:

```sh
npm install -g @rtlzeromemory/sloppy@alpha
```

Create, build, and run a minimal API:

```sh
sloppy create my-api --template minimal-api
cd my-api
sloppy build
sloppy run --once GET /health
```

Windows x64 and Linux x64 are the npm runtime targets for this alpha. macOS and
arm64 are not published as npm platform packages yet; use source builds there.

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

The developer experience is closer to ASP.NET Core Minimal APIs than to an
Express-style middleware stack. The runtime model is its own: a C kernel,
Rust compiler, and V8 isolated behind an explicit bridge.

Sloppy is not Node compatibility, Bun compatibility, or a drop-in framework
for an existing app. A Sloppy app imports the Sloppy stdlib and runs with the
`sloppy` CLI.

## What works today

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
- **Data providers.** SQLite has the strongest end-to-end path. PostgreSQL and
  SQL Server have provider metadata and opt-in live lanes that require local
  services and platform dependencies.
- **HTTP runtime.** HTTP/1.1, bounded keep-alive, opt-in TLS, and experimental
  HTTP/2 over TLS ALPN plus h2c prior knowledge and Upgrade handling.
- **Network client.** `HttpClient` supports HTTP/1.1, explicit h2/h2c, pooled
  h2 multiplexing, and HTTPS `auto` ALPN selection where the private outbound
  TLS bridge is available.
- **CLI tooling.** `sloppy create`, `build`, `run`, `routes`, `capabilities`,
  `doctor`, `audit`, `openapi`, and `package`.
- **Stdlib.** App host, routing, results, config, services, logging,
  capabilities, data, schema, filesystem, network, OS, process boundary, time,
  crypto, codec, and workers.
- **Templates and examples.** `minimal-api`, `full-api`, and `dogfood`
  templates, plus source examples under [`examples/`](examples/README.md).

Surface-by-surface status is tracked in
[Stability Reference](docs/reference/stability.md).

## What pre-alpha means

Sloppy is usable for experiments, demos, and feedback. It is not hardened as a
production edge runtime.

Current limits:

- API and artifact formats can change between alpha revisions.
- Sloppy apps do not resolve arbitrary `node_modules` packages.
- Sloppy is not a full Node runtime. Node globals and built-ins exist only when
  Sloppy implements an explicit API for that behavior.
- The compiler supports a focused source subset. Dynamic app shapes fail closed
  instead of producing partial Plans.
- macOS and arm64 package-manager distribution are not part of this alpha.
- Live PostgreSQL and SQL Server checks need explicit local services and
  drivers.

See [Roadmap](docs/roadmap.md) for the dependency story, Program Mode, native
interop, and production-hardening direction.

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

- [Quickstart](docs/quickstart.md) - create, build, run, and package a first API
- [Install](docs/install.md) - npm, source builds, and platform notes
- [Tutorials](docs/tutorials/README.md) - guided app-building path
- [API](docs/api/README.md) - first-party stdlib and app APIs
- [CLI](docs/cli/README.md) - `sloppy` and `sloppyc` commands
- [Guides](docs/guide/README.md) - project layout, examples, troubleshooting
- [Reference](docs/reference/README.md) - Plan, syntax, stability, config
- [Internals](docs/internals/README.md) - runtime/compiler design notes
- [Roadmap](docs/roadmap.md) - what exists now and what comes later

## Contributing and feedback

Sloppy is pre-alpha. Reports from real attempts are useful: install problems,
confusing diagnostics, examples that do not match behavior, missing docs, and
small API paper cuts.

- [CONTRIBUTING.md](CONTRIBUTING.md)
- [Contributor docs](docs/contributor/README.md)
- [Issues](https://github.com/RtlZeroMemory/Slop/issues)
- [Security policy](SECURITY.md)

## License

See [LICENSE.md](LICENSE.md).
