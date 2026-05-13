# ORM Migrations

This example shows the ORM migration draft workflow for a built app.

```powershell
sloppy build
sloppy orm migration script .sloppy --provider main
sloppy orm migration add CreateUsers .sloppy --provider main
sloppy orm migration status .sloppy --provider main
sloppy orm migration apply .sloppy --provider main
```

`script` and `add` read static ORM table metadata from `app.plan.json`.
Review the generated SQL before applying it.
