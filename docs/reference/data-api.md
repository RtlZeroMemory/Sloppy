# Data API Reference

Data APIs are implemented in `stdlib/sloppy/data.js`.

Import forms:

```ts
import { data, sql } from "sloppy";
// or
import { data, sql } from "sloppy/data";
```

## sql Tagged Template

Default tag:

```ts
const q = sql`select * from users where id = ${id}`;
```

Lowered query shape:

- `__sloppyQuery: true`
- `text`
- `parameters`
- `parameterCount`
- `placeholderStyle`
- `placeholders`

Lowering helper:

- `sql.lower(strings, values?, { placeholderStyle })`
- placeholder styles: `question`, `postgres`, `named`

## Typed SQL Value Wrappers

`sql` helpers:

- `decimal`, `uuid`, `date`, `time`, `timestamp`, `instant`, `offsetDateTime`
- `json`, `rawJson`, `bytes`

Wrapper objects are frozen and carry DB-value markers used by provider bridges.

## Provider Objects

`data` exports:

- `sqlite` (callable provider shortcut + `.open(...)`)
- `postgres.open(...)`
- `sqlserver.open(...)`
- `createFakeProvider(...)`

Common operation methods:

- `query(...)`
- `queryOne(...)`
- `exec(...)`
- `transaction(async (tx) => ...)`

Nested transactions are rejected. Transaction objects are invalid after callback completion.

## Operation Input Forms

Provider operations accept:

- tagged template call style
- lowered query object (`sql\`...\`` result)
- SQL string + optional params array + optional operation options

Operation options currently support:

- `deadline`
- `signal`
- `timeoutMs`

Unsupported option keys are rejected.

## sqlite.open Options

Required/allowed:

- `database` or `path` (non-empty string)
- `capability` (non-empty string)
- `access`: `read` | `write` | `readwrite` (default `readwrite`)

If both `database` and `path` are provided they must match.

## postgres.open / sqlserver.open Options

Postgres:

- `connectionString` (required, non-empty)
- `access`: `read` or `readwrite`
- `maxConnections`: integer `1..16` (default `1`)

SQL Server:

- `connectionString` (required, non-empty ODBC string)
- `access`: `read` or `readwrite`
- `maxConnections`: integer `1..16` (default `1`)

## Runtime Feature Gate

Without active V8 provider bridges, provider `open(...)` calls fail with unavailable-runtime-feature errors (with redacted sensitive values in messages).

## Fake Provider API

`data.createFakeProvider(definition)` is for deterministic test/runtime-shape simulation.

Definition methods:

- `query`, `queryOne`, `exec`
- optional transaction hooks or transaction handler

Fake-provider evidence is not live database evidence.

## Limits

- No ORM/migration API in this surface.
- No prepared statement handle API in sqlite surface.
- No claim of automatic retry/pool tuning beyond exposed bounded options.
