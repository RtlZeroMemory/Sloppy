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
- [Auth](auth.md) — experimental JWT bearer, API keys, route authorization, `ctx.user`
- [Health checks](health.md) — `app.mapHealthChecks(options?)`, liveness/readiness
- [Errors](errors.md) — `app.useErrors(...)`, typed mappings, safe logging
- [ProblemDetails](problem-details.md) — problem response descriptor options
- [Request IDs](request-id.md) — request ID middleware and response header behavior
- [Request logging](request-logging.md) — structured request completion logs
- [App test host](testing.md) — in-memory app-host dispatch for tests
- [Request context](request-context.md) — what's on `ctx` inside a handler
- [Results](results.md) — every response helper
- [Network](network.md) — TCP, local IPC (Unix sockets / named pipes), `NetworkAddress`
- [HTTP Client](http-client.md) — outbound HTTP/1.1 and explicit HTTP/2 requests
- [Services](services.md) — singleton/scoped/transient DI, disposal
- [Config](config.md) — `addObject`, typed getters, secrets, binding
- [Logging](logging.md) — levels, sinks
- [Capabilities](capabilities.md) — declaring database/provider capabilities
- [Data](data.md) — `data.sqlite`, `data.postgres`, `data.sqlserver`, `sql\`…\`` templates
- [Workers](workers.md) — background services, work queues, cancellation
- [Schema](schema.md) — value validation
- [Filesystem](filesystem.md) — `sloppy/fs` files, directories, handles, watchers
- [OS](os.md) — `sloppy/os` system info, environment, subprocess, shutdown signals
- [Time](time.md) — `sloppy/time` delays, deadlines, intervals, cancellation, fake clock
- [Crypto](crypto.md) — `sloppy/crypto` random, hashing, HMAC, password, secrets
- [Codec](codec.md) — `sloppy/codec` Base64, hex, UTF-8, binary I/O, gzip, CRC-32
- [FFI](ffi.md) — `sloppy/ffi` typed native interop

## Stability

Surfaces are tagged `Stable`, `Experimental`, or `Planned`. Unmarked surfaces
are stable in the sense that the *shape* won't change underneath you in
pre-alpha — but Sloppy is pre-alpha, so breakage is possible. See
[reference/stability.md](../reference/stability.md) for the current matrix.
