# API

The Sloppy public API is exposed through a single import:

```ts
import { Sloppy, Results, sql, schema, data, ... } from "sloppy";
```

Most apps need only `Sloppy` and `Results`. Reach for the rest as you need
configuration, services, validation, or data access.

## Reference

- [App](app.md) — `Sloppy.create()`, the builder, modules, freezing
- [Routing](routing.md) — `app.get`/`post`/`put`/`patch`/`delete`, route patterns, groups, controllers
- [Middleware](middleware.md) — `app.use(fn)`, `group.use(fn)`, pipeline order
- [CORS](cors.md) — `app.useCors(policy)`, allowed origins, preflight
- [Health checks](health.md) — `app.mapHealthChecks(options?)`, liveness/readiness
- [App test host](testing.md) — in-memory app-host dispatch for tests
- [Request context](request-context.md) — what's on `ctx` inside a handler
- [Results](results.md) — every response helper
- [Services](services.md) — singleton/scoped/transient DI, disposal
- [Config](config.md) — `addObject`, typed getters, secrets, binding
- [Logging](logging.md) — levels, sinks
- [Capabilities](capabilities.md) — declaring database/provider capabilities
- [Data](data.md) — `data.sqlite`, `data.postgres`, `data.sqlserver`, `sql\`…\`` templates
- [Workers](workers.md) — background services, work queues, cancellation
- [Schema](schema.md) — value validation

## Stability

Surfaces are tagged `Stable`, `Experimental`, or `Planned`. Unmarked surfaces
are stable in the sense that the *shape* won't change underneath you in
pre-alpha — but Sloppy is pre-alpha, so breakage is possible. See
[reference/stability.md](../reference/stability.md) for the current matrix.
