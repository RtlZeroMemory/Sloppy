# sloppy orm

Generate, inspect, and apply ORM migration files through the provider-matched
migration engine:

```sh
sloppy orm migration script .sloppy --provider main
sloppy orm migration add CreateUsers .sloppy --provider main
sloppy orm migration status .sloppy --provider main
sloppy orm migration apply .sloppy --provider main
```

`script` and `add` read compiler-emitted `orm.tables` metadata from
`app.plan.json`. `status` delegates to `sloppy db status`. `apply` delegates
to `sloppy db migrate`. The command exists so ORM projects can keep migration
documentation and scripts under the ORM command vocabulary while using the same
Plan, provider, migration history, hash, and package metadata path as
`sloppy db`.

## Usage

```sh
sloppy orm migration script [artifacts-dir|package-dir] --provider <name> [--format text|json]
sloppy orm migration add <name> [artifacts-dir|package-dir] --provider <name> [--format text|json]
sloppy orm migration status [artifacts-dir|package-dir] --provider <name> [--format text|json]
sloppy orm migration apply [artifacts-dir|package-dir] --provider <name> [--format text|json]
```

If no target is supplied, `.sloppy` is used. See [`sloppy db`](db.md) for the
provider configuration, live dependency, package, and output contracts.

## Script

`script` prints a deterministic provider-specific draft migration for the
static ORM tables the compiler extracted into the Plan:

```sh
sloppy orm migration script .sloppy --provider main
```

JSON output wraps the same SQL in a structured payload:

```sh
sloppy orm migration script .sloppy --provider main --format json
```

The generated SQL is a review draft. It is based on current Plan metadata and
does not inspect live schema state.

## Add

`add` writes the generated script to the configured migration directory from
`sloppy.json`:

```sh
sloppy orm migration add CreateUsers .sloppy --provider main
```

The file name uses the next four-digit prefix and a slugified migration name,
for example `0001_create_users.sql`. Review the generated file before running
`sloppy orm migration apply`.

## JavaScript Helpers

The runtime API can also generate migration SQL directly from table objects:

```ts
const snapshot = orm.migrations.snapshot([Teams, Users]);
const script = orm.migrations.script([Teams, Users], { provider: "sqlite" });
const diff = orm.migrations.diff(previousSnapshot, [Teams, Users], {
  provider: "postgres",
});
```

Use the JavaScript helpers when a test or tool already has direct access to ORM
table objects. Use the CLI when you want the draft migration that matches the
built artifact metadata.
