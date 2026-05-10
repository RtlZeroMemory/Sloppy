<p align="center">
  <img src="docs/public/banner.svg" alt="Sloppy — public alpha TypeScript runtime" width="100%" />
</p>

# Sloppy

[![CI](https://github.com/RtlZeroMemory/Slop/actions/workflows/ci.yml/badge.svg)](https://github.com/RtlZeroMemory/Slop/actions/workflows/ci.yml)
[![CodeQL](https://github.com/RtlZeroMemory/Slop/actions/workflows/codeql.yml/badge.svg)](https://github.com/RtlZeroMemory/Slop/actions/workflows/codeql.yml)
[![npm alpha](https://img.shields.io/npm/v/@rtlzeromemory/sloppy/alpha?label=npm%20alpha)](https://www.npmjs.com/package/@rtlzeromemory/sloppy)
![pre-alpha](https://img.shields.io/badge/status-pre--alpha-yellow)
![license](https://img.shields.io/badge/license-see%20LICENSE-blue)

> Public alpha (pre-production) TypeScript runtime and application framework
> for backend APIs, CLIs, and local programs.

Sloppy is a public alpha, pre-production TypeScript runtime built around a
compiler-first application model. You write supported TypeScript, `sloppyc`
lowers the source
into a deterministic Plan, and the native runtime executes that known app
shape through an isolated V8 bridge. The model is inspired by ASP.NET-style
backend ergonomics: declare what the app is and let the framework wire it up.

Sloppy is compiler-first, but not compiler-restricted. Static code gives the
strongest metadata; dynamic code can still run, with partial metadata recorded
in the Plan.

The runtime kernel is C, the compiler (`sloppyc`) is Rust, and JavaScript
execution goes through a named V8 bridge — there is no Node-compatible host
underneath.

Sloppy has two current execution shapes:

- **Web Mode** — routes, middleware, Results, OpenAPI, HTTP runtime, and app
  metadata.
- **Program Mode** — route-free console tools and local programs with
  `main(args, ctx)`, stdlib imports, packaging, and artifact execution.

"Public alpha, pre-production" means Sloppy is usable for experiments, demos,
and feedback; APIs and artifact formats may change between alpha revisions;
and it is not production-ready.

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

The public alpha package is:

```sh
npm install -g @rtlzeromemory/sloppy@alpha
sloppy --version
```

Published alpha npm platform packages: Windows x64, Linux x64 (glibc), and
macOS arm64 (Apple Silicon). macOS x64, Linux arm64, and Windows arm64 are
source/archive builds. Once installed, `sloppy --version` confirms the CLI
is on `PATH`.

More detail: [Install](docs/install.md).

## Quickstart: API

Create a SQLite-backed API from the default `api` template, build, inspect the
artifacts, run a request, and produce a runnable app package:

```sh
sloppy create my-api
cd my-api
sloppy build
sloppy routes .sloppy
sloppy run .sloppy --once GET /health
sloppy run .sloppy --once GET /users
sloppy package
sloppy run .sloppy/package --once GET /health
```

The packaged app under `.sloppy/package/` runs from a copied artifact layout
without returning to the source checkout.

## Quickstart: Program Mode

Program Mode is the route-free shape for small CLIs and local programs:

```sh
sloppy create my-tool --template cli
cd my-tool
sloppy run src/main.ts -- --help
sloppy package
sloppy run .sloppy/package -- --help
```

Program Mode entrypoints can be `export async function main(args, ctx)` or a
default function. Numeric returns set the process exit code.

More detail: [Program Mode](docs/guide/program-mode.md).

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

Sloppy is not full Node compatibility, Bun compatibility, or a drop-in
framework for an existing app. A Sloppy app imports the Sloppy stdlib, may
bundle compatible installed JavaScript packages, and runs with the `sloppy`
CLI.

For a practical comparison, see
[Sloppy vs Node, Bun, and Deno](docs/about/sloppy-vs-node-bun-deno.md).

## Current features

- **Web apps** — routing, middleware, Results, ProblemDetails, CORS, health
  checks, request IDs, request logging, and OpenAPI metadata.
- **Program Mode** — route-free CLIs and tools with `main(args, ctx)`,
  console output, numeric exit codes, and packaged runs.
- **Templates** — `api`, `minimal-api`, `program`, `cli`, `package-api`, and
  `node-compat` starters via `sloppy create`.
- **Packages and dependency graph** — bundle compatible installed pure-JS
  packages from `node_modules`; `sloppy deps` inspects the sealed graph.
- **Partial Node compatibility** — explicit `node:*` shims (`path`, `events`,
  `url`, `querystring`, `buffer`, `util`, `timers`, `fs`, `os`, `process`,
  `crypto`) backed by Sloppy Core APIs.
- **SQLite and data APIs** — strongest provider path; PostgreSQL and SQL
  Server have typed metadata and opt-in live lanes.
- **HTTP runtime** — HTTP/1.1, opt-in TLS, and experimental HTTP/2 over TLS
  ALPN plus h2c prior knowledge and Upgrade.
- **`HttpClient`** — outbound HTTP/1.1, explicit h2/h2c, pooled h2
  multiplexing, and HTTPS `auto` ALPN selection.
- **FFI foundation** — typed C ABI calls through `sloppy/ffi`, with
  Plan-visible metadata and packaged local native libraries. Unsafe
  boundary; experimental.
- **CLI tooling** — `create`, `build`, `run`, `package`, `routes`, `deps`,
  `capabilities`, `doctor`, `audit`, `openapi`, and `sloppyc`.
- **Stdlib** — app host, routing, results, config, services, logging, data,
  schema, filesystem, network, OS, process boundary, time, crypto, codec, and
  workers.
- **Benchmarks** — local engineering harnesses under `benchmarks/`. They are
  not a public superiority claim.
- **Documentation site** — published from `main` via GitHub Pages.

Surface-by-surface status: [Stability Reference](docs/reference/stability.md).

## Current limits

Sloppy is usable for experiments, demos, and feedback. It is not a production
edge runtime, and it is not a drop-in replacement for Node, Bun, or Deno.

- Public alpha, pre-production. APIs and artifact formats may change between
  alpha revisions.
- Not full Node compatibility. Node API support is partial and grows through
  explicit shims backed by Sloppy Core APIs.
- Not full npm compatibility. Package support is experimental: Sloppy bundles
  compatible installed pure-JavaScript packages, but it does not install from
  a registry, solve semver ranges, or read lockfiles.
- Native Node addons and N-API are unsupported.
- FFI is experimental and unsafe. Wrong signatures or invalid pointers can
  crash the process.
- Benchmarks are local engineering measurements, not a superiority claim.
- Only Windows x64, Linux x64 (glibc), and macOS arm64 ship as published
  alpha npm platform packages. macOS x64, Linux arm64, and Windows arm64
  use source/archive builds.
- Live PostgreSQL and SQL Server checks need explicit local services and
  drivers.

See [Roadmap](docs/roadmap.md) and the
[Node compatibility roadmap](docs/roadmap/node-compatibility.md) for the
dependency story and production-hardening direction.

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

- [Install](docs/install.md) — npm, source builds, and platform notes
- [Quickstart](docs/quickstart.md) — three flows: API, Program, and packaged app
- [Templates](docs/guide/templates.md) — `api`, `minimal-api`, `program`,
  `cli`, `package-api`, `node-compat`
- [Using installed packages](docs/guide/using-packages.md) — bundle
  pure-JavaScript dependencies
- [Node compatibility](docs/reference/node-compatibility.md) — supported
  shims and unsupported builtins
- [Native FFI](docs/reference/ffi.md) — typed C ABI calls (unsafe)
- [Performance](docs/about/performance.md) — what's measured and why no
  competitive numbers are published
- [Roadmap](docs/roadmap.md) — what exists now and what comes later
- [API](docs/api/index.md) — first-party stdlib and app APIs
- [CLI](docs/cli/index.md) — `sloppy` and `sloppyc` commands
- [Sloppy vs Node, Bun, and Deno](docs/about/sloppy-vs-node-bun-deno.md) —
  runtime model and tradeoffs
- [Reference](docs/reference/index.md) — Plan format, syntax, stability,
  config
- [Internals](docs/internals/index.md) — runtime/compiler design notes
- [Contributing](CONTRIBUTING.md) · [Security](SECURITY.md) ·
  [License](LICENSE)

## Contributing and feedback

Sloppy is in public alpha. Reports from real attempts are useful: install problems,
confusing diagnostics, examples that do not match behavior, missing docs, and
small API paper cuts.

- [CONTRIBUTING.md](CONTRIBUTING.md)
- [Contributor docs](docs/contributor/index.md)
- [Issues](https://github.com/RtlZeroMemory/Slop/issues)
- [Security policy](SECURITY.md)

## License

Sloppy is licensed under the [Apache License, Version 2.0](LICENSE).
