# TestServices

`TestServices` is Sloppy's first-party disposable infrastructure for
dependency-backed tests. It starts real Docker containers through the Docker
CLI, waits for the matching Sloppy provider or client to prove readiness, and
gives `TestHost` a provider, client, or environment map.
Webhook outbox tests should use TestServices provider connections when proving
PostgreSQL or SQL Server persistence beyond bootstrap fake-provider tests.
It is experimental and opt-in. Default CI must not depend on Docker.

```ts
import { Results, Sloppy, TestHost, TestServices } from "sloppy";

const app = Sloppy.create();
app.get("/health/ready", () => Results.json({ ok: true }));

await using pg = await TestServices.postgres({
    database: "app_test",
});

await using host = await TestHost.create(app, {
    providers: {
        main: pg.provider(),
    },
    config: {
        DATABASE_URL: pg.connectionString,
    },
});

await host.post("/users")
    .json({ email: "ada@example.com" })
    .expectStatus(201);
```

Create the service first, then the host. Dispose the host before the service.
`TestHost` does not own the service unless app code explicitly stores and
disposes it.

## Docker

```ts
const docker = await TestServices.docker.available();

if (!docker.ok) {
    console.log(`SKIPPED: Docker unavailable: ${docker.reason}`);
}

await TestServices.docker.require();
```

`available()` returns `{ ok, available, reason, version }`. `require()` returns
the same object when Docker is usable and throws
`SLOPPY_E_TESTSERVICES_DOCKER_UNAVAILABLE` otherwise.

The backend uses argv calls to the Docker CLI:

- `docker version`
- `docker image inspect` / `docker pull`
- `docker create` / `docker start`
- `docker inspect`
- `docker logs`
- `docker stop` / `docker rm --force`

Arguments are passed as arrays, not shell-concatenated command strings.

## PostgreSQL

```ts
await using pg = await TestServices.postgres({
    image: "postgres:17",
    database: "app_test",
    username: "sloppy",
    password: "sloppy",
    startupTimeoutMs: 30_000,
});
```

Defaults:

| Option | Default |
| --- | --- |
| `image` | `postgres:17` |
| `database` | `app_test` |
| `username` | `sloppy` |
| `password` | `sloppy` |
| `startupTimeoutMs` | `30000` |

The service exposes:

- `connectionString`
- `provider()` for `TestHost.create(..., { providers })`
- `env()` with `POSTGRES_HOST`, `POSTGRES_PORT`, `POSTGRES_USER`,
  `POSTGRES_PASSWORD`, `POSTGRES_DB`, and `DATABASE_URL`
- `exec(sql, params?)`
- `migrate(pathOrGlob)`
- `seed(fn)`
- `reset(options?)`
- `logs({ tail })`
- `diagnostics()`
- `dispose()` / async disposal

Readiness requires the container to start, Docker port mapping to be visible,
the PostgreSQL data provider bridge to be active, and `select 1` to succeed.
If the provider bridge is missing, startup fails with
`SLOPPY_E_TESTSERVICES_PROVIDER_UNAVAILABLE`; it does not return a fake
provider or silently use an in-memory database.

## Redis

```ts
await using redis = await TestServices.redis({
    image: "redis:7-alpine",
    database: 0,
    password: "sloppy",
    startupTimeoutMs: 15000,
});
```

Defaults:

| Option | Default |
| --- | --- |
| `image` | `redis:7-alpine` |
| `database` | `0` |
| `password` | unset |
| `startupTimeoutMs` | `30000` |

The service exposes:

- `url`
- `connectionString`
- `env()` with `REDIS_URL`, `Redis:Url`, and `Sloppy__Redis__main__url`
- `client(name?, options?)`
- `flush()`
- `reset()`
- `logs({ tail })`
- `diagnostics()`
- `dispose()` / async disposal

Readiness requires the container to start, Docker port mapping to be visible,
the Sloppy outbound network bridge to be active, and `PING` to succeed through
the first-party Redis client. If the network bridge is missing, startup fails;
it does not return a fake Redis service.

## SQL Server

```ts
await using sqlServer = await TestServices.sqlServer({
    image: "mcr.microsoft.com/mssql/server:2022-latest",
    database: "app_test",
    username: "sa",
    password: "Strong_test_password_123!",
    startupTimeoutMs: 60_000,
});
```

Defaults:

| Option | Default |
| --- | --- |
| `image` | `mcr.microsoft.com/mssql/server:2022-latest` |
| `database` | `app_test` |
| `driver` | `ODBC Driver 17 for SQL Server` |
| `username` | `sa` |
| `password` | generated strong local-test password |
| `startupTimeoutMs` | `60000` |

The service sets `ACCEPT_EULA=Y`, uses Docker-assigned host ports by default,
creates the database during readiness when needed, and verifies `select 1`.
This PR supports only the built-in SQL Server `sa` login. Passing any other
`username` throws a `TypeError`; custom login/user provisioning is deferred.

`env()` returns `SQLSERVER_HOST`, `SQLSERVER_PORT`, `SQLSERVER_USER`,
`SQLSERVER_PASSWORD`, `SQLSERVER_DATABASE`, and
`SQLSERVER_DRIVER`, and `SQLSERVER_CONNECTION_STRING`.

## TestHost

App-host mode:

```ts
import { Sloppy, TestHost, TestServices } from "sloppy";

const app = Sloppy.create();
await using pg = await TestServices.postgres();

await using host = await TestHost.create(app, {
    providers: {
        main: pg.provider(),
    },
    config: {
        DATABASE_URL: pg.connectionString,
    },
});
```

Artifact/package mode:

```ts
import { TestHost, TestServices } from "sloppy";

await using pg = await TestServices.postgres();

await using host = await TestHost.fromArtifacts(".sloppy", {
    env: pg.env(),
});
```

Loopback package mode:

```ts
import { TestHost, TestServices } from "sloppy";

await using pg = await TestServices.postgres();
await using host = await TestHost.fromPackage("./dist/app", {
    mode: "loopback",
    env: pg.env(),
});
```

## Migrations, Seeds, Reset

`migrate(pathOrGlob)` accepts `.sql` files or existing Sloppy migration globs
such as `migrations/postgres/*.sql`. Lists are sorted before applying so test
setup is deterministic.

```ts
await pg.migrate("migrations/postgres/*.sql");

await pg.seed(async (db) => {
    await db.exec("insert into users (email) values ($1)", ["ada@example.com"]);
});

await pg.reset({ migrate: true });
```

PostgreSQL reset drops and recreates the `public` schema. SQL Server reset
recreates the test database from `master` and reconnects before rerunning
migrations. `reset({ migrate: true })` reruns the migrations previously applied
through the service.

## Diagnostics And Cleanup

`diagnostics()` includes safe metadata:

- kind
- image
- short container id
- container name
- host and mapped port
- startup state
- readiness attempts
- last readiness error
- bounded log tail
- timings
- provider bridge availability

Passwords and connection strings are redacted from diagnostics and startup
errors. `logs({ tail })` also redacts known secrets before returning text.

Cleanup guarantees:

- successful `dispose()` stops and removes the container
- failed startup removes partial containers
- `dispose()` is idempotent
- stop/remove operations are bounded
- remove failures are reported in diagnostics and make `dispose()` throw unless
  `keepContainerOnFailure: true` is set for debugging
- `keepContainerOnFailure: true` keeps the failed container for debugging and
  leaves the name/id in diagnostics

## CI Policy

Container tests are opt-in. Use a gate such as:

```powershell
$env:SLOPPY_TESTSERVICES = "1"
```

Default CI should run the Sloppy artifact bootstrap contract test and report
live container lanes as `SKIPPED` with the exact reason. Do not report
PostgreSQL, SQL Server, or Redis container behavior as `PASS` unless those
containers actually started and provider or client readiness succeeded.

The bootstrap contract uses `sloppy run --artifacts` against
`tests/integration/execution/testservices_runtime`. It verifies the runtime
export, the Docker backend contract, and the provider-unavailable path without
starting a container. Non-V8 builds report that contract as `UNAVAILABLE`.
Real container starts belong in a V8/native-provider lane.

## Limits

- Requires Docker CLI and a reachable Docker daemon.
- Requires the matching Sloppy data provider bridge or Redis network bridge for
  readiness.
- SQL Server images are large and startup can be slow.
- Container tests are slower than app-host tests.
- Docker Compose, S3/MinIO, SMTP, Kubernetes abstractions, Redis cluster,
  Redis sentinel, Redis pub/sub, and Redis streams are not part of this API.
