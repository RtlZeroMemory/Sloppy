# Examples

`/examples/` has many small apps. This page starts with the ones worth
running by hand to learn real Sloppy idioms — grouped by what you'd want to
build — and ends with a full inventory of every example directory.

If you are starting out, prefer a [template](templates.md) over an example:
templates are starter apps; examples are short demonstrations of a
particular API surface.

For the full test-oriented inventory (including conformance/source-input
catalogs used by the test suite), see `examples/README.md` in the
repository.

## Start here

| Example | Shows |
| --- | --- |
| [`framework-hello`](#framework-hello) | `Sloppy.create()`, typed route param, JSON response |
| [`hello-minimal`](#hello-minimal) | The smallest runnable app |

## Multi-file project

| Example | Shows |
| --- | --- |
| [`prealpha-control-plane`](#prealpha-control-plane) | Multi-file project-mode app host, SQLite capability, app test host coverage |

## Routing

| Example | Shows |
| --- | --- |
| [`framework-controller`](#framework-controller) | Controller class + DI through `static inject` |
| [`framework-explicit-binding`](#framework-explicit-binding) | `Route<T>`, `Query<T>`, `Body<T>`, `Header<>` typed bindings |
| [`request-context`](#request-context) | `ctx.route`, `ctx.query`, `ctx.request.headers`, body parsing |

## Services and config

| Example | Shows |
| --- | --- |
| [`framework-di-services`](#framework-di-services) | Singleton/scoped/transient lifetimes through a request scope |
| [`config-basic`](#config-basic) | `addObject`, typed getters |

## Data

| Example | Shows |
| --- | --- |
| [`framework-sqlite-crud`](#framework-sqlite-crud) | Typed-handler SQLite CRUD with provider injection |
| [`framework-postgres-crud`](#framework-postgres-crud) | Same shape, PostgreSQL provider (needs a live database) |
| [`framework-sqlserver-crud`](#framework-sqlserver-crud) | Same shape, SQL Server provider (needs a live database) |

## Validation

| Example | Shows |
| --- | --- |
| [`framework-validation-errors`](#framework-validation-errors) | Schema-backed body binding, Plan-level validation diagnostics |

## Workers

| Example | Shows |
| --- | --- |
| [`workers-background-service`](#workers-background-service) | Long-running service alongside the HTTP server |
| [`workers-workerpool`](#workers-workerpool) | Bounded pool of worker isolates |

## Stdlib (filesystem, network, OS, time, crypto, codec)

The shortest path to seeing each stdlib module in real source. Each
directory is a small, focused demonstration — not a full tutorial.
Reference docs:
[Filesystem](../api/filesystem.md), [Network](../api/network.md),
[HTTP Client](../api/http-client.md), [OS](../api/os.md),
[Time](../api/time.md), [Crypto](../api/crypto.md),
[Codec](../api/codec.md).

| Example | Shows |
| --- | --- |
| `fs-basic`, `fs-streams`, `fs-watch`, `fs-roots-policy` | `sloppy/fs` read/write, streaming handles, watchers, named-root paths |
| `net-tcp-client`, `net-tcp-server`, `net-tcp-echo`, `net-deadline-cancel` | `sloppy/net` TCP client/server, deadlines, cancellation |
| `net-local-ipc`, `net-policy-strict` | `LocalEndpoint` IPC; HTTP client origin policy |
| `http-client-basic` | `HttpClient` outbound usage |
| `os-runtime-api`, `core-process-time-codec` | `sloppy/os` `System`, `Environment`, `Process.run`/`start`, `Signals` |
| `time-basic`, `time-deadline-cancellation`, `time-interval-schedule`, `time-fake-clock` | `sloppy/time` delays, deadlines, intervals, `Time.fakeClock` |
| `crypto-random-token`, `crypto-hash-hmac`, `crypto-password`, `crypto-secret-constant-time` | `sloppy/crypto` randomness, hashing, HMAC, Argon2id, `Secret` |
| `codec-base64-hex`, `codec-text-binary`, `codec-checksums`, `codec-compression`, `codec-streaming-compression` | `sloppy/codec` encodings, binary I/O, gzip |
| `core-fs-time-codec`, `core-network-time-codec`, `core-worker-time` | combined stdlib usage with deadlines |

---

## How to run one

For a new project, prefer a template:

```powershell
sloppy create my-api --template minimal-api
cd my-api
sloppy build
sloppy run .sloppy --once GET /health
```

Most curated examples have a `README.md` and a single source file. To run one
through the dev path:

```powershell
sloppy run examples/framework-hello/app.ts --once GET /hello/Ada
```

Or build first and run the artifacts:

```powershell
sloppy build examples/framework-hello/app.ts --out .sloppy-tmp
sloppy run .sloppy-tmp --once GET /hello/Ada
```

Project-mode examples, such as `prealpha-control-plane`, run from their own
directory:

```powershell
cd examples/prealpha-control-plane
sloppy build
sloppy run .sloppy --once GET /projects?owner=runtime
```

`sloppy run` enters V8. Default non-V8 builds report the V8 requirement after
compiling source input and writing artifacts; that diagnostic confirms the
handoff path, but it is not positive handler execution.

## Quick descriptions

### framework-hello

Two routes, one typed parameter, deterministic JSON response. The example the
[Quickstart](../quickstart.md) is built on. Good first run.

### hello-minimal

The smallest possible app: `Sloppy.create()`, one route, one `Results.text(...)`
response. Useful when something else stops working and you want to bisect.

### prealpha-control-plane

A multi-file project with `sloppy.json`, `appsettings*.json`, function modules,
route groups, JSON bodies, path and query params, SQLite provider metadata,
health routes, and diagnostics. Its app-host test imports the same route
modules and covers CORS, ProblemDetails, request IDs, request logging
redaction, service-scope cleanup, negative paths, and host lifecycle.

### framework-controller

Controller class with a `static inject` array, mapped via
`app.controller("/users", Controller, ...)`. Demonstrates DI without typed
handler parameters.

### framework-explicit-binding

Shows `Route<T>`, `Query<T>`, `Body<T>`, `Header<"name">` type wrappers for
explicit handler-parameter binding.

### request-context

Exercises the source-input/native request context fields currently covered by
the example: route params, query parsing, method, decoded path, and raw target.
Headers and body helpers are covered by the broader runtime/test-host suites.

### framework-di-services

Singleton, scoped, and transient services in a single app. Verifies that each
lifetime behaves as documented.

### config-basic

Loads config from `addObject`, reads with typed getters, demonstrates the
`SLOPPY:...` key normalization.

### framework-sqlite-crud

CRUD app using `Sqlite<"main">` typed injection. The typed-handler version of
the [SQLite walkthrough](sqlite.md).

### framework-postgres-crud

Same shape, PostgreSQL provider. Requires `libpq` and a running database. The
example reads its connection string from
`Sloppy__Providers__postgres__main__connectionString`. The compiler emits typed
provider metadata/wrappers; live execution depends on the PostgreSQL bridge,
provider config, and service setup.

### framework-sqlserver-crud

Same shape, SQL Server provider. Requires an ODBC driver and a connection
string in `Sloppy__Providers__sqlserver__main__connectionString`. The compiler
emits typed provider metadata/wrappers; live execution depends on the SQL
Server bridge, provider config, and driver support.

### framework-validation-errors

A handler with a schema-typed `Body<T>`. Sends back structured
`application/problem+json` validation errors when the body is malformed.

### workers-background-service

A `BackgroundService` that runs alongside HTTP. Demonstrates start/stop hooks
and cancellation through `WorkerCancellationSignal`.

### workers-workerpool

A bounded worker isolate pool. Niche, but the pattern is canonical.

## Complete example inventory

| Directory | What it demonstrates |
| --- | --- |
| `codec-base64-hex` | Base64 and hex codec usage |
| `codec-checksums` | Codec checksum helpers |
| `codec-compression` | Codec gzip compression |
| `codec-streaming-compression` | Streaming gzip compression |
| `codec-text-binary` | Text-to-binary conversion helpers |
| `compiler-hello` | Compiler/runtime conformance source (contributor) |
| `config-basic` | `addObject` and typed getters |
| `config-secrets-redaction` | Secret redaction in config |
| `config-strict-mode` | Strict config-mode behavior |
| `configured-api` | Compiler/config conformance (contributor) |
| `core-config-secrets` | Core integration: config + secrets |
| `core-fs-time-codec` | Core integration: fs + time + codec |
| `core-network-time-codec` | Core integration: net + time + codec |
| `core-policy-audit` | Core integration: policy + audit |
| `core-process-time-codec` | Core integration: process + time + codec |
| `core-worker-time` | Core integration: workers + time |
| `crypto-hash-hmac` | Hash and HMAC helpers |
| `crypto-password` | Password hashing (Argon2id) |
| `crypto-random-token` | Random token generation |
| `crypto-secret-constant-time` | `Secret` and constant-time comparison |
| `data-foundation` | Data and capability API surface |
| `dogfood` | Internal example catalog used by the test suite (contributor) |
| `ergonomics` | API ergonomics demonstrations |
| `framework-controller` | Controller class + DI through `static inject` |
| `framework-di-services` | Singleton/scoped/transient services |
| `framework-explicit-binding` | Typed parameter bindings (Route/Query/Body/Header) |
| `framework-hello` | First-API quickstart sample |
| `framework-postgres-crud` | PostgreSQL CRUD (needs a live database) |
| `framework-sqlite-crud` | SQLite CRUD with typed handler injection |
| `framework-sqlserver-crud` | SQL Server CRUD (needs a live database) |
| `framework-validation-errors` | Schema-backed body validation responses |
| `fs-basic` | `sloppy/fs` read/write basics |
| `fs-roots-policy` | Named-root filesystem policy |
| `fs-streams` | Filesystem streaming handles |
| `fs-watch` | Filesystem watcher |
| `hello` | Smallest hello-app source used by app-host shape checks |
| `hello-minimal` | Minimal runnable app |
| `http-client-basic` | `HttpClient` outbound usage |
| `modules-api` | Compiler module conformance (contributor) |
| `modules-basic` | Module API surface |
| `net-deadline-cancel` | Network deadlines and cancellation |
| `net-local-ipc` | Local IPC endpoints |
| `net-policy-strict` | HTTP client origin policy |
| `net-tcp-client` | TCP client |
| `net-tcp-echo` | TCP echo loop |
| `net-tcp-server` | TCP server |
| `os-runtime-api` | `sloppy/os` System/Environment/Process |
| `postgres-basic` | PostgreSQL provider (needs a live database) |
| `prealpha-control-plane` | Multi-file project-mode app host with SQLite and app-test-host coverage |
| `request-context` | Request context fields and parsing |
| `sqlite-basic` | SQLite provider basics |
| `sqlserver-basic` | SQL Server provider (needs a live database) |
| `time-basic` | `sloppy/time` delays |
| `time-deadline-cancellation` | Time cancellation |
| `time-fake-clock` | `Time.fakeClock` |
| `time-interval-schedule` | Intervals and schedules |
| `users-api-sqlite` | SQLite source-input conformance (contributor) |
| `validation-errors` | Compiler validation diagnostics (contributor) |
| `workers-background-service` | Long-running service alongside HTTP |
| `workers-js-isolate` | Worker isolate API surface |
| `workers-shutdown` | Worker shutdown behavior |
| `workers-workerpool` | Bounded worker isolate pool |
| `workers-workqueue` | Worker queue |
