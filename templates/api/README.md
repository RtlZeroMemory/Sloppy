# Sloppy API Starter

This is the recommended public alpha starter for building a backend with
Sloppy. It shows routes, route modules, a small service/repository split,
configuration, SQLite provider metadata, health endpoints, and the app package
flow.

## Layout

- `src/main.ts` wires the app, SQLite provider, middleware, services, and routes.
- `src/routes/` contains route registration modules.
- `src/services/` contains business logic.
- `src/db/` contains the SQLite migration and repository helpers.
- `src/models/` contains request and response shapes.
- `appsettings*.json` contains runtime configuration.
- `data/` is where the development SQLite file is created.

## Build And Inspect

```sh
sloppy build
sloppy routes .sloppy
sloppy capabilities .sloppy
sloppy doctor .sloppy
sloppy audit .sloppy
```

## Run

```sh
sloppy run .sloppy --once GET /health
sloppy run .sloppy --once GET /users
```

To exercise `POST /users`, run the development server and send a request with
your HTTP client:

```sh
sloppy run .sloppy
curl -X POST http://127.0.0.1:5173/users -H "content-type: application/json" -d "{\"name\":\"Katherine Johnson\",\"email\":\"katherine@example.test\"}"
```

## Package

```sh
sloppy package
sloppy run .sloppy/package --once GET /health
sloppy run .sloppy/package --once GET /users
```

The alpha package format contains the compiled Sloppy artifacts. The SQLite
database path is configured as `data/app.db`; create that directory when you run
the package from a directory that does not already have it.

## Alpha Limits

Sloppy is pre-production alpha software. SQLite is available through Sloppy's
current provider bridge, and migrations here are intentionally just
`create table if not exists` plus deterministic seed rows. Sloppy is not full
Node, and this template does not require internet packages, Docker, or external
services.
