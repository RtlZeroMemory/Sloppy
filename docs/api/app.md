# App

The application object exposes routing, services, configuration, logging, and
provider hooks. There are two ways to construct one.

## `Sloppy.create()`

Returns a built app with the four builder namespaces sealed but routes
and modules still open. Use it for small apps that don't need custom
configuration sources or service registration.

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.get("/", () => Results.text("ok"));

export default app;
```

`Sloppy.create()` is sugar for `Sloppy.createBuilder().build()`. After
`build()`, the config/services/logging/capabilities builders are sealed,
but route registration (`app.get`, `app.useModule`, `app.controller`,
…) is still allowed until you call `app.freeze()` explicitly.

## `Sloppy.createBuilder()`

Returns a mutable builder when you need to register services, attach config
sources, declare capabilities, or set up logging before the app exists.

```ts
import { Sloppy, Results } from "sloppy";

const builder = Sloppy.createBuilder();

builder.config.addObject({ "app:greeting": "hello" });
builder.services.addSingleton(
    "greeting",
    () => builder.config.getString("app:greeting", "hi"),
);

const app = builder.build();

app.get("/", (ctx) => Results.text(ctx.services.get("greeting")));

export default app;
```

Service factories receive a service scope, *not* the config provider —
read `builder.config` (or `app.config` after build) directly when you
need configuration inside a factory. See [services](services.md) for
the resolver shape.

The builder has four namespaces:

- `builder.config` — see [config](config.md)
- `builder.logging` — see [logging](logging.md)
- `builder.services` — see [services](services.md)
- `builder.capabilities` — see [capabilities](capabilities.md)

Calling `builder.build()` runs every registered module's capability and
service callbacks, freezes the four builder namespaces, then returns the
app. Module route callbacks run after the app exists so they can call
`app.get` etc.

## App methods

After construction, every app exposes:

| Member            | Purpose                                                        |
| ----------------- | -------------------------------------------------------------- |
| `app.config`      | Frozen `ConfigProvider`                                        |
| `app.log`         | `Logger`                                                       |
| `app.services`    | Service resolver (root scope)                                  |
| `app.capabilities`| Capability provider                                            |
| `app.auth`        | Auth policy registry (experimental)                           |
| `app.use(...)`    | Register a provider descriptor or worker resource              |
| `app.useModule(...)`| Register a route-only or full module                         |
| `app.get/post/put/patch/delete(...)` | Register a route                            |
| `app.mapGet/mapPost/...` | Same as the above; `map*` is the longer name           |
| `app.group(...)`  | Create a route group; alias `app.mapGroup`                     |
| `app.urlFor(...)` | Generate a URL for a named route                            |
| `app.controller(...)`| Register a controller class; alias `app.mapController`      |
| `app.docs(...)`      | Register first-party OpenAPI JSON and Swagger UI routes     |
| `app.freeze()`    | Mark the app immutable; further route registration throws      |
| `app.isFrozen()`  | Check immutability                                             |

`app.get` and `app.mapGet` (and the equivalents for other verbs) are aliases —
pick one style and stick with it.

For tests, build the app normally and pass it to `TestHost.create(app)`.
Use `TestHost.fromArtifacts(...)` or `TestHost.fromPackage(...)` when the
test must exercise compiled artifacts and the native runtime path. See
[TestHost](testhost.md).

## OpenAPI docs

`app.docs(options?)` registers two `GET` routes:

- `openapiPath`, default `/openapi.json`, returns the app's OpenAPI document.
- `path`, default `/docs`, returns an embedded Swagger UI page that loads that
  document without CDN or npm middleware.

```ts
app.docs({
    title: "Users API",
    path: "/docs",
    openapiPath: "/openapi.json",
    requireAuth: { policy: "Docs.Read" },
});
```

The docs routes are normal route-table entries named `Docs.OpenApi` and
`Docs.Ui`. Use `requireAuth` when the generated contract should not be public.
The compiler records these routes in the Plan so `sloppy routes`, `doctor`,
`audit`, `openapi`, and package flows can see them.

## Modules

A module groups capabilities, services, and routes under a name.

```ts
const Users = Sloppy.module("users")
    .services((services) => {
        services.addScoped("users.repo", () => new UsersRepo());
    })
    .routes((app) => {
        app.get("/users/{id:int}", handler);
    });

const app = Sloppy.create();
app.useModule(Users);
```

Module phases run in order: `capabilities` → `services` → `routes`. All
callbacks are synchronous. Module names match `^[a-z][a-z0-9.-]*$`.

A bare function module is also accepted:

```ts
function statusModule(app) {
    app.get("/_status", () => Results.text("ok"));
}

app.useModule(statusModule);
```

Function modules are route-only — they receive the app directly and can only
register routes.

## Providers

`app.use(...)` registers a Sloppy provider descriptor. The simplest way
to construct one for SQLite is the helper exported from
`sloppy/providers/sqlite`:

```ts
import { Sloppy, Results } from "sloppy";
import { sqlite } from "sloppy/providers/sqlite";

const app = Sloppy.create();
app.use(sqlite("main", { database: "app.db" }));
```

For PostgreSQL and SQL Server, declare a capability up front and call
`data.postgres.open(...)` / `data.sqlserver.open(...)` from a service
factory or handler. See [data](data.md).

## Freezing

`builder.build()` seals the config/services/logging/capabilities
namespaces. Route and module registration stays open until you call
`app.freeze()` explicitly. Once frozen, `app.useModule`, `app.get`,
`app.use`, and the rest throw.

```ts
const app = Sloppy.create();
app.get("/", () => Results.text("ok"));
app.freeze();
// app.get(...) now throws
```

Freezing before serving is useful in tests that assert nothing else
mutates the app.
