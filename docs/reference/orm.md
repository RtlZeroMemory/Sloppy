# ORM Reference

Import the ORM from `sloppy/orm`:

```ts
import { orm, table, column, relation, sql } from "sloppy/orm";
```

## Exports

- `table(name, columns)` creates an immutable table model.
- `column` creates column builders.
- `relation(table, callback)` registers one-to-one or one-to-many metadata.
- `orm.from(table)` starts a query.
- `orm.transaction(db, callback)` delegates to the provider transaction.
- `orm.query(db, sql, mapper?)` runs raw SQL.
- `orm.cursor(db, sql, options?)` opens a raw cursor.
- `orm.sql`, `orm.sql.sqlite`, `orm.sql.postgres`, and
  `orm.sql.sqlserver` create safe raw SQL fragments.
- `orm.migrations` exposes script, snapshot, diff, apply, and status helpers.
- `SloppyOrmError` and `SloppyOrmConcurrencyError` are the public error types.

## Columns

Column types: `text`, `string`, `int`, `integer`, `bigint`, `number`, `float`,
`decimal`, `bool`, `boolean`, `uuid`, `instant`, `timestamp`, `date`, `json`,
`blob`, `bytes`, and `enum(values)`.

Modifiers: `primaryKey`, `notNull`, `nullable`, `unique`, `index`, `default`,
`defaultNow`, `generated`, `references`, `concurrencyToken`, `softDelete`, and
`private`.

## Table Helpers

Tables expose:

- `rowSchema`, `insertSchema`, `patchSchema`
- `publicSchema(columns?)`
- `public(row, columns?)`
- `pick(...columns)`
- `insert(db, values).execute()`
- `insert(db, values).returning()`
- `insertMany(db, rows)`
- `updateById(db, id, patch, options?)`
- `deleteById(db, id, options?)`
- `softDeleteById(db, id, options?)`
- `findById(db, id)`
- `findOne(db, predicate)`
- `exists(db, predicate)`
- `count(db, predicate)`
- `edit(row)`

Rows returned by ORM helpers are immutable plain objects. The ORM has no global
change tracker and no lazy-loading proxies.

## Query Builder

Builder methods: `where`, `select`, `orderBy`, `thenBy`, `skip`, `take`,
`include`, `toList`, `first`, `firstOrDefault`, `single`, `singleOrDefault`,
`any`, `count`, and `cursor`.

Predicate operators: `eq`, `ne`, `gt`, `gte`, `lt`, `lte`, `isNull`,
`isNotNull`, `like`, `ilike`, `in`, `notIn`, `startsWith`, `contains`,
`endsWith`, plus `orm.and`, `orm.or`, and `orm.not`.

## Includes

One-side includes default to a left join. Collection includes default to split
queries and attach frozen arrays. Filtered and limited collection includes stay
split-query so parent rows are not multiplied.

Use `{ strategy: "split" }` or `{ strategy: "join" }` to request a strategy
where supported.

## Migrations

`orm.migrations.script(tables, { provider })` emits deterministic create-table
SQL.

`orm.migrations.snapshot(tables)` returns `sloppy.orm.snapshot.v1` metadata with
a checksum.

`orm.migrations.diff(previousSnapshot, nextTables, { provider })` emits
additive SQL for new tables, columns, and indexes. Destructive changes throw
unless `allowDestructive: true` is passed.

`orm.migrations.apply` and `orm.migrations.status` are the existing Sloppy data
migration functions.

## Error Codes

- `SLOPPY_ORM_UNIQUE_VIOLATION`
- `SLOPPY_ORM_FOREIGN_KEY_VIOLATION`
- `SLOPPY_ORM_NOT_NULL_VIOLATION`
- `SLOPPY_ORM_CONCURRENCY_CONFLICT`
- `SLOPPY_ORM_PROVIDER_ERROR`
