# Dynamic Web Routes

This example mixes one statically understood route with route registrations
that execute at startup.

```powershell
sloppyc build examples/web-dynamic-routes/app.ts --out .sloppy
sloppy routes .sloppy
sloppy openapi .sloppy --output openapi.json
sloppy run .sloppy --once GET /health
```

`/health` has complete route metadata. The loop, computed path, conditional
registration, and dynamic status response stay runnable when V8 is enabled, but
the Plan marks their route metadata as dynamic and emits
`SLOPPYC_W_DYNAMIC_ROUTE` findings. OpenAPI includes only routes it can
represent safely.
