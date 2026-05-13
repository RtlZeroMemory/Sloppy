# Architecture

Sloppy is a compiler-first TypeScript runtime with three main programs:

- `sloppyc`, the Rust compiler that reads supported source and emits
  deterministic artifacts.
- `sloppy`, the native runtime and CLI that validates artifacts, owns the app
  lifecycle, drives HTTP, and dispatches handlers.
- the V8 bridge, the C++ boundary that evaluates generated JavaScript and
  calls handler IDs from the native runtime.

```text
source              sloppyc             .sloppy/               sloppy
src/main.ts   ->    compile       ->    app.plan.json     ->   load + validate
                                        app.js                 activate features
                                        app.js.map             initialize V8
                                        deps.graph.json        dispatch requests
```

## Purpose

The architecture exists to make backend app shape visible before execution.
Routes, handlers, configuration, providers, capabilities, auth metadata,
response metadata, dependencies, package assets, and Program Mode entrypoints
are written into artifacts that the runtime can validate before serving or
running code.

This gives Sloppy three useful properties:

- tooling can inspect an app without entering V8;
- startup can fail before any request reaches a malformed or unsupported app;
- native, platform, provider, and JavaScript boundaries stay reviewable.

## Where It Lives

| Layer | Main paths | Owns |
| --- | --- | --- |
| Compiler | `compiler/src/` | Source parsing, supported-syntax checks, app metadata extraction, dependency graph construction, generated JS, Plan emission |
| Plan parser | `src/core/plan_parse.c`, `include/sloppy/plan.h` | Plan JSON parsing, schema validation, feature/provider/capability checks |
| CLI | `src/main.c`, `src/cli/cli_*.inc` | `build`, `run`, `dev`, `package`, `routes`, `deps`, `capabilities`, `doctor`, `audit`, `openapi`, `db`, and `orm` command dispatch |
| App host | `src/core/app_host.c`, `stdlib/sloppy/app.js` | Startup lifecycle, feature activation, app-host APIs, request scopes |
| HTTP runtime | `src/core/http*.c`, `src/platform/libuv/http_transport_libuv.c` | HTTP parser, route dispatch, response writing, transport integration |
| V8 bridge | `src/engine/v8/*` | Isolate ownership, generated bundle evaluation, handler registration, JS/native conversion |
| Providers | `src/data/*`, `stdlib/sloppy/data.js` | SQLite, PostgreSQL, SQL Server native providers and JS provider handles |
| Stdlib | `stdlib/sloppy/*` | Public JavaScript modules such as `Sloppy`, `Results`, `data`, `schema`, `fs`, `net`, `crypto`, and `workers` |
| Platform | `src/platform/*` | OS and libuv boundary code |

## Main Concepts

**Source input** is the TypeScript or JavaScript entrypoint. It can be a web
app that exports a Sloppy app, or a Program Mode module that exports
`main(args, ctx)`, a default function, or top-level program code.

**Plan** is the native contract between compiler and runtime. It records the
artifact kind, runtime minimum version, routes, handlers, features, providers,
capabilities, package dependencies, static assets, migrations, and metadata
used by CLI tools.

**Generated bundle** is the JavaScript that V8 evaluates. Native code never
introspects arbitrary user source at request time; it dispatches named handler
IDs through the bridge.

**Stdlib** is the public JavaScript surface. Public modules stay small where
possible and delegate shared validation, redaction, and lifecycle behavior to
focused internals.

## Lifecycle

Compile time:

```text
entry source
  -> Oxc parse
  -> supported source extraction
  -> dependency and package graph resolution
  -> generated JS + source map
  -> app.plan.json
```

Startup:

```text
sloppy run
  -> read app.plan.json
  -> validate schema, hashes, routes, handlers, providers, and features
  -> activate runtime features
  -> initialize V8 when handler execution is required
  -> evaluate app.js
  -> register handler IDs
```

Request handling:

```text
transport bytes
  -> parse and validate HTTP
  -> match the Plan-derived route table
  -> build request context and request scope
  -> dispatch handler ID through V8
  -> convert Results.* response
  -> write response and clean up scope
```

Program Mode uses the same artifact validation and V8 bridge, but the Plan kind
is `program` and execution enters the generated program entrypoint instead of a
route table.

## Invariants

- Core code does not include OS or libuv details directly. Those live behind
  `src/platform/`.
- V8 types do not cross out of `src/engine/v8/`; public headers use
  Sloppy-owned C types.
- JavaScript receives capability-checked handles, not raw native pointers.
- A V8 isolate has one owner thread. Wrong-thread entry fails before touching
  V8.
- Malformed Plans, unknown handler IDs, unsupported features, and missing
  required artifacts fail before request execution.
- Secrets and provider connection strings are redacted in Plan-backed tooling
  and diagnostics.

## Failure Behavior

Failures become diagnostics at the layer that owns the contract:

| Failure point | Surface |
| --- | --- |
| Source parsing or unsupported source shape | `sloppyc` diagnostic and non-zero exit |
| Plan parse or validation | startup diagnostic before V8 initialization |
| Feature activation | startup diagnostic before the server or program runs |
| V8 initialization or bundle evaluation | startup diagnostic |
| Handler exception | mapped error response with request cleanup |
| Provider failure | provider-specific JS error or startup/tooling diagnostic |
| Transport error | connection closed while the server keeps serving other requests |

Late async completions can clean up resources, but they cannot double-settle a
request or mutate a closed JS state.

## Public API Relationship

Application authors primarily use:

- `Sloppy.create()` and route/group/module APIs;
- `Results.*` response helpers;
- first-party stdlib modules such as `sloppy/data`, `sloppy/schema`,
  `sloppy/fs`, `sloppy/net`, `sloppy/crypto`, and `sloppy/workers`;
- CLI commands that read Plan artifacts, such as `routes`, `deps`,
  `capabilities`, `doctor`, `audit`, and `openapi`.

Internals docs explain how those APIs are implemented. They are not extra API
surface for applications.

## Tests And Evidence

Architecture boundaries are checked through a mix of unit, integration,
conformance, package, and docs gates:

- `tools/windows/check-platform-boundaries.ps1` checks native boundary rules.
- `tools/windows/check-docs-freshness.ps1` keeps required docs present and
  rejects known stale documentation patterns.
- `tests/cmake/check_main_contract_docs.cmake` checks Quickstart and core CLI
  documentation fragments.
- `cmake/SloppySourceInputTests.cmake`,
  `cmake/SloppyCompilerConformanceTests.cmake`, and
  `cmake/SloppyExampleBootstrapTests.cmake` wire source-input, compiler, and
  app-host evidence.
- Provider, HTTP, V8, package, live-provider, stress, sanitizer, fuzz, and
  benchmark lanes are reported separately when they matter.

## Current Limits

- Sloppy supports a bounded source subset rather than arbitrary TypeScript.
  See [supported syntax](../reference/supported-syntax.md).
- Package resolution uses compatible installed JavaScript as build input. It
  does not install registry packages or provide full Node runtime behavior.
- PostgreSQL, SQL Server, Redis, live provider tests, TLS, advanced HTTP
  transport paths, and benchmarks have optional or environment-specific lanes.
- Internal formats can change across alpha revisions. Public docs and the
  stability matrix describe the supported behavior for the current tree.

## Where To Read Next

- [Runtime](runtime.md)
- [Compiler](compiler.md)
- [Plan](plan.md)
- [V8 bridge](v8-bridge.md)
- [HTTP runtime](http-runtime.md)
- [Provider runtime](provider-runtime.md)
- [Memory model](memory-model.md)
- [Platform boundaries](platform-boundaries.md)
- [Security model](security-model.md)
