# Examples

`/examples/` contains runnable apps, focused API examples, live-provider
examples, and fixtures used by the test suite. Start with the curated examples
below when you want code to learn from. Use the full inventory when you need to
find the evidence source for a specific feature.

If a row says "fixture", treat it as test evidence rather than a tutorial.

For a complete test-oriented inventory, see `examples/README.md` in the
repository.

## Start here

| Example | Shows |
| --- | --- |
| [`framework-hello`](#framework-hello) | `Sloppy.create()`, typed route param, JSON response |
| [`hello-minimal`](#hello-minimal) | The smallest runnable app |
| [`program-hello`](#program-hello) | Route-free Program Mode entrypoint |
| [`package-zod-like`](#package-zod-like) | Installed package graph with a local file dependency |

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
| `orm-basic` | First-party `sloppy/orm` model, CRUD, relations, and migration script shape |
| `orm-relations-includes` | One-join and collection split-query includes |
| `orm-cursor-export` | ORM cursor export to NDJSON chunks without materializing all rows |
| `orm-migrations` | Compiler-emitted ORM metadata and migration CLI output |

Guide: [Data streaming](data-streaming.md). APIs: [Data](../api/data.md) and
[ORM](../api/orm.md).

## Cache, Redis, and HTTP clients

| Example | Shows |
| --- | --- |
| `cache-basic` | Memory cache, cache-aside reads, schema validation, tag invalidation |
| `cache-output-api` | Route output cache and HTTP cache headers |
| `cache-hybrid-postgres` | Memory-fronted PostgreSQL distributed cache |
| `redis-basic` | First-party Redis client, values, scripts, diagnostics, health |
| `redis-cache` | Redis-backed cache provider with tags and service registration |
| `redis-locks` | Redis single-key lease shape |
| `http-client-typed` | Typed named client registered through services |
| `http-client-generated` | OpenAPI-to-typed-client generation |
| `http-client-resilience` | Retry, circuit-breaker, bulkhead, and pool options |
| `http-client-testhost` | `TestHttp.mock()` with TestHost outbound overrides |

APIs: [Cache](../api/cache.md), [Redis](../api/redis.md), and
[HTTP Client](../api/http-client.md).

## Durable jobs

| Example | Shows |
| --- | --- |
| `jobs-basic` | SQLite durable queue, idempotency, redaction, worker run-once |
| `jobs-recurring` | Five-field UTC cron schedules and manual recurring ticks |
| `jobs-postgres-worker` | PostgreSQL-backed worker shape |
| `jobs-sqlserver-worker` | SQL Server-backed worker shape |
| `jobs-concurrency`, `jobs-concurrency-sqlite`, `jobs-concurrency-step` | Provider-backed claim and lease behavior |

Guide: [Background tasks](background-tasks.md). API: [Jobs](../api/jobs.md).

## Validation

| Example | Shows |
| --- | --- |
| [`framework-validation-errors`](#framework-validation-errors) | Schema-backed body binding, Plan-level validation diagnostics |

## Rate limiting

| Example | Shows |
| --- | --- |
| `rate-limit-basic` | Sliding-window IP limit on a public login route |
| `rate-limit-auth` | User-partitioned limits on authenticated routes |
| `rate-limit-redis` | Declares `RateLimit.redis(...)` and its fail-closed behavior on `main` |
| `rate-limit-testhost` | Deterministic windows under `FakeClock` |
| `rate-limit-websocket` | WebSocket upgrade rate limiting |

Guide: [Rate limiting](rate-limiting.md). API: [RateLimit](../api/rate-limit.md).

## Webhooks

| Example | Shows |
| --- | --- |
| `webhooks-basic` | Event descriptor, outbox registration, transactional publish, signed delivery wiring |

Guide: [Webhooks](webhooks.md). Bootstrap test coverage:
`node tests/bootstrap/test_webhooks.mjs`.

## Static files

| Example | Shows |
| --- | --- |
| `static-files-basic` | `app.staticFiles("/assets", { root: "public", ... })` over a project-local directory |
| `static-files-package` | Static assets carried into a packaged app artifact |

Guide: [Serve Static Assets](static-assets.md). API:
[Static Files](../api/static-files.md).

## Background tasks and workers

| Example | Shows |
| --- | --- |
| [`workers-background-service`](#workers-background-service) | Long-running service alongside the HTTP server |
| `workers-workqueue` | Producer/consumer queue with retry |
| [`workers-workerpool`](#workers-workerpool) | Bounded pool of worker isolates |
| `workers-js-isolate` | Single `Worker.start` isolate from a module path |
| `workers-shutdown` | Drain-vs-cancel `stop()` behavior |

Guide: [Background tasks](background-tasks.md). API:
[Workers](../api/workers.md).

## Stdlib (filesystem, network, OS, time, crypto, codec)

These are API-shape fixtures rather than full tutorials, but they are the
shortest path to seeing each stdlib module in real source. Reference docs:
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

### program-hello

Route-free Program Mode source with `main(args, ctx)`, relative module imports,
arguments after `--`, and stdout.

### package-zod-like

Installed package graph example that uses a local `file:` dependency. It is the
smallest example for package resolution without depending on registry access.

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

Same shape, PostgreSQL provider. This optional provider example needs
PostgreSQL client support and a running database. Normal Sloppy apps, the
Quickstart, Program Mode, SQLite, templates, and package support do not need
PostgreSQL or libpq. The example reads its connection string from
`Sloppy__Providers__postgres__main__connectionString`. The compiler emits typed
provider metadata/wrappers; live execution depends on the PostgreSQL bridge,
provider config, and service setup.

### framework-sqlserver-crud

Same shape, SQL Server provider. This optional provider example needs
Microsoft ODBC Driver 17 or 18 and a connection string in
`Sloppy__Providers__sqlserver__main__connectionString`. Normal Sloppy apps, the
Quickstart, Program Mode, SQLite, templates, and package support do not need
SQL Server or ODBC. The compiler emits typed provider metadata/wrappers; live
execution depends on the SQL Server bridge, provider config, and driver support.

### framework-validation-errors

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
| `cache-basic` | Memory cache API-shape fixture |
| `cache-hybrid-postgres` | Hybrid cache example, live-provider gated |
| `cache-output-api` | Output cache API-shape fixture |
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
| `dogfood` | Machine-readable evidence catalog |
| `ergonomics` | API ergonomics fixture |
| `framework-controller` | Curated routing/controller example |
| `framework-di-services` | Curated services example |
| `framework-explicit-binding` | Curated typed binding example |
| `framework-hello` | Curated quickstart example |
| `framework-postgres-crud` | Live PostgreSQL example |
| `framework-sqlite-crud` | Curated SQLite CRUD example |
| `framework-sqlserver-crud` | Live SQL Server example |
| `framework-validation-errors` | Curated validation example |
| `fs-basic` | Filesystem API-shape fixture |
| `fs-roots-policy` | Filesystem policy fixture |
| `fs-streams` | Filesystem streaming API-shape fixture |
| `fs-watch` | Filesystem watch API-shape fixture |
| `hello` | Smallest hello-app fixture used by app-host shape checks |
| `hello-minimal` | Minimal runnable example |
| `http-client-basic` | HTTP client API-shape fixture |
| `http-client-generated` | OpenAPI-to-typed-client documentation example |
| `http-client-resilience` | HTTP client resilience documentation example |
| `http-client-testhost` | HTTP client TestHost mock documentation example |
| `http-client-testhost-package-mock` | HTTP client package TestHost mock documentation example |
| `http-client-typed` | Typed HTTP client documentation example |
| `jobs-basic` | Durable jobs SQLite example |
| `jobs-concurrency` | Durable jobs concurrency example, live-provider gated |
| `jobs-concurrency-sqlite` | Durable jobs SQLite concurrency example |
| `jobs-concurrency-step` | Durable jobs process-step concurrency example |
| `jobs-postgres-worker` | Durable jobs PostgreSQL worker example, live-provider gated |
| `jobs-recurring` | Durable recurring jobs example |
| `jobs-sqlserver-worker` | Durable jobs SQL Server worker example, live-provider gated |
| `jobs-stress` | Durable jobs stress example |
| `modules-api` | Compiler module conformance fixture |
| `modules-basic` | Module API-shape fixture |
| `net-deadline-cancel` | Network cancellation fixture |
| `net-local-ipc` | Local IPC API-shape fixture |
| `net-policy-strict` | Network policy fixture |
| `net-tcp-client` | TCP client API-shape fixture |
| `net-tcp-echo` | TCP echo API-shape fixture |
| `net-tcp-server` | TCP server API-shape fixture |
| `os-runtime-api` | OS API-shape fixture |
| `orm-basic` | ORM documentation example |
| `orm-cursor-export` | ORM cursor export documentation example |
| `orm-migrations` | ORM migration tooling fixture |
| `orm-relations-includes` | ORM relations/include documentation example |
| `orm-testservices` | ORM TestServices documentation example |
| `postgres-basic` | PostgreSQL provider fixture, live-provider gated |
| `control-plane` | Larger app-style coverage example for routing, data, diagnostics, and tooling |
| `rate-limit-auth` | Authenticated-user rate-limit example |
| `rate-limit-basic` | Sliding-window IP rate-limit example |
| `rate-limit-redis` | Distributed rate-limit adapter declaration (fails closed on `main`) |
| `rate-limit-testhost` | Rate-limit windows under `FakeClock` |
| `rate-limit-websocket` | WebSocket upgrade rate limiting |
| `redis-basic` | Redis client example, live-provider gated |
| `redis-cache` | Redis-backed cache example, live-provider gated |
| `redis-locks` | Redis lock example, live-provider gated |
| `request-context` | Curated request context example |
| `sqlite-basic` | SQLite provider fixture |
| `sqlserver-basic` | SQL Server provider fixture, live-provider gated |
| `static-files-basic` | `app.staticFiles` over a project-local directory |
| `static-files-package` | Static assets carried into a packaged app artifact |
| `webhooks-basic` | Webhook event descriptor, outbox registration, transactional publish, signed delivery |
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
