# Cache Hybrid PostgreSQL Example

This example shows the app-host shape for a hybrid cache:

- memory cache handles hot in-process hits;
- PostgreSQL distributed cache shares entries across app instances;
- `getOrCreate(...)` coalesces same-process misses.

It requires PostgreSQL provider support, a configured connection string, and a
live PostgreSQL service. Default Sloppy lanes do not require PostgreSQL.
