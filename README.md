<img width="1267" height="370" alt="SLOPPY" src="https://github.com/user-attachments/assets/5741c460-6617-4a70-92e0-0e00c4579a4d" />

# Sloppy

> Pre-alpha · TypeScript backend runtime with a compiler that knows your app

Sloppy is a backend runtime for TypeScript. Most JavaScript runtimes
discover what your app does as it runs. Sloppy compiles supported source
into an **application Plan** first — routes, capabilities, configuration,
provider usage — then runs the app against that known shape.

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/hello/{name}", (ctx) =>
    Results.json({ hello: ctx.route.name })
);

export default app;
```

```
$ sloppy run src/main.ts --once GET /hello/Ada
HTTP/1.1 200 OK
content-type: application/json; charset=utf-8
content-length: 15

{"hello":"Ada"}
```

## Why

The JavaScript backend ecosystem leans on assembling routing, validation,
configuration, DI, diagnostics, database access, and OpenAPI from many
libraries with mismatched assumptions. Sloppy takes the opposite shape —
one app host, first-party stdlib, and a compiler step that gives the
runtime structured knowledge of your app.

Closer to **ASP.NET Core Minimal APIs** than to Express middleware soup.

## Install

Public release archives and npm packages aren't published yet. The supported
local paths today are:

- **Build from source** — full instructions in
  [docs/contributor/building-from-source.md](docs/contributor/building-from-source.md).
  Windows x64 is the most validated lane; Linux builds work; macOS and
  arm64 still need work.
- **Build a local archive** — `.\tools\windows\dev.ps1 package` produces a per-platform
  archive under `artifacts/packages/` that can be extracted and put on
  `PATH`. See [docs/install.md](docs/install.md).
- **Run the local npm proof** — `.\tools\windows\dev.ps1 npm-dry-run` or
  `./tools/unix/dev.sh npm-dry-run --package-path ...` stages
  `@rtlzeromemory/sloppy` and a matching platform package from a tested
  archive, then installs and smokes the local tarballs without publishing.

Public GitHub Release artifacts and npm publication are gated release work.

## A first app

```
sloppy create hello --template minimal-api --no-git
cd hello
```

`src/main.ts`:

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/health", () => Results.text("ok"));
app.get("/hello/{name}", (ctx) =>
    Results.json({ hello: ctx.route.name })
);

export default app;
```

Build, then run a single request:

```
sloppy build
sloppy run --artifacts .sloppy --once GET /hello/Ada
sloppy package
```

Or start a server bound to `127.0.0.1:5173`:

```
sloppy run
```

Full walkthrough: [docs/quickstart.md](docs/quickstart.md).

## What's there today

- A first-party runtime: app, routing, results, services, config, logging,
  capabilities.
- A first-party stdlib: `data.sqlite`, `data.postgres`, `data.sqlserver`,
  schema validation, codec, time, crypto, workers, filesystem, network.
- A compiler that extracts route, handler, capability, and
  framework-metadata into a deterministic Plan.
- An HTTP/1.1 server with bounded keep-alive and opt-in TLS.
- CLI app workflow and introspection: `sloppy create`, `sloppy build`,
  `sloppy run`, `sloppy package`, `sloppy routes`, `sloppy capabilities`,
  `sloppy doctor`, `sloppy audit`, `sloppy openapi`.
- A V8 bridge isolated under `src/engine/v8/` with explicit ownership
  rules.

## What's pre-alpha

- Production hardening (long graceful drain, broader TLS posture, HTTP/2/3).
- Some framework features are app-host only today. Middleware, CORS, request
  IDs, request logging, controllers, health checks, and ProblemDetails run in
  the bootstrap app-host/test-host path; compiler source input rejects the
  surfaces it cannot encode yet.
- OpenAPI is generated from current Plan metadata. Security schemes, richer
  response schemas, CORS, middleware, request IDs, request logging, and
  controller behavior are outside emitted OpenAPI metadata today.
- Public release distribution (GitHub Release archives, npm publication).
- Cross-platform polish: Windows x64 is the most validated lane.
- npm app dependency support — Sloppy apps don't import `node_modules`
  packages, see [why](docs/about/why-no-node-modules.md).

## Documentation

- [Quickstart](docs/quickstart.md) — five-minute first app
- [API](docs/api/README.md) — `Sloppy`, `Results`, routes, services, config, data
- [CLI](docs/cli/README.md) — `build`, `run`, `routes`, `doctor`, …
- [Guides](docs/guide/README.md) — project layout, TypeScript subset, examples
- [Reference](docs/reference/README.md) — Plan format, supported syntax, stability
- [About](docs/about/README.md) — design notes, why no `node_modules`, V8 bridge
- [Internals](docs/internals/README.md) — for people working on Sloppy itself
- [Contributor docs](docs/contributor/README.md) — building from source, tests

## Repository layout

| Path           | Contents                                                          |
| -------------- | ----------------------------------------------------------------- |
| `src/`         | C runtime kernel, app host, platform boundary, V8 bridge          |
| `compiler/`    | Rust `sloppyc` compiler (built around Oxc)                        |
| `stdlib/sloppy/` | First-party JavaScript stdlib                                   |
| `examples/`    | Runnable and fixture-shaped apps                                  |
| `tests/`       | Unit, integration, conformance, fuzz, package, live tests         |
| `tools/`       | Windows and Unix contributor scripts                              |
| `docs/`        | Product, contributor, and internals documentation                 |

## License

See [LICENSE.md](./LICENSE.md).
