# Sloppy API Starter

This is the recommended public alpha starter for building a backend with
Sloppy. It shows routes, route modules, a small service/repository split,
configuration, SQLite provider metadata, Schema request validation, health
endpoints, and the app package flow.

## Layout

- `src/main.ts` wires the app, SQLite provider, middleware, services, and routes.
- `src/routes/` contains route registration modules.
- `src/services/` contains business logic.
- `migrations/` contains first-party SQLite schema migration files.
- `src/db/` contains runtime migration and repository helpers.
- `src/models/` contains request and response shapes.
- `public/` contains static assets served under `/public`.
- `appsettings*.json` contains runtime configuration.
- `data/` is where the development SQLite file is created.

## Build And Inspect

```sh
sloppy build
sloppy db migrate .sloppy --provider main
sloppy routes .sloppy
sloppy capabilities .sloppy
sloppy doctor .sloppy
sloppy audit .sloppy
```

## Run

```sh
sloppy run .sloppy --once GET /health
sloppy run .sloppy --once GET /users
sloppy run .sloppy --once GET /public/hello.txt
sloppy run .sloppy --once POST /users --json "{\"name\":\"Katherine Johnson\",\"email\":\"katherine@example.test\"}"
printf "name=Katherine Johnson&email=katherine@example.test" > user.form
sloppy run .sloppy --header "content-type: application/x-www-form-urlencoded" --body-file user.form --once POST /users
```

```powershell
Set-Content -Path user.form -Value "name=Katherine Johnson&email=katherine@example.test" -NoNewline
sloppy run .sloppy --header "content-type: application/x-www-form-urlencoded" --body-file user.form --once POST /users
```
You can also run the development server and send requests with your HTTP
client:

```sh
sloppy run .sloppy
curl -X POST http://127.0.0.1:5173/users -H "content-type: application/json" -d "{\"name\":\"Katherine Johnson\",\"email\":\"katherine@example.test\"}"
```

Invalid JSON or invalid `name`/`email` fields return a `400
application/problem+json` validation problem.

Add `app.useErrors(...)` in `src/main.ts` when you want one explicit error
policy for typed exception mappings, request IDs in error bodies, and redacted
error logs.

## Package

```sh
sloppy package
sloppy db migrate .sloppy/package --provider main
sloppy run .sloppy/package --once GET /health
sloppy run .sloppy/package --once GET /users
sloppy run .sloppy/package --once GET /public/hello.txt
```

The alpha package format contains the compiled Sloppy artifacts. The SQLite
database path is configured as `data/app.db`; create that directory when you run
the package from a directory that does not already have it. `/health/ready`
performs a SQLite query after running the template migration.

## Alpha Limits

Sloppy is pre-production alpha software. SQLite is available through Sloppy's
current provider bridge. The template uses `migrations/0001_create_users.sql`
for schema setup and deterministic seed rows in code. Sloppy is not full Node,
and this template does not require internet packages, Docker, or external
services.
