# Framework Reference

This page documents the current JavaScript framework surface from `stdlib/sloppy/app.js` and bootstrap tests.

## Imports

Root runtime exports come from `sloppy` (for example `Sloppy`, `Router`, `Results`,
`ProblemDetails`, `RequestId`, `RequestLogging`, `schema`, `data`, and `sql`).

Provider descriptor registration currently has one runtime module:

```ts
import { sqlite } from "sloppy/providers/sqlite";
```

Compiler metadata markers such as `Route<T>`, `Query<T>`, `Body<T>`, `Header<...>`, `RequestContext`, `Service<T>`, `Config<...>`, `Sqlite<...>`, `Postgres<...>`, and `SqlServer<...>` are compile-time extraction shapes used by `sloppyc`.

## Sloppy Object

| API | Behavior |
| --- | --- |
| `Sloppy.create()` | Returns a built app with default builder state. |
| `Sloppy.createBuilder()` | Returns a mutable builder (`config`, `logging`, `capabilities`, `services`, `addModule`, `build`). |
| `Sloppy.module(name)` | Creates a module descriptor with capability/service/route phases. |

## App Object

| API | Behavior |
| --- | --- |
| `config`, `log`, `services`, `capabilities` | Built providers. |
| `use(providerOrWorker)` | Accepts worker resources and Sloppy provider descriptors. Current provider kind accepted by app validation: `sqlite`. |
| `useCors(policy)` | Registers an app-host CORS policy for subsequently registered routes and generated preflight handlers. |
| `useModule(moduleOrFactory)` | Accepts route-only `Sloppy.module(...)` or named synchronous function modules. |
| `mapGet/mapPost/mapPut/mapPatch/mapDelete` | Route registration methods. |
| `get/post/put/patch/delete` | Aliases for `map*`. |
| `mapHealthChecks(options?)` | Registers bootstrap health, liveness, and readiness routes. |
| `mapGroup` / `group` | Route grouping helpers with group-local middleware support. |
| `mapController` / `controller` | Controller mapper APIs. |
| `freeze()` / `isFrozen()` | Freeze app mutation state. |
| `__getRoutes()`, `__debug()`, `__getModuleGraph()`, `__getPlanContributions()` | Tested introspection helpers used by tooling/tests. |

## Builder Behavior

- `build()` runs module phases in dependency order.
- Capability phases run first, then service phases, then route phases.
- `build()` freezes builder mutation.
- Duplicate module names are rejected.
- Missing module dependencies and dependency cycles are rejected.
- Async module phase callbacks are rejected.

## Module Rules

- Module names must be lowercase.
- `dependsOn(...)` creates dependency edges.
- Duplicate module registration is rejected.
- Route-only module descriptors can be used directly with `app.useModule(...)`.
- Function modules must be named and synchronous.

## CORS Policies

Bootstrap app-host CORS uses this shape:

```ts
app.useCors({
  origins: ["https://app.example"],
  headers: ["content-type", "authorization"],
  exposedHeaders: ["x-total-count"],
  credentials: true,
  maxAgeSeconds: 600,
});
```

Current behavior:

- `app.useCors(policy)` applies to routes registered after the policy call.
- `origins` is required and accepts an origin string, an origin array, or `"*"`.
- `credentials: true` requires explicit origins.
- `methods` can override the preflight `Access-Control-Allow-Methods` value; otherwise
  the app-host derives methods from registered routes with the same pattern.
- `headers` lists request headers allowed during preflight.
- `exposedHeaders` writes `Access-Control-Expose-Headers` on actual responses.
- `maxAgeSeconds` writes `Access-Control-Max-Age` on successful preflight responses.
- Actual route responses receive CORS headers only when the request has an allowed
  `Origin` header.
- The bootstrap app-host creates `OPTIONS` preflight routes for CORS-enabled patterns.

CORS currently runs in the bootstrap app-host route handler path. Compiler
extraction and emitted Plan metadata for CORS policies will be added in a separate
compiler/runtime slice.

## Provider Descriptors In Framework Registration

`sqlite(name, options?)` returns a frozen descriptor with:

- `__sloppyProvider: true`
- `kind: "sqlite"`
- `name`
- `token`: `data.<name>` unless `name` already contains a dot
- `options`

Provider descriptor name validation:

- non-empty string
- no leading/trailing whitespace
- pattern: letters, digits, dot, underscore, hyphen

Merged provider options at `app.use(...)` must satisfy current sqlite checks, including non-empty `database`.

Only SQLite descriptors are currently accepted by `app.use(...)`. PostgreSQL and
SQL Server appear through typed injection and runtime data APIs, not through
`app.use(postgres(...))` or `app.use(sqlserver(...))` descriptor modules.

## Health Checks

`app.mapHealthChecks(options?)` registers the current bootstrap health route
set:

- aggregate health: `GET /health`
- liveness: `GET /health/live`
- readiness: `GET /health/ready`

Paths can be customized:

```ts
app.mapHealthChecks({
  path: "/status",
  livenessPath: "/status/live",
  readinessPath: "/status/ready",
});
```

Health checks are functions or named check objects:

```ts
app.mapHealthChecks({
  checks: [
    function cache() {
      return true;
    },
    {
      name: "worker-loop",
      liveness: true,
      readiness: false,
      async check() {
        return { ok: true };
      },
    },
    {
      name: "database",
      check() {
        return false;
      },
    },
  ],
});
```

Check functions receive the same handler context shape as regular bootstrap
handlers, including `services`, `capabilities`, `config`, `log`, and `route`.
Function checks are readiness checks by default. Object checks can opt into
liveness with `liveness: true` and can opt out of readiness with
`readiness: false`. Liveness has no dependency checks by default, so the
liveness endpoint reports the process route table as reachable unless a check
explicitly targets liveness.

Healthy responses use status `200`; unhealthy responses use status `503`.
Response bodies contain only the health status and per-check names/statuses:

```json
{
  "status": "unhealthy",
  "checks": [
    { "name": "database", "status": "unhealthy" }
  ]
}
```

Check return details and thrown error messages are intentionally omitted from
the health response. Detailed failure logging belongs in the logging and
diagnostics surfaces.

## Request IDs

`RequestId.defaults(options?)` returns app-host middleware that assigns one request ID
to each request:

```ts
import { RequestId } from "sloppy";

app.use(RequestId.defaults());
```

Default behavior:

- generated IDs use a stable `req-<number>` format;
- incoming request IDs are ignored;
- `ctx.requestId` is available to later middleware and handlers;
- the response receives an `x-request-id` header.

Options:

| Option | Behavior |
| --- | --- |
| `header` | Header name to read/write. Default: `x-request-id`. Must be a safe unmanaged HTTP header name. |
| `responseHeader` | Writes the response header when `true`. Default: `true`. |
| `trustIncoming` | Uses a safe incoming header value when `true`. Default: `false`. |
| `generator` | Function used to generate IDs. Useful for deterministic tests. |

Trusted incoming values must be non-empty HTTP header values without control
characters. Invalid incoming values are ignored and a generated ID is used instead.

## Request Logging

`RequestLogging.defaults(options?)` returns app-host middleware that writes one
structured log entry when the request completes:

```ts
import { RequestId, RequestLogging } from "sloppy";

app.use(RequestId.defaults());
app.use(RequestLogging.defaults());
```

The entry message is `request completed`. Fields include the request method, path
or target, status, route pattern when known, request ID when present, and duration
in milliseconds when enabled.

Options:

| Option | Behavior |
| --- | --- |
| `includeRoute` | Includes the route pattern when the app-host knows it. Default: `true`. |
| `includeDuration` | Includes `durationMs` from the host clock. Default: `true`. |
| `includeRequestId` | Includes `requestId` when `RequestId` ran earlier in the pipeline. Default: `true`. |

Request logging records metadata only. It does not log request bodies or request
headers. Authorization, cookie, API key, and proxy authorization header values stay
out of the default log entry.

## Static Provider Handles

The current static provider handle syntax is:

```ts
const db = app.provider("sqlite:main");
```

This path is generated for SQLite provider handles. Non-SQLite static provider
handles are rejected by the compiler-generated provider bridge path with
`SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE` rather than silently becoming live
PostgreSQL or SQL Server execution.

## Typed Provider Injection

Framework v2 typed handlers can ask for provider parameters:

```ts
app.get("/users", async (db: Sqlite<"main">, ctx: RequestContext) => {
  return Results.json(await db.query("select id, name from users"));
});
```

`Sqlite<"...">`, `Postgres<"...">`, and `SqlServer<"...">` are compile-time
metadata markers. Runtime execution depends on the active V8 bridge, provider
configuration, and live-provider dependencies for PostgreSQL and SQL Server.

## Mutation And Lifecycle Errors

Common enforced errors:

- duplicate route registration (`method + pattern`)
- duplicate service registration
- app/builder frozen mutation
- circular service dependencies
- singleton resolving scoped dependency
- root service resolution for non-singleton services

## ProblemDetails

`ProblemDetails.defaults(options?)` returns an app-level error handling descriptor:

```ts
import { Sloppy, ProblemDetails } from "sloppy";

const app = Sloppy.create();
app.use(ProblemDetails.defaults());
```

When installed, synchronous thrown handler errors and async rejected handler errors are
converted to `500` problem responses with this safe body:

```json
{"status":500,"title":"Internal Server Error","code":"SLOPPY_E_HANDLER_ERROR"}
```

Options:

| Option | Behavior |
| --- | --- |
| `detail: "never"` | Default. Do not include exception details. |
| `detail: "development"` | Include details only in the stdlib app path when `Sloppy:Environment` or `Environment` is `Development`. |
| `detail: "always"` | Include details in the stdlib app path. Use only for local diagnostics. |

## Limits

- `app.use(...)` provider validation is currently sqlite-only.
- `app.provider("sqlite:main")` is the current static provider handle path; non-SQLite static handles are diagnostic-only.
- Typed PostgreSQL and SQL Server injection still needs live database setup for
  execution.
- Health checks currently register bootstrap route-table entries. Deployment
  health policy, authentication, and load-balancer integration are separate
  framework/deployment areas.
- Request ID and request logging helpers run in the bootstrap app-host middleware
  path. Compiler extraction and emitted Plan metadata for those helpers are separate
  framework work.
- Double-underscore methods are usable and tested, but remain internal-oriented surfaces.
- Handler execution through `sloppy run` requires V8.
- ProblemDetails currently provides safe global handler-error mapping. Production error
  policy and exception taxonomy are separate framework areas.
