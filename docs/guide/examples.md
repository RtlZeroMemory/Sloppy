# Examples

`/examples/` in the repository has many small apps. Most are smoke fixtures
that let the test suite exercise compiler/runtime behavior. A handful are
worth running by hand to see real Sloppy idioms.

This page lists the curated set. Run them with `sloppy run` from the repo
root.

## Start here

| Example                                  | Shows                                         |
| ---------------------------------------- | --------------------------------------------- |
| [`framework-v2-hello`](#framework-v2-hello) | `Sloppy.create()`, typed route param, JSON response |
| [`hello-minimal`](#hello-minimal)        | The smallest runnable app                     |

## Routing

| Example                                  | Shows                                         |
| ---------------------------------------- | --------------------------------------------- |
| [`framework-v2-controller`](#framework-v2-controller) | Controller class + DI through `static inject` |
| [`framework-v2-explicit-binding`](#framework-v2-explicit-binding) | `Route<T>`, `Query<T>`, `Body<T>`, `Header<>` typed bindings |
| [`request-context`](#request-context)    | `ctx.route`, `ctx.query`, `ctx.request.headers`, body parsing |

## Services and config

| Example                                  | Shows                                         |
| ---------------------------------------- | --------------------------------------------- |
| [`framework-v2-di-services`](#framework-v2-di-services) | Singleton/scoped/transient lifetimes through a request scope |
| [`config-basic`](#config-basic)          | `addObject`, typed getters                    |

## Data

| Example                                  | Shows                                         |
| ---------------------------------------- | --------------------------------------------- |
| [`framework-v2-sqlite-crud`](#framework-v2-sqlite-crud) | Typed-handler SQLite CRUD with provider injection |
| [`framework-v2-postgres-crud`](#framework-v2-postgres-crud) | Same shape, PostgreSQL provider (live lane) |
| [`framework-v2-sqlserver-crud`](#framework-v2-sqlserver-crud) | Same shape, SQL Server provider (live lane) |

## Validation

| Example                                  | Shows                                         |
| ---------------------------------------- | --------------------------------------------- |
| [`framework-v2-validation-errors`](#framework-v2-validation-errors) | Schema-backed body binding, Plan-level validation diagnostics |

## Workers

| Example                                  | Shows                                         |
| ---------------------------------------- | --------------------------------------------- |
| [`workers-background-service`](#workers-background-service) | Long-running service alongside the HTTP server |
| [`workers-workerpool`](#workers-workerpool) | Bounded pool of worker isolates              |

---

## How to run any of them

Most examples have a `README.md` and a single source file. To run one
through the dev path:

```
sloppy run examples/framework-v2-hello/app.ts --once GET /hello/Ada
```

Or build first and run the artifacts:

```
sloppy build examples/framework-v2-hello/app.ts --out .sloppy-tmp
sloppy run --artifacts .sloppy-tmp --once GET /hello/Ada
```

Some examples are *API-shape fixtures*, not runnable apps — their README
will say so. They exist to demonstrate the public API shape and to feed
the compiler test suite. If a fixture's README says it's not runnable,
believe it; run a different one.

## Quick descriptions

### framework-v2-hello

Two routes, one typed parameter, deterministic JSON response. The example
the [Quickstart](../quickstart.md) is built on. Good first run.

### hello-minimal

The smallest possible app — `Sloppy.create()`, one route, one
`Results.text(...)` response. Useful when something else stops working and
you want to bisect.

### framework-v2-controller

Controller class with a `static inject` array, mapped via
`app.controller("/users", Controller, …)`. Demonstrates DI without typed
handler parameters.

### framework-v2-explicit-binding

Shows `Route<T>`, `Query<T>`, `Body<T>`, `Header<"name">` type wrappers
for explicit handler-parameter binding.

### request-context

Exercises every field on `ctx`: route params, query parsing, header lookup,
JSON and text body parsing.

### framework-v2-di-services

Singleton, scoped, and transient services in a single app. Verifies that
each lifetime behaves as documented.

### config-basic

Loads config from `addObject`, reads with typed getters, demonstrates the
`SLOPPY:…` key normalization.

### framework-v2-sqlite-crud

CRUD app using `Sqlite<"main">` typed injection. The typed-handler
version of the [SQLite walkthrough](sqlite.md). SQLite is the only
provider with end-to-end typed-handler injection today.

### framework-v2-postgres-crud

Same shape, PostgreSQL provider. Requires `libpq` and a running
database. The example reads its connection string from the
`SLOPPY_POSTGRES_TEST_URL` environment variable. Typed-handler
injection for PostgreSQL is not implemented yet
(`SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE`); the example uses the
explicit module + service factory shape.

### framework-v2-sqlserver-crud

Same shape, SQL Server provider. Requires an ODBC driver and a
connection string in `SLOPPY_SQLSERVER_TEST_CONNECTION_STRING`. As
with PostgreSQL, typed-handler injection is not implemented yet; the
example uses explicit module + service factory.

### framework-v2-validation-errors

A handler with a schema-typed `Body<T>`. Sends back structured
`application/problem+json` validation errors when the body is malformed.

### workers-background-service

A `BackgroundService` that runs alongside HTTP. Demonstrates start/stop
hooks and cancellation through `WorkerCancellationSignal`.

### workers-workerpool

A bounded worker isolate pool. Niche, but the pattern is canonical.

## What about all the others?

The remaining `/examples/` folders are smoke and conformance fixtures. If
you find one you want to learn from, read the source. If it isn't on this
page, treat it as a test artifact rather than a tutorial.
