---
layout: home

hero:
  name: Sloppy
  text: Compiler-first TypeScript backend runtime
  tagline: Build TypeScript APIs, CLIs, local tools, and packaged app artifacts from a compiler-visible app model.
  image:
    src: /logo.svg
    alt: Sloppy
  actions:
    - theme: brand
      text: Install
      link: /install
    - theme: alt
      text: Quickstart
      link: /quickstart
    - theme: alt
      text: Tutorials
      link: /tutorials/
    - theme: alt
      text: API
      link: /api/
    - theme: alt
      text: Roadmap
      link: /roadmap
    - theme: alt
      text: GitHub
      link: https://github.com/RtlZeroMemory/Slop

features:
  - title: Compiler-first app shape
    details: Routes, handlers, config, capabilities, providers, package dependencies, migrations, and response metadata are emitted into deterministic artifacts before runtime execution.
  - title: First-party backend APIs
    details: Routing, results, config, services, logging, data, workers, filesystem, network, time, crypto, codec, and schema live in the Sloppy stdlib.
  - title: Native runtime, explicit bridge
    details: The runtime core is C, the compiler is Rust, and JavaScript execution goes through a named V8 bridge rather than a Node-compatible host.
---

## Start here

New to Sloppy? Read these in order:

1. [Install Sloppy](install.md)
2. [Create and run a first API](quickstart.md)
3. [Build your first Sloppy API](tutorials/first-api.md)
4. [Generate OpenAPI and package the app](tutorials/openapi-and-package.md)
5. [Check the stability matrix](reference/stability.md)

If you are comparing Sloppy with other JavaScript runtimes, read
[Sloppy vs Node, Bun, and Deno](about/sloppy-vs-node-bun-deno.md).

## The App Loop

```sh
npm install -g @slopware/sloppy@alpha
sloppy create my-api
cd my-api
sloppy build
sloppy db migrate .sloppy --provider main
sloppy routes .sloppy
sloppy run .sloppy --once GET /health
sloppy run .sloppy --once GET /users
sloppy openapi .sloppy
sloppy package
sloppy db migrate .sloppy/package --provider main
sloppy run .sloppy/package --once GET /health
```

That loop creates a project, compiles the supported source into `.sloppy/`
artifacts, applies the template SQLite migration, inspects routes, runs
requests through the runtime, generates OpenAPI from Plan metadata, writes an
app package, and proves the package can run outside the source checkout.

## What works today

- Templates: `api`, `minimal-api`, `program`, `cli`, `package-api`, and
  `node-compat`.
- CLI: `create`, `build`, `dev`, `run`, `package`, `routes`, `deps`,
  `capabilities`, `doctor`, `audit`, `openapi`, and `db status|migrate`.
- Runtime: V8-backed web handler execution and Program Mode entrypoint
  execution on Windows x64, Linux x64 glibc, and macOS alpha packages.
- HTTP: HTTP/1.1, opt-in TLS, and experimental HTTP/2 over TLS ALPN plus h2c.
- Stdlib: app host, routing, results, services, config, logging, data, workers,
  filesystem, network, OS, time, crypto, codec, and schema.

## Why Sloppy?

- [Sloppy vs Node, Bun, and Deno](about/sloppy-vs-node-bun-deno.md) explains
  the compiler-first runtime model with side-by-side route examples.
- [Why `node_modules` is build input](about/why-no-node-modules.md) explains
  the dependency boundary.
- [Compiler-first runtime](about/compiler-first-runtime.md) explains why the
  Plan exists.

## Current limits

Sloppy is public alpha software. APIs and artifacts can change between alpha
revisions. Package support is limited to compatible installed JavaScript that
Sloppy can bundle, and Sloppy is not a full Node runtime.

Platform npm packages cover Windows x64, Linux x64 glibc, and macOS. Linux
arm64 and Windows arm64 are source-build paths until matching alpha packages
are available.

## Examples

Start with [Examples and demo apps](guide/examples.md). The `api`,
`minimal-api`, `program`, `cli`, `package-api`, and `node-compat` templates are
the best first places to copy from.
