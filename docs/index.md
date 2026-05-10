---
layout: home

hero:
  name: Sloppy
  text: Compiler-first TypeScript backend runtime
  tagline: Write a Sloppy app, compile it into a Plan, and run that known app shape on a native runtime with an isolated V8 bridge.
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
    details: Routes, handlers, config, capabilities, providers, and response metadata are emitted into deterministic artifacts before runtime execution.
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

## The app loop

```sh
npm install -g @rtlzeromemory/sloppy@alpha
sloppy create my-api --template minimal-api
cd my-api
sloppy build
sloppy run .sloppy --once GET /health
sloppy openapi .sloppy
sloppy package --format json
```

That loop creates a project, compiles the supported source into `.sloppy/`
artifacts, runs one request through the runtime, generates OpenAPI from Plan
metadata, and writes an app package.

## What works today

- Templates: `minimal-api`, `full-api`, and `dogfood`.
- CLI: `create`, `build`, `run`, `package`, `routes`, `capabilities`,
  `doctor`, `audit`, and `openapi`.
- Runtime: V8-backed handler execution on Windows x64 and Linux x64 alpha
  packages.
- HTTP: HTTP/1.1, opt-in TLS, and experimental HTTP/2 over TLS ALPN plus h2c.
- Stdlib: app host, routing, results, services, config, logging, data, workers,
  filesystem, network, OS, time, crypto, codec, and schema.

## Pre-alpha limits

Sloppy is for experiments, demos, and feedback right now. APIs and artifacts can
change between alpha revisions. Sloppy apps do not resolve arbitrary npm
dependencies, and Sloppy is not a full Node runtime.

Platform packages are currently Windows x64 and Linux x64. macOS and arm64 are
source-build paths until matching alpha packages are available.

## Examples

Start with [Examples and demo apps](guide/examples.md). The `minimal-api`,
`full-api`, and `dogfood` templates are the best first places to copy from.
