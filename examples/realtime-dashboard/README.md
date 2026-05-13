# Realtime Dashboard

This example shows the current alpha realtime API shape:

- `app.sse("/events", ...)` records and emits an SSE route.
- `app.ws("/socket", ...)` records WebSocket route intent and executes through
  native HTTP/1.1 Upgrade in V8-backed `sloppy run`.
- `Realtime.hub("dashboard")` is an in-process helper, not an external broker.

Build and inspect the route metadata:

```powershell
sloppy build examples/realtime-dashboard/app.js --out .sloppy
sloppy routes .sloppy
sloppy openapi .sloppy
```

Handler execution through `sloppy run --once` requires a V8-enabled build.
The SSE route uses the bounded `Results.stream` descriptor path; it does not
claim production socket backpressure or incremental live push yet.
