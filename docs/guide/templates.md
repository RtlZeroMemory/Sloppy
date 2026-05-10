# Templates

`sloppy create` copies a built-in starter into a new directory.

```sh
sloppy create my-api --template api
```

Current public templates are:

| Template | Purpose |
| --- | --- |
| `api` | SQLite-backed API starter with routes, services, provider config, migrations, health, and packaging flow. |
| `minimal-api` | Smallest web API starter. |
| `program` | Program Mode starter. |
| `cli` | CLI-style Program Mode starter. |
| `package-api` | API starter that uses a compatible local pure-JavaScript package. |
| `node-compat` | Program starter using supported Node compatibility shims. |

## API Template Migrations

The `api` template includes:

- `migrations/0001_create_users.sql`
- `sloppy.json` migration metadata for provider `main`
- runtime migration usage through `Migrations.apply(...)`

Build and apply the template database schema:

```sh
sloppy build
sloppy db migrate .sloppy --provider main
```

Package output includes the migration file and migration manifest metadata, so
the same command shape works against `.sloppy/package`.
