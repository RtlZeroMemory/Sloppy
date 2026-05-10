# Database Migrations

Use first-party migrations when the app owns its database schema. The current
complete migration path is SQLite.

## Add Migration Files

Put SQL files under `migrations/` and use a sortable numeric prefix:

```text
migrations/
  0001_create_users.sql
  0002_add_user_status.sql
```

Files run in lexical order.

## Configure The Project

Add `migrations` to `sloppy.json`:

```json
{
  "entry": "src/main.ts",
  "outDir": ".sloppy",
  "environment": "Development",
  "capabilities": {
    "fs": true
  },
  "migrations": {
    "main": {
      "provider": "sqlite",
      "path": "migrations/*.sql"
    }
  }
}
```

`Migrations.apply(...)` reads migration files through `sloppy/fs`, so apps that
apply migrations at runtime need the `fs` capability.

## Apply At Runtime

```ts
import { Migrations } from "sloppy/data";

export async function migrate(db) {
    await Migrations.apply(db, {
        provider: "main",
        path: "migrations/*.sql",
    });
}
```

`Migrations.apply` creates `_sloppy_migrations`, checks applied hashes, applies
pending files, and returns the number applied and skipped. A second call is a
no-op when the files are unchanged.

Use `Migrations.status` to inspect the same metadata without applying pending
files:

```ts
const status = await Migrations.status(db, {
    provider: "main",
    path: "migrations/*.sql",
});
```

## Apply From The CLI

Build first, then apply against the artifacts:

```sh
sloppy build
sloppy db status .sloppy --provider main
sloppy db migrate .sloppy --provider main
```

Packages carry migration files and manifest metadata:

```sh
sloppy package
sloppy db status .sloppy/package --provider main
sloppy db migrate .sloppy/package --provider main
```

## Failure Rules

- Missing migration directories fail clearly.
- Files are applied in lexical order.
- Each SQLite migration runs in its own transaction.
- A failed migration rolls back and is not recorded.
- Editing an already-applied migration fails with a hash mismatch.
- Re-running after a successful migration skips already-applied files.

## Provider Scope

SQLite migrations are implemented end to end. PostgreSQL and SQL Server remain
metadata/documentation paths for now; default tests do not require live external
databases, and Sloppy does not pretend those migrations executed.
