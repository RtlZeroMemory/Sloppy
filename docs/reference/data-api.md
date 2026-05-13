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
- `queryRaw(...)`
- `queryCursor(...)`
- `queryRawCursor(...)`
- `stream(...)` on connection objects as a `queryCursor(...)` alias
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
- `mode` on `query(...)` only: `object` (default) or `raw`
- `batchSize` on cursor APIs: integer `1..4096`
- `maxRows` on `query(...)`, `queryRaw(...)`, and cursor APIs: integer `1..4294967295`

Unsupported option keys are rejected.

## Query Results

`query(...)` returns object rows by default. Each row is a plain object keyed by
column name. The returned row array carries non-enumerable, read-only metadata:

- `mode`: `object`
- `columnNames`: frozen array of column names in result order
- `columns`: frozen array of `{ name, index }`

`queryOne(...)` returns the same object-row shape for a single row, or `null`.

`query(..., { mode: "raw" })` and `queryRaw(...)` return:

- `mode`: `raw`
- `columnNames`: frozen array of column names in result order
- `columns`: frozen array of `{ name, index }`
- `rows`: array of positional value arrays

Duplicate column names are preserved in raw mode. In object-row mode, ordinary
JS property semantics apply and the last duplicate column value wins.

Query materialization is bounded. The default cap is 128 rows, and `maxRows`
changes the cap for one `query` or `queryRaw` call. Exceeding the cap fails the
operation instead of returning a partial row set.

## Cursor Results

`queryCursor(...)` opens an object-row cursor. `queryRawCursor(...)` opens a
raw positional cursor. `queryCursor(..., { mode: "raw" })` is equivalent to
`queryRawCursor(...)`.

Cursor objects expose:

- `next()`, `return()`, `throw()`, and `[Symbol.asyncIterator]()`
- `close()` for explicit cleanup; close is idempotent
- `closed`, `provider`, `mode`, `columns`, and `columnNames`

The cursor owns the active native statement/result and pins the provider
connection until the cursor reaches end-of-stream or closes. Early `for await`
break, consumer errors, iterator `return()`, explicit close, provider close,
and runtime shutdown release or invalidate the native cursor deterministically.
Calling `next()` after close fails deterministically.

Cursor APIs do not apply the materialized 128-row cap by default. Pass
`maxRows` to put a hard cap on a cursor stream. Unsupported cursor option keys
are rejected before provider dispatch. Driver pointers and handles never cross
the JavaScript boundary.

SQLite cursors step a prepared statement incrementally. PostgreSQL cursors use
libpq single-row mode. SQL Server cursors keep an ODBC statement active and
fetch rows incrementally with `SQLFetch`.

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
- `maxConnections`: integer `1..256` (default `1`)

SQL Server:

- `connectionString` (required, non-empty ODBC string)
- `access`: `read` or `readwrite`
- `maxConnections`: integer `1..256` (default `1`)

## Runtime Feature Gate

Without active V8 provider bridges, provider `open(...)` calls fail with unavailable-runtime-feature errors (with redacted sensitive values in messages).

## Fake Provider API

`data.createFakeProvider(definition)` is for deterministic test/runtime-shape simulation.

Definition methods:

- `query`, `queryRaw`, `queryOne`, `exec`
- optional transaction hooks or transaction handler

Fake-provider tests validate runtime shape only. Live database behavior uses
provider integration checks.

## Limits

- No ORM API in this surface.
- No prepared statement handle API in sqlite surface.
- Automatic retry/pool tuning is limited to the exposed bounded options.
- `deadline`, `signal`, and `timeoutMs` are checked before provider dispatch.
  Native `query`, `queryRaw`, and cursor bridge calls also pass finite timeout
  budgets to driver-level interruption. Signals are a pre-dispatch
  cancellation mechanism for data providers.
- HTTP response streaming currently consumes database cursors as async
  iterables; no helper may materialize every cursor row before writing a
  response. Cursor column metadata is stable for future native JSON streaming.
