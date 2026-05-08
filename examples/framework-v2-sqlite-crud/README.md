# Framework v2 SQLite CRUD Example

Status: V8-gated executable Framework v2 SQLite source-input example.

This example uses typed `Body<T>` and `Route<T>` bindings, Plan-backed SQLite provider
injection, semantic request types, and the native SQLite bridge through V8. It is
intentionally small: list users, fetch one user, and create a user with
`Results.created`.

```powershell
.\build\windows-relwithdebinfo\sloppy.exe run examples/framework-v2-sqlite-crud/app.ts --once GET /users
```

The database is `:memory:` and intended for local evidence only. This is not an ORM,
migration system, production database policy, PostgreSQL/SQL Server proof, benchmark,
public alpha claim, or package-manager behavior.
