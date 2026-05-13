# ORM

Sloppy ORM is the first-party SQL model and query API. Import it from
`sloppy/orm`:

```ts
import { orm, table, column, relation } from "sloppy/orm";
```

The ORM works with any Sloppy database provider object that exposes the data
provider contract: `query`, `queryOne`, `exec`, `queryCursor`, and
`transaction(callback)`.

## Tables

Define a table with explicit columns:

```ts
const Teams = table("teams", {
  id: column.uuid().primaryKey(),
  slug: column.text().notNull().unique(),
  name: column.text().notNull(),
  createdAt: column.instant().notNull().defaultNow(),
});

const Users = table("users", {
  id: column.uuid().primaryKey(),
  teamId: column.uuid().notNull().references(() => Teams.id),
  email: column.text().notNull().unique(),
  displayName: column.text().nullable(),
  passwordHash: column.text().notNull().private(),
  version: column.int().notNull().concurrencyToken(),
  deletedAt: column.instant().nullable().softDelete(),
  createdAt: column.instant().notNull().defaultNow(),
});
```

Supported column types are `text`, `string`, `int`, `integer`, `bigint`,
`number`, `float`, `decimal`, `bool`, `boolean`, `uuid`, `instant`,
`timestamp`, `date`, `json`, `blob`, `bytes`, and `enum([...])`.

Supported modifiers are `primaryKey()`, `notNull()`, `nullable()`, `unique()`,
`index()`, `default(value)`, `defaultNow()`, `generated()`,
`references(() => Other.id)`, `concurrencyToken()`, `softDelete()`, and
`private()`.

Table and column metadata is frozen. Invalid SQL identifiers, empty table
definitions, mismatched defaults, duplicate concurrency tokens, invalid
soft-delete columns, and unresolved references fail during model creation.

## Schemas

Each table produces Sloppy schemas:

```ts
Users.rowSchema;
Users.insertSchema;
Users.patchSchema;
Users.publicSchema(["id", "email", "displayName"]);
```

`insertSchema` requires non-null fields that do not have defaults.
`patchSchema` makes patchable fields optional and excludes primary keys,
generated columns, and private columns. Patch values of `undefined` throw;
omit the field or set `null` for nullable columns.

Use `public(row, columns)` to return immutable DTO data without private fields:

```ts
return Users.public(user, ["id", "email", "displayName", "createdAt"]);
```

## Relations and Includes

Declare relations explicitly:

```ts
relation(Users, ({ one }) => ({
  team: one(Teams, {
    local: Users.teamId,
    foreign: Teams.id,
  }),
}));

relation(Teams, ({ many }) => ({
  users: many(Users, {
    local: Teams.id,
    foreign: Users.teamId,
  }),
}));
```

Includes use split queries for collection loading:

```ts
const team = await orm
  .from(Teams)
  .where(t => t.id.eq(teamId))
  .include(t => t.users.where(u => u.deletedAt.isNull()).take(100))
  .singleOrDefault(ctx.db);
```

Loaded rows are immutable plain objects. The ORM does not create lazy-loading
proxies or attach row methods.

## Queries

Queries are fluent and explicit:

```ts
const users = await orm
  .from(Users)
  .where(u => u.email.contains("@example.com"))
  .orderBy(u => u.createdAt.desc())
  .select(u => ({ id: u.id, email: u.email }))
  .skip(20)
  .take(20)
  .toList(ctx.db);
```

Available terminal methods are `toList`, `first`, `firstOrDefault`, `single`,
`singleOrDefault`, `any`, `count`, and `cursor`.

Column predicates include `eq`, `ne`, `gt`, `gte`, `lt`, `lte`, `isNull`,
`isNotNull`, `like`, `ilike`, `in`, `notIn`, `startsWith`, `contains`, and
`endsWith`. Use `orm.and`, `orm.or`, and `orm.not` for compound predicates.

## CRUD

Tables expose explicit helpers:

```ts
await Users.insert(ctx.db, {
  id: crypto.randomUUID(),
  teamId,
  email: input.email,
  displayName: input.displayName ?? null,
  passwordHash: input.passwordHash,
  version: 1,
}).execute();

const created = await Users.insert(ctx.db, values).returning();

await Users.updateById(ctx.db, userId, { displayName: "Ada" }, {
  expected: { version: 1 },
});

await Users.deleteById(ctx.db, userId);
await Users.softDeleteById(ctx.db, userId);
```

If a table has a concurrency token, updates increment it unless the patch sets
it explicitly. Supplying `expected: { version }` makes updates and deletes fail
with `SloppyOrmConcurrencyError` when no row matches the expected token.

For local patch construction:

```ts
const edit = Users.edit(row);
edit.set("displayName", null);
await edit.save(ctx.db, { expected: { version: row.version } });
```

## Transactions

`orm.transaction(db, callback)` delegates to the active provider:

```ts
const user = await orm.transaction(ctx.db, async tx => {
  return await Users.insert(tx, values).returning();
});
```

## Cursors

Use `cursor(db, options?)` for incremental reads:

```ts
const cursor = await orm
  .from(Users)
  .select(u => ({ id: u.id, email: u.email }))
  .orderBy(u => u.id.asc())
  .cursor(ctx.db, { batchSize: 512, maxRows: 100000 });
```

The returned cursor is an async iterable with `close()`, `closed`, `selected`,
`columns`, and `columnNames`. Early loop exit closes the provider cursor.
`maxRows` is enforced by the ORM wrapper.

For stream-ready export code, `orm.stream.ndjson(cursor, mapper?)` returns an
incremental async iterable with `application/x-ndjson` metadata:

```ts
const stream = orm.stream.ndjson(cursor, row => ({
  id: row.id,
  email: row.email,
}));
```

This is a cursor adapter, not a claim that `Results.stream` exposes unbounded
incremental JSON streaming. `Results.stream` remains a bounded response
descriptor today.

## Raw SQL

Use `orm.sql` for explicit SQL escape hatches:

```ts
const rows = await orm.query(
  ctx.db,
  orm.sql`select id, email from users where email = ${email}`,
);
```

Provider-specific fragments fail if they run against the wrong provider:

```ts
orm.sql.sqlite`...`;
orm.sql.postgres`...`;
orm.sql.sqlserver`...`;
```

Raw fragments can also be used inside predicates when the provider is known.

## Migrations

Generate provider-specific `create table` scripts from table metadata:

```ts
const script = orm.migrations.script([Teams, Users], { provider: "sqlite" });
```

For built artifacts, the CLI can generate the same kind of review draft from
compiler-emitted ORM Plan metadata:

```sh
sloppy orm migration script .sloppy --provider main
sloppy orm migration add CreateUsers .sloppy --provider main
```

For reviewable migration drafts, capture a deterministic snapshot and diff it
against the next model set:

```ts
const previous = orm.migrations.snapshot([Teams]);
const draft = orm.migrations.diff(previous, [Teams, Users], {
  provider: "postgres",
});

await File.writeText("migrations/20260513_add_users.sql", draft.sql);
```

Snapshots use format `sloppy.orm.snapshot.v1` and include an 8-character
checksum over sorted table, column, index, key, relation-adjacent, private,
soft-delete, and concurrency metadata. Diffs generate additive table, column,
and index SQL for SQLite, PostgreSQL, and SQL Server. Removed tables/columns or
changed column definitions are reported as destructive changes and throw unless
`allowDestructive: true` is passed so callers can inspect the change explicitly.

`orm.migrations.apply` and `orm.migrations.status` delegate to the existing
Sloppy data migration helpers.

## Errors

Provider constraint failures are mapped to Sloppy ORM errors where provider
metadata or message text is recognizable:

- `SLOPPY_ORM_UNIQUE_VIOLATION`
- `SLOPPY_ORM_FOREIGN_KEY_VIOLATION`
- `SLOPPY_ORM_NOT_NULL_VIOLATION`
- `SLOPPY_ORM_CONCURRENCY_CONFLICT`
- `SLOPPY_ORM_PROVIDER_ERROR`

`details` includes the ORM operation, table when known, provider code/state
when available, and the original provider message.

Every `SloppyOrmError` also carries a short `hint` string. Hints point at the
valid table/relation shape, patch rule, provider action, query terminal, or
migration option that usually fixes the error.

## Compiler and Plan

The compiler recognizes `sloppy/orm` imports and generated app JavaScript reads
ORM exports from `globalThis.__sloppy_runtime`. Plan output marks ORM usage as
Plan-visible dynamic metadata:

```json
{
  "features": { "orm": true },
  "strongPlan": { "evidence": { "orm": true } },
  "orm": {
    "mode": "runtime-dynamic",
    "extraction": { "status": "partial" }
  }
}
```

Runtime ORM usage works dynamically even when table and relation definitions are
too dynamic for static extraction. Obvious static `table("name", { ... })`
definitions are emitted in `orm.tables` with column/modifier metadata, while
the overall extraction status remains `partial` until the compiler can prove a
complete table and relation catalog.
