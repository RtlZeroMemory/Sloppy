# ORM Relations and Includes

This documentation example focuses on relation metadata and include behavior:

- `Users -> team` defaults to a join include.
- `Teams -> users` defaults to a split-query collection include.
- Filtered collection includes can use `where(...).take(...)`.

The app expects `ctx.db` to be supplied by `TestHost` provider overrides or by a
runtime provider configured in the Plan.
