# Examples

`/examples/` in the repository has many small apps. Most are smoke fixtures
that let the test suite exercise compiler/runtime behavior. A smaller set is
worth running by hand to learn real Sloppy idioms.

This page starts with the curated set, then lists every example directory with
its current role. If a row says "fixture", treat it as test evidence rather
than a tutorial.

For a complete evidence-oriented inventory, see `examples/README.md` in the
repository.

## Start here

| Example | Shows |
| --- | --- |
| [`framework-v2-hello`](#framework-v2-hello) | `Sloppy.create()`, typed route param, JSON response |
| [`hello-minimal`](#hello-minimal) | The smallest runnable app |

## Dogfood proof

| Example | Shows |
| --- | --- |
| [`prealpha-control-plane`](#prealpha-control-plane) | Multi-file project-mode app host, SQLite capability, app test host proof |

## Routing

| Example | Shows |
| --- | --- |
| [`framework-v2-controller`](#framework-v2-controller) | Controller class + DI through `static inject` |
| [`framework-v2-explicit-binding`](#framework-v2-explicit-binding) | `Route<T>`, `Query<T>`, `Body<T>`, `Header<>` typed bindings |
| [`request-context`](#request-context) | `ctx.route`, `ctx.query`, `ctx.request.headers`, body parsing |

## Services and config

| Example | Shows |
| --- | --- |
| [`framework-v2-di-services`](#framework-v2-di-services) | Singleton/scoped/transient lifetimes through a request scope |
| [`config-basic`](#config-basic) | `addObject`, typed getters |

## Data

| Example | Shows |
| --- | --- |
| [`framework-v2-sqlite-crud`](#framework-v2-sqlite-crud) | Typed-handler SQLite CRUD with provider injection |
| [`framework-v2-postgres-crud`](#framework-v2-postgres-crud) | Same shape, PostgreSQL provider (live lane) |
| [`framework-v2-sqlserver-crud`](#framework-v2-sqlserver-crud) | Same shape, SQL Server provider (live lane) |

## Validation

| Example | Shows |
| --- | --- |
| [`framework-v2-validation-errors`](#framework-v2-validation-errors) | Schema-backed body binding, Plan-level validation diagnostics |

## Workers

| Example | Shows |
| --- | --- |
| [`workers-background-service`](#workers-background-service) | Long-running service alongside the HTTP server |
| [`workers-workerpool`](#workers-workerpool) | Bounded pool of worker isolates |

---

## How to run one

For a new project, prefer a template:

```powershell
sloppy create my-api --template minimal-api
cd my-api
sloppy run --once GET /health
```

Most curated examples have a `README.md` and a single source file. To run one
through the dev path:

```powershell
sloppy run examples/framework-v2-hello/app.ts --once GET /hello/Ada
```

Or build first and run the artifacts:

```powershell
sloppy build examples/framework-v2-hello/app.ts --out .sloppy-tmp
sloppy run --artifacts .sloppy-tmp --once GET /hello/Ada
```

Project-mode examples, such as `prealpha-control-plane`, run from their own
directory:

```powershell
cd examples/prealpha-control-plane
sloppy run --once GET /projects?owner=runtime
```

`sloppy run` enters V8. Default non-V8 builds report the V8 requirement after
compiling source input and writing artifacts; that diagnostic is useful proof,
but it is not positive handler execution.

## Quick descriptions

### framework-v2-hello

Two routes, one typed parameter, deterministic JSON response. The example the
[Quickstart](../quickstart.md) is built on. Good first run.

### hello-minimal

The smallest possible app: `Sloppy.create()`, one route, one `Results.text(...)`
response. Useful when something else stops working and you want to bisect.

### prealpha-control-plane

A multi-file project with `sloppy.json`, `appsettings*.json`, function modules,
route groups, JSON bodies, path and query params, SQLite provider metadata,
health routes, diagnostics, and dogfood proof lanes. Its app-host test imports
the same route modules and covers CORS, ProblemDetails, request IDs, request
logging redaction, service-scope cleanup, negative paths, and host lifecycle.

### framework-v2-controller

Controller class with a `static inject` array, mapped via
`app.controller("/users", Controller, ...)`. Demonstrates DI without typed
handler parameters.

### framework-v2-explicit-binding

Shows `Route<T>`, `Query<T>`, `Body<T>`, `Header<"name">` type wrappers for
explicit handler-parameter binding.

### request-context

Exercises the source-input/native request context fields currently covered by
the example: route params, query parsing, method, decoded path, and raw target.
Headers and body helpers are covered by the broader runtime/test-host suites.

### framework-v2-di-services

Singleton, scoped, and transient services in a single app. Verifies that each
lifetime behaves as documented.

### config-basic

Loads config from `addObject`, reads with typed getters, demonstrates the
`SLOPPY:...` key normalization.

### framework-v2-sqlite-crud

CRUD app using `Sqlite<"main">` typed injection. The typed-handler version of
the [SQLite walkthrough](sqlite.md).

### framework-v2-postgres-crud

Same shape, PostgreSQL provider. Requires `libpq` and a running database. The
example reads its connection string from
`Sloppy__Providers__postgres__main__connectionString`. The compiler emits typed
provider metadata/wrappers; live execution depends on the PostgreSQL bridge,
provider config, and service setup.

### framework-v2-sqlserver-crud

Same shape, SQL Server provider. Requires an ODBC driver and a connection
string in `Sloppy__Providers__sqlserver__main__connectionString`. The compiler
emits typed provider metadata/wrappers; live execution depends on the SQL
Server bridge, provider config, and driver support.

### framework-v2-validation-errors

A handler with a schema-typed `Body<T>`. Sends back structured
`application/problem+json` validation errors when the body is malformed.

### workers-background-service

A `BackgroundService` that runs alongside HTTP. Demonstrates start/stop hooks
and cancellation through `WorkerCancellationSignal`.

### workers-workerpool

A bounded worker isolate pool. Niche, but the pattern is canonical.

## Complete example inventory

| Directory | Current role |
| --- | --- |
| `codec-base64-hex` | Codec API-shape fixture |
| `codec-checksums` | Codec API-shape fixture |
| `codec-compression` | Codec API-shape fixture |
| `codec-streaming-compression` | Codec API-shape fixture |
| `codec-text-binary` | Codec API-shape fixture |
| `compiler-hello` | Compiler/runtime conformance source |
| `config-basic` | Curated config example |
| `config-secrets-redaction` | Config redaction fixture |
| `config-strict-mode` | Config strict-mode fixture |
| `configured-api` | Compiler/config conformance fixture |
| `core-config-secrets` | Core integration fixture |
| `core-fs-time-codec` | Core integration fixture |
| `core-network-time-codec` | Core integration fixture |
| `core-policy-audit` | Core integration fixture |
| `core-process-time-codec` | Core integration fixture |
| `core-worker-time` | Core integration fixture |
| `crypto-hash-hmac` | Crypto API-shape fixture |
| `crypto-password` | Crypto API-shape fixture |
| `crypto-random-token` | Crypto API-shape fixture |
| `crypto-secret-constant-time` | Crypto API-shape fixture |
| `data-foundation` | Data/capability API-shape fixture |
| `dogfood` | Machine-readable dogfood catalog |
| `ergonomics` | API ergonomics fixture |
| `framework-v2-controller` | Curated routing/controller example |
| `framework-v2-di-services` | Curated services example |
| `framework-v2-explicit-binding` | Curated typed binding example |
| `framework-v2-hello` | Curated quickstart example |
| `framework-v2-postgres-crud` | Live PostgreSQL example |
| `framework-v2-sqlite-crud` | Curated SQLite CRUD example |
| `framework-v2-sqlserver-crud` | Live SQL Server example |
| `framework-v2-validation-errors` | Curated validation example |
| `fs-basic` | Filesystem API-shape fixture |
| `fs-roots-policy` | Filesystem policy fixture |
| `fs-streams` | Filesystem streaming API-shape fixture |
| `fs-watch` | Filesystem watch API-shape fixture |
| `hello` | Legacy hello example fixture |
| `hello-minimal` | Minimal runnable example |
| `http-client-basic` | HTTP client API-shape fixture |
| `modules-api` | Compiler module conformance fixture |
| `modules-basic` | Module API-shape fixture |
| `net-deadline-cancel` | Network cancellation fixture |
| `net-local-ipc` | Local IPC API-shape fixture |
| `net-policy-strict` | Network policy fixture |
| `net-tcp-client` | TCP client API-shape fixture |
| `net-tcp-echo` | TCP echo API-shape fixture |
| `net-tcp-server` | TCP server API-shape fixture |
| `os-runtime-api` | OS API-shape fixture |
| `postgres-basic` | PostgreSQL provider fixture, live-provider gated |
| `prealpha-control-plane` | Dogfood proof app |
| `request-context` | Curated request context example |
| `sqlite-basic` | SQLite provider fixture |
| `sqlserver-basic` | SQL Server provider fixture, live-provider gated |
| `time-basic` | Time API-shape fixture |
| `time-deadline-cancellation` | Time cancellation fixture |
| `time-fake-clock` | Time fake-clock fixture |
| `time-interval-schedule` | Time interval/schedule fixture |
| `users-api-sqlite` | SQLite source-input conformance example |
| `validation-errors` | Compiler validation fixture |
| `workers-background-service` | Curated worker service example |
| `workers-js-isolate` | Worker isolate API-shape fixture |
| `workers-shutdown` | Worker shutdown fixture |
| `workers-workerpool` | Curated worker pool example |
| `workers-workqueue` | Worker queue fixture |
