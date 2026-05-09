# Framework v2 SQLite CRUD Example

V8-gated executable Framework v2 SQLite source-input example.
This example uses typed `Body<T>` and `Route<T>` bindings, compiler-inferred SQLite
provider metadata from `Sqlite<"main">`, semantic request types, and the native SQLite
bridge through V8. `appsettings.json` supplies the normal provider config for the
inferred `sqlite/main` provider. It is intentionally small: list users, fetch one user,
and create a user with `Results.created`.

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/framework-v2-sqlite-crud/app.ts --once GET /users
```

The database is `:memory:` and intended for local evidence only. This is not an ORM,
migration system, production database policy, PostgreSQL/SQL Server proof, benchmark,
public release claim, or package-manager behavior.
