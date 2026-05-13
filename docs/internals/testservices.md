# TestServices Internals

`TestServices` is an experimental bootstrap JavaScript layer in
`stdlib/sloppy/testservices.js`. It deliberately stays small: Docker process
control, provider-backed readiness, SQL setup helpers, diagnostics, and
cleanup.

## Architecture

```text
test code
  |
  v
TestServices.postgres/sqlServer
  |
  +-- sloppy/os Process.run -> docker CLI
  +-- sloppy/data provider bridge -> readiness/query/migration/reset
  +-- TestHost provider/env handoff
```

The Docker backend is CLI-based. Sloppy does not depend on npm
`testcontainers`, Docker SDK packages, or shell scripts per app.

## Docker Backend

The backend calls Docker with argv arrays:

- `version --format &#123;&#123;json .&#125;&#125;`
- `image inspect <image>`
- `pull <image>`
- `create --name <name> -e ... -p 127.0.0.1::<port> <image>`
- `start <container>`
- `inspect <container>`
- `logs --tail <n> <container>`
- `stop --time <seconds> <container>`
- `rm --force <container>`

Docker assigns host ports unless the caller supplies `hostPort`. The mapped
port is read from `docker inspect`; TestServices does not reserve a local port
by binding and closing it.

## Readiness

Readiness is provider-backed:

- PostgreSQL opens `data.postgres` and runs `select 1`.
- SQL Server opens `data.sqlserver`, creates the database if missing, and runs
  `select 1`.

If the native provider bridge is unavailable, startup fails with
`SLOPPY_E_TESTSERVICES_PROVIDER_UNAVAILABLE`. This preserves the contract that
TestServices proves real database behavior and never returns a fake provider.

## Lifecycle

Startup records container id/name, mapped port, readiness attempts, last
readiness error, log tail, cleanup errors, and timestamps. Startup failure
removes the partial container unless `keepContainerOnFailure` is set, and
records cleanup failures in diagnostics and startup errors.

Service disposal:

1. closes provider objects returned by `provider()`;
2. stops the container with a bounded timeout;
3. force-removes it;
4. records cleanup failures in diagnostics;
5. marks disposal complete only after removal succeeds.

`dispose()` is idempotent after successful removal and is also attached to
`Symbol.asyncDispose` when the runtime provides it. A remove failure makes
`dispose()` throw unless `keepContainerOnFailure` is set for debugging.

## Migrations And Reset

Glob migrations delegate to `Migrations.apply`. Single `.sql` files are read
through `sloppy/fs` and executed directly. Lists are sorted before applying.

Reset is intentionally provider-specific:

- PostgreSQL drops and recreates `public`.
- SQL Server recreates the test database from `master`, then reconnects before
  rerunning migrations.

## Redaction

Diagnostics and startup errors redact known passwords and provider connection
strings before returning text. The service never places raw passwords in
container names.

Docker logs can still contain database image messages outside Sloppy's
control; TestServices redacts known service secrets before exposing log tails.

## Test Policy

`tests/integration/execution/testservices_runtime` runs through
`sloppy run --artifacts` for default CI. It uses a fake Docker backend for the
deterministic provider-unavailable path and only probes the real Docker CLI as
status. Live containers remain opt-in through an explicit environment gate and
require Docker plus a V8/native-provider runtime.
