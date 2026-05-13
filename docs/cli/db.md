# sloppy db

Inspect and apply first-party database migrations for a built app.

```sh
sloppy db status .sloppy --provider main
sloppy db migrate .sloppy --provider main
```

ORM projects can use the ORM migration vocabulary for the same status/apply
engine, plus Plan-derived migration draft generation:

```sh
sloppy orm migration script .sloppy --provider main
sloppy orm migration add CreateUsers .sloppy --provider main
sloppy orm migration status .sloppy --provider main
sloppy orm migration apply .sloppy --provider main
```

`sloppy db` reads provider metadata from `app.plan.json` and migration metadata
from `sloppy.json`. When the target is a package directory, it reads
`manifest.json` and uses the packaged migration files.

## Usage

```sh
sloppy db status [artifacts-dir|package-dir] --provider <name> [--format text|json]
sloppy db migrate [artifacts-dir|package-dir] --provider <name> [--format text|json]
```

If no target is supplied, `.sloppy` is used.

## Provider

`--provider` is the migration provider name from `sloppy.json`, for example
`main`:

```json
{
  "migrations": {
    "main": {
      "provider": "sqlite",
      "path": "migrations/*.sql"
    }
  }
}
```

The command matches that name to the Plan provider token. `main` matches
`data.main`.

## Status

`status` reports each configured migration as:

| Status | Meaning |
| --- | --- |
| `pending` | The file exists but has not been recorded in `_sloppy_migrations`. |
| `applied` | The file name and hash match the recorded migration. |
| `changed` | A migration with the same name was already applied with a different hash. |

A changed migration exits non-zero. Create a new migration file instead of
editing an applied file.

## Migrate

`migrate` applies pending files in lexical order. For SQLite, PostgreSQL, and
SQL Server, each migration is wrapped in its own transaction and recorded in
`_sloppy_migrations` with:

- `id`
- `name`
- `hash`
- `appliedAt`

Running `migrate` again is a no-op when all file hashes match.

SQLite migrations do not require PostgreSQL, SQL Server, libpq, or ODBC.

PostgreSQL and SQL Server migrations are optional provider paths. They require
a live connection string and the matching provider dependency only when the
selected provider uses that database. If the Plan provider has a `configKey`,
`sloppy db` reads the corresponding environment variable. Otherwise it reads
the compiler-generated key:

- `Sloppy__Providers__postgres__<name>__connectionString`
- `Sloppy__Providers__sqlserver__<name>__connectionString`

If PostgreSQL support is unavailable, the command reports that the PostgreSQL
provider is unavailable and points to the optional provider package path when
available or system libpq. If SQL Server support is unavailable, the command
reports that the SQL Server provider is unavailable and points to Microsoft
ODBC Driver 17 or 18 from Microsoft's platform packages or an
organization-managed deployment. Connection strings are redacted.

## Current Limits

- PostgreSQL and SQL Server migrations require live database services and
  provider support only when the selected provider uses those databases.
- Migration paths currently use the project-relative `directory/*.sql` shape.
