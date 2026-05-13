# Cache Basic Example

This example uses `Cache.memory(...)` for cache-aside reads and tag invalidation.

- `GET /users/42` uses `getOrCreate(...)` with a schema and tags.
- `POST /users/42/invalidate` invalidates the user and list tags.

Memory cache is per process. Use `Cache.sqlite`, `Cache.postgres`, or
`Cache.sqlServer` when multiple app instances must share entries.
