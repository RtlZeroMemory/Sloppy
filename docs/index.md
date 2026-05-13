---
layout: home

hero:
  name: Sloppy
  text: Compiler-first TypeScript backend runtime
  tagline: Build TypeScript APIs, CLIs, local tools, and packaged app artifacts from a compiler-visible app model. Public alpha.
  image:
    src: /logo.svg
    alt: Sloppy mascot
  actions:
    - theme: brand
      text: Install
      link: /install
    - theme: alt
      text: Quickstart
      link: /quickstart
    - theme: alt
      text: Examples
      link: /guide/examples
    - theme: alt
      text: API
      link: /api/
    - theme: alt
      text: GitHub
      link: https://github.com/RtlZeroMemory/Slop

features:
  - icon: ⚙️
    title: Framework
    details: App host, routing, middleware, results, OpenAPI, auth, sessions, security headers, and health checks.
    link: /guide/project-layout
    linkText: Build apps
  - icon: 🔌
    title: Core APIs
    details: First-party stdlib for data, services, config, logging, workers, filesystem, network, time, crypto, codec, and schema.
    link: /api/
    linkText: API reference
  - icon: 🧪
    title: Testing
    details: TestHost and TestServices provide first-party integration testing for routes, services, and providers.
    link: /api/testing
    linkText: How to test
  - icon: 🚀
    title: Operations
    details: CLI tools for build, run, package, routes, doctor, audit, openapi, and db migrations.
    link: /cli/
    linkText: CLI overview
  - icon: 📚
    title: Examples
    details: Templates and reference apps that exercise routes, providers, packaging, and testing end-to-end.
    link: /guide/examples
    linkText: Browse examples
  - icon: 🧭
    title: Reference
    details: Stability matrix, plan format, supported syntax, configuration keys, providers, and dependencies.
    link: /reference/stability
    linkText: Look it up
  - icon: 🏛
    title: Architecture
    details: How the Rust compiler, Plan artifact, native runtime, and isolated V8 bridge fit together.
    link: /internals/architecture
    linkText: Internals
  - icon: 📦
    title: Release
    details: Artifact contract, install verification, supported platforms, and known limitations for the alpha.
    link: /release/
    linkText: Release docs
---

<div class="sloppy-home">
  <div class="row cols-2">
    <div class="panel">
      <h3>Built for TypeScript</h3>
      <p>
        Author handlers in TypeScript. <code>sloppyc</code> reads supported
        source, extracts routes, providers, capabilities, services, and
        response shapes, and emits a deterministic Plan the runtime executes.
      </p>
      <p style="margin-top:10px">
        For editor IntelliSense, install the package as a dev dependency in
        your app workspace:
      </p>
      <p style="margin-top:6px"><code>npm install --save-dev @slopware/sloppy@alpha</code></p>
      <p style="margin-top:10px">
        <code>@slopware/sloppy</code> ships TypeScript declarations
        (<code>.d.ts</code>) for the <code>sloppy</code> entry and supported
        subpath imports. Editor autocomplete and hover come from your project's
        <code>tsconfig.json</code>. Sloppy compiler diagnostics are reported
        separately by <code>sloppy build</code>; a dedicated Sloppy language
        server is not part of the alpha.
      </p>
    </div>
    <div class="panel ts-card">
      <div class="ts-mark">TS</div>
      <div>
        <h3>Typed handlers, static metadata</h3>
        <p>
          Typed parameter bindings like <code>Route&lt;T&gt;</code>,
          <code>Body&lt;T&gt;</code>, <code>Service&lt;T&gt;</code>, and
          <code>Sqlite&lt;"main"&gt;</code> drive Plan extraction and OpenAPI
          output.
        </p>
      </div>
    </div>
  </div>
</div>

## Try it in 30 seconds

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
npm install -g @slopware/sloppy@alpha
sloppy create my-api --template minimal-api
cd my-api && sloppy build
sloppy run .sloppy --once GET /health
```

## Included backend batteries

<div class="sloppy-home">
  <div class="batteries">
    <a class="battery" href="/Slop/api/routing"><span class="dot"></span>Routing &amp; Results</a>
    <a class="battery" href="/Slop/api/middleware"><span class="dot"></span>Middleware pipeline</a>
    <a class="battery" href="/Slop/guide/auth"><span class="dot"></span>Auth &amp; sessions</a>
    <a class="battery" href="/Slop/api/cors"><span class="dot"></span>CORS &amp; security headers</a>
    <a class="battery" href="/Slop/api/request-logging"><span class="dot"></span>Request logging &amp; context</a>
    <a class="battery" href="/Slop/api/config"><span class="dot"></span>Config &amp; services</a>
    <a class="battery" href="/Slop/api/logging"><span class="dot"></span>Structured logging</a>
    <a class="battery" href="/Slop/api/data"><span class="dot"></span>Data providers &amp; migrations</a>
    <a class="battery" href="/Slop/cli/openapi"><span class="dot"></span>OpenAPI emission</a>
    <a class="battery" href="/Slop/api/health"><span class="dot"></span>Health, metrics &amp; management</a>
    <a class="battery" href="/Slop/api/static-files"><span class="dot"></span>Static files</a>
    <a class="battery" href="/Slop/api/realtime"><span class="dot"></span>Realtime &amp; WebSockets</a>
    <a class="battery" href="/Slop/api/workers"><span class="dot"></span>Workers &amp; jobs</a>
    <a class="battery" href="/Slop/guide/program-mode"><span class="dot"></span>Program Mode</a>
    <a class="battery" href="/Slop/api/testing"><span class="dot"></span>TestHost &amp; TestServices</a>
  </div>
  <p class="batteries-note">
    Exact support boundaries and current alpha limits are tracked in the
    <a href="/Slop/reference/stability">stability matrix</a>.
  </p>
</div>

## The App Loop

<div class="sloppy-home">
<div class="panel loop">

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

</div>
</div>

That loop creates a project, compiles the supported source into `.sloppy/`
artifacts, applies the template SQLite migration, inspects routes, runs
requests through the runtime, generates OpenAPI from Plan metadata, writes an
app package, and proves the package can run outside the source checkout.

## What works today

- **Templates:** `api`, `minimal-api`, `program`, `cli`, `package-api`, and
  `node-compat`.
- **CLI:** `create`, `build`, `dev`, `run`, `package`, `routes`, `health`,
  `metrics`, `deps`, `capabilities`, `doctor`, `audit`, `openapi`,
  `db status|migrate`, and `orm migration add|script|status|apply`.
- **Runtime:** Web handler execution and Program Mode entrypoint execution on
  Windows x64, Linux x64 glibc, macOS arm64, and macOS x64 alpha packages.
- **HTTP:** HTTP/1.1, opt-in TLS, and HTTP/2 over TLS ALPN plus h2c.
- **Stdlib:** app host, routing, results, services, config, logging, data,
  workers, filesystem, network, OS, time, crypto, codec, and schema.

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

Supported npm platform packages cover Windows x64, Linux x64 glibc, macOS
arm64, and macOS x64. Linux arm64, Windows arm64, and musl/Alpine Linux are
source-build paths until matching alpha packages ship.

## Examples

Start with [Examples and demo apps](guide/examples.md). The `api`,
`minimal-api`, `program`, `cli`, `package-api`, and `node-compat` templates
are the best first places to copy from.
