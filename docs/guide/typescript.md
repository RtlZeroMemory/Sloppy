# TypeScript support

`sloppyc` accepts a focused subset of TypeScript and JavaScript. The
goal is to extract a deterministic Plan from your source — anything the
compiler can't statically analyze is rejected with a stable
`SLOPPYC_E_*` diagnostic.

This page is the high-level shape. The full matrix of error codes and
extraction rules lives in
[reference/supported-syntax.md](../reference/supported-syntax.md), and
the canonical acceptance source is the fixture suite under
`compiler/tests/fixtures/`.

## Inputs

- File extensions: `.js`, `.mjs`, `.ts`.
- Required entry import: `Sloppy` from `"sloppy"`, named and unaliased.
- `Results` is file-local: any file with handlers that call `Results.*` must
  import `Results` from `"sloppy"` in that same file.
- Compiler-recognized import sources: `"sloppy"`, `"sloppy/data"`,
  `"sloppy/providers/sqlite"`, `"sloppy/providers/postgres"`,
  `"sloppy/providers/sqlserver"`, `"sloppy/fs"`, `"sloppy/time"`,
  `"sloppy/crypto"`, `"sloppy/codec"`, `"sloppy/net"`, `"sloppy/os"`,
  `"sloppy/workers"`, and relative imports rooted in the project.

Anything outside this list fails with
`SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER` or
`SLOPPYC_E_UNSUPPORTED_IMPORT`.

## What the compiler extracts

The compiler reads supported source and emits Plan metadata for:

- top-level `app.get/post/put/patch/delete` calls (and the `mapGet`/
  `mapPost`/… aliases) on the app and on groups;
- top-level `app.group(...)` and group method chains;
- literal `app.mapHealthChecks(...)` calls;
- route options — string literal patterns, route names, tags;
- top-level `app.services` and `builder.services` registrations
  (`addSingleton/addScoped/addTransient` with literal token strings and
  inline non-capturing factories);
- top-level capability declarations
  (`capabilities.addDatabase("token", { ... })`);
- typed handler parameter bindings (Framework v2): `Route<T>`,
  `Query<T>`, `Body<T>`, `Header<"name">`, `Service<T>`,
  `Config<"KEY">`, plus `Sqlite<"name">`, `Postgres<"name">`,
  `SqlServer<"name">`, `WorkQueue<"name">`;
- handler bodies that return `Results.*` literals or simple computed
  expressions over the request context;
- async handlers whose returned Promise settles in the bounded
  microtask drain;
- function modules and route-only modules.

Handler bodies and module shapes have their own static rules. If a
handler is too dynamic for the extractor, you'll get
`SLOPPYC_E_UNSUPPORTED_HANDLER`,
`SLOPPYC_E_UNSUPPORTED_HANDLER_PARAMETERS`,
`SLOPPYC_E_UNSUPPORTED_HANDLER_VALUE`,
`SLOPPYC_E_UNSUPPORTED_ASYNC_HANDLER_BODY`, or
`SLOPPYC_E_UNSUPPORTED_TYPESCRIPT_HANDLER`.

## What gets rejected

Confirmed unsupported (each has a fixture or diagnostic code):

- npm imports, dynamic `import()`, `node:` specifier prefix.
- Dynamic route patterns and computed method names
  (`SLOPPYC_E_UNSUPPORTED_DYNAMIC_ROUTE_PATTERN`,
  `SLOPPYC_E_UNSUPPORTED_COMPUTED_ROUTE_METHOD`).
- Conditional or loop-based route registration
  (`fixtures/conditional-route-registration/`,
  `fixtures/loop-route-registration/`).
- Closures over module-level bindings inside handlers
  (`fixtures/unsupported-handler-capture/`).
- TypeScript handler shapes the extractor doesn't model
  (`fixtures/unsupported-typescript-handler/`).
- HTTP methods other than GET/POST/PUT/PATCH/DELETE
  (`fixtures/unsupported-http-method/`,
  `SLOPPYC_E_UNSUPPORTED_HTTP_METHOD`).
- Dynamic middleware lookup, dynamic CORS policies, RequestId generator
  callbacks, dynamic RequestLogging options, Testing imports, and dynamic
  controller mappings fail with specific diagnostics instead of being omitted
  from the Plan. Static middleware, CORS, RequestId, RequestLogging, and
  controller subsets are compiler-extracted.
- Sloppy default imports (`fixtures/unsupported-sloppy-default-import/`,
  `SLOPPYC_E_UNSUPPORTED_IMPORT_SPECIFIER`).

What the compiler tolerates outside route-extraction code (helper
functions, modules) is broader, but:

- It does *not* type-check arbitrary TypeScript. Use `tsc` in your
  editor for full type checking; `sloppyc` parses TS syntax to extract
  Plan metadata.
- It does *not* evaluate arbitrary expressions. Static literals (route
  patterns, capability tokens, service tokens) are extracted; computed
  values are rejected.

If a syntactic feature isn't covered by a fixture, treat it as
unverified — file an issue or check the diagnostic if the compiler
rejects it.

## Imports

```ts
// Public surface (everything documented in docs/api/)
import { Sloppy, Results, sql, schema } from "sloppy";

// Provider type wrappers for typed handler injection
import { Sqlite } from "sloppy/providers/sqlite";

// Relative
import { usersModule } from "./users";
```

Subpath imports under `"sloppy/..."` are reserved for the runtime
stdlib; see [API](../api/README.md) for what each subpath exports.

`Results` imports are not inherited across files. A thin `main.ts` that only
creates the app and registers function modules can import `Sloppy` alone; each
route module imports `Results` when its own handlers return `Results.*`.

## Async handlers

```ts
app.get("/users/{id:int}", async (ctx) => {
    const user = await loadUser(ctx.route.id);
    return user ? Results.ok(user) : Results.notFound();
});
```

The runtime awaits the returned promise during the owner-thread
microtask drain. Long-running awaits aren't supported; if your handler
depends on multi-second background work, queue it via `WorkQueue` and
return `Results.accepted({ jobId })`.

## Type-driven handler bindings

Framework v2 typed handlers let you declare what a handler needs in
the parameter list:

```ts
import { Sloppy, Results, sql } from "sloppy";
import { Route, Query, Body } from "sloppy";
import { Sqlite } from "sloppy/providers/sqlite";

const app = Sloppy.create();

app.get("/users/{id:int}", (
    id:    Route<number>,
    db:    Sqlite<"main">,
) =>
    db.queryOne(sql`SELECT id, name FROM users WHERE id = ${id}`)
);

app.post("/users", (
    body:  Body<{ name: string; email: string }>,
    db:    Sqlite<"main">,
) =>
    db.exec(sql`
        INSERT INTO users (name, email)
        VALUES (${body.name}, ${body.email})
    `)
);
```

The compiler emits Plan metadata for route bindings, body schemas,
provider injections, and service capabilities from these types.

`Sqlite<"name">`, `Postgres<"name">`, and `SqlServer<"name">` all emit
provider metadata and generated typed-injection wrappers. SQLite is the
strongest local path. PostgreSQL and SQL Server execution also need the
matching connection-string config, active provider bridge, and live service
dependencies. `SLOPPYC_E_UNSUPPORTED_PROVIDER_BRIDGE` is reserved for
unsupported static non-SQLite provider handles such as
`app.provider("postgres:main")`.

## Common gotchas

- **Import paths must be statically analyzable.** `import("./" + name)`
  is rejected.
- **Route registrations are top-level only.** No `if`/`for` wrapping
  the call; no computed method names.
- **No `process.env`.** Sloppy source has no `process` global. Use
  `Environment.get(...)` from `"sloppy/os"` if you need env access in
  module code, or read configuration via `ctx.config` inside a
  handler.
- **No top-level await.** Initialize lazily inside services or
  handlers.
- **Config defaults are honored by typed injection.** `Config<"KEY">` reads the
  environment first, then falls back to a literal default from
  `app.config.getString("KEY", "default")` when the source declares one.
