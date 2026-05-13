# ORM Basic

This example shows the first-party `sloppy/orm` model and query shape for a
small team/user API.

It covers:

- `table`, `column`, and `relation` declarations
- generated insert, patch, row, and public DTO schemas
- route `.accepts()` / `.returns()` metadata with ORM schemas
- transactional insert with `.returning()`
- split-query collection include with a soft-delete filter
- public DTO projection that excludes a private password hash column

The handlers expect `ctx.db` to be a Sloppy database provider connection. The
same table metadata can generate provider-specific migration SQL through
`orm.migrations.script([Teams, Users], { provider })`.
