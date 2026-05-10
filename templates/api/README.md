# Sloppy API Starter

The recommended public alpha starter for building a backend with Sloppy.
SQLite-backed, with routes, route modules, a small service/repository split,
configuration, health endpoints, and the app package flow.

Public alpha, pre-production. APIs and artifact formats may change between
alpha revisions.

## Layout

- `src/main.ts` wires the app, SQLite provider, middleware, services, and
  routes.
- `src/routes/` contains route registration modules.
- `src/services/` contains business logic.
- `src/db/` contains the SQLite migration and repository helpers.
- `src/models/` contains request and response shapes.
- `appsettings*.json` contains runtime configuration.
- `data/` is where the development SQLite file is created.

## Build, inspect, run

```sh
sloppy build
sloppy routes .sloppy
sloppy capabilities .sloppy
sloppy doctor .sloppy
sloppy audit .sloppy
sloppy run .sloppy --once GET /health
sloppy run .sloppy --once GET /users
```

To exercise `POST /users`, run the development server and send a request
with your HTTP client:

```sh
sloppy run .sloppy
curl -X POST http://127.0.0.1:5173/users \
  -H "content-type: application/json" \
  -d "{\"name\":\"Katherine Johnson\",\"email\":\"katherine@example.test\"}"
```

## Package

```sh
sloppy package
sloppy run .sloppy/package --once GET /health
sloppy run .sloppy/package --once GET /users
```

The packaged app contains the compiled Sloppy artifacts. The SQLite database
path is `data/app.db`; create that directory when you run the package from a
location that does not already have it. `/health/ready` performs a SQLite
query after running the template migration.

## Where to edit next

- Add a route module under `src/routes/` and register it in `src/main.ts`.
- Add a service under `src/services/` when handlers need shared state.
- Add a config key in `appsettings.Development.json` and read it through the
  typed config API.
- Add a migration step in `src/db/` if you change the schema.

## Current limitations

- Public alpha, pre-production. Both Sloppy itself and this template can
  change before a stable release.
- SQLite is the strongest provider path. PostgreSQL and SQL Server need their
  own configuration, drivers, and live services; this template does not
  include those.
- No internet packages, Docker, or external services are required.
- Sloppy is not a full Node runtime. Avoid bringing in packages that depend on
  Node native addons, full Node globals, or unsupported `node:*` builtins.
