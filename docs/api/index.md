# API

The Sloppy public API is exposed through a single import:

```ts
import { Sloppy, Results, sql, schema, data, ... } from "sloppy";
```

Most apps need only `Sloppy` and `Results`. Reach for the rest as you need
configuration, services, validation, or data access.

## Reference

- [App](app.md) — `Sloppy.create()`, the builder, modules, OpenAPI docs, freezing
- [Routing](routing.md) — `app.get`/`post`/`put`/`patch`/`delete`, route patterns, groups, controllers
- [Static files](static-files.md) — `app.useStaticFiles(...)`, build-time asset routes
- [Middleware](middleware.md) — `app.use(fn)`, `group.use(fn)`, pipeline order
- [CORS](cors.md) — `app.useCors(policy)`, allowed origins, preflight
- [Auth](auth.md) — experimental JWT bearer, API keys, route authorization, scopes, policies, `ctx.user`
- [Sessions](sessions.md) — signed cookie sessions and CSRF
- [Security headers](security.md) — first-party response security headers
- [Health checks](health.md) — `Health`, `app.health()`, liveness/readiness/startup
- [Metrics](metrics.md) — counters, gauges, histograms, JSON snapshots, Prometheus output
- [Management](management.md) — opt-in actuator-style backend endpoints
- [Errors](errors.md) — `app.useErrors(...)`, typed mappings, safe logging
- [ProblemDetails](problem-details.md) — problem response descriptor options
- [Request IDs](request-id.md) — request ID middleware and response header behavior
- [Request logging](request-logging.md) — structured request completion logs
- [TestHost](testhost.md) — first-party app, artifact, package, and loopback API testing
- [TestServices](testservices.md) — opt-in live PostgreSQL and SQL Server test services
- [App test host](testing.md) — in-memory app-host dispatch for tests
- [Request context](request-context.md) — what's on `ctx` inside a handler
- [Results](results.md) — every response helper
- [Realtime](realtime.md) — SSE route helpers, WebSocket route registration, and in-process hubs
- [WebSockets](websockets.md) — app-host WebSocket socket API, options, TestHost helpers, and runtime limits
- [Network](network.md) — TCP, local IPC (Unix sockets / named pipes), `NetworkAddress`
- [HTTP Client](http-client.md) — outbound HTTP/1.1 and explicit HTTP/2 requests
- [Services](services.md) — singleton/scoped/transient DI, disposal
- [Config](config.md) — `addObject`, typed getters, secrets, binding
- [Logging](logging.md) — levels, sinks
- [Capabilities](capabilities.md) — declaring database/provider capabilities
- [Data](data.md) — `data.sqlite`, `data.postgres`, `data.sqlserver`, `sql\`…\`` templates
- [ORM](orm.md) — `sloppy/orm` tables, columns, schemas, CRUD, includes, and migrations
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
are the current public alpha shape, not a stable compatibility promise. APIs
and artifact formats can still change before a stable release. See
[reference/stability.md](../reference/stability.md) for the current matrix.
