# Health

`Health` provides first-party liveness, readiness, startup, and detailed health
checks for the bootstrap app host. This operations API is alpha: endpoint
shapes are usable for pre-alpha deployments, but they can still change before a
stable release.

```ts
import { Health, Sloppy } from "sloppy";

const app = Sloppy.create();

app.health()
    .check("self", Health.self(), { tags: ["live", "ready", "startup"] })
    .check("db", Health.data(db), { tags: ["ready"], timeoutMs: 1000, critical: true })
    .check("disk", Health.disk({ path: "./data", minFreeBytes: 500_000_000 }), {
        tags: ["ready", "health"],
        critical: false,
        cacheMs: 5000,
    })
    .expose({
        live: "/live",
        ready: "/ready",
        startup: "/startup",
        health: "/health",
    });
```

## Result Model

Every check returns one of:

- `healthy`
- `degraded`
- `unhealthy`

Top-level aggregation is:

- any critical `unhealthy` check makes the response `unhealthy`;
- any non-critical `unhealthy` check makes the response `degraded`;
- any critical `degraded` check with `degradedIsUnhealthy: true` makes the
  response `unhealthy`;
- any other `degraded` check makes the response `degraded`;
- otherwise the response is `healthy`.

The response contains deterministic JSON:

```json
{
  "status": "degraded",
  "durationMs": 13,
  "checkedAtUtc": "2026-05-12T00:00:00.000Z",
  "checks": {
    "db": {
      "name": "db",
      "status": "healthy",
      "durationMs": 4,
      "checkedAtUtc": "2026-05-12T00:00:00.000Z",
      "tags": ["ready"],
      "critical": true,
      "cached": false,
      "timeoutMs": 1000
    }
  },
  "summary": {
    "healthy": 1,
    "degraded": 0,
    "unhealthy": 0
  }
}
```

Health output redacts secret-looking keys such as `password`, `secret`, `token`,
`cookie`, `authorization`, `apiKey`, and `connectionString`.

## Check Options

`app.health().check(name, check, options)` accepts:

- `tags`: mode selectors such as `live`, `ready`, `startup`, and `health`;
- `critical`: `false` converts that check's `unhealthy` result into aggregate
  `degraded`;
- `timeoutMs`: app-host timeout for one check;
- `cacheMs`: app-host cache duration for one check result;
- `degradedIsUnhealthy`: for critical checks, treats `degraded` as aggregate
  `unhealthy`.

## Built-In Checks

`Health.self()` reports that the process is alive.

`Health.runtime()` reports the app-host runtime state and fails when the request
context says startup is incomplete or shutdown has started.

`Health.config(requiredKeys)` checks that required config keys are present. It
reports key names only, not values.

`Health.data(provider)` calls `ProviderHealth.check(provider)`, which runs a
bounded provider `select 1` probe through the existing data provider facade.

`Health.jobs(resource)` reports queue or scheduler state from a resource with a
`state` snapshot. Missing scheduler resources produce a degraded result.

`Health.disk({ path, minFreeBytes })` checks path accessibility and free bytes
when the platform supports `statfs`.

`Health.memory({ degradedRssBytes, unhealthyRssBytes })` checks process memory
when the host exposes `process.memoryUsage()`.

`Health.http(url, options)` and `Health.tcp(host, port, options)` provide
bounded HTTP and TCP dependency probes.

## Compiler Visibility

The bootstrap app host supports every built-in listed above.

Compiler-generated health routes intentionally expose a smaller static surface:
inline check functions plus zero-argument `Health.self()`, `Health.runtime()`,
`Health.memory()`, and `Health.openApi()`. Compiler-visible check options are
`tags`, `critical`, and `degradedIsUnhealthy`.

The compiler rejects app-host-only built-ins such as `Health.config(...)`,
`Health.data(...)`, `Health.disk(...)`, `Health.http(...)`,
`Health.tcp(...)`, `Health.cache(...)`, and `Health.storage(...)` with
`SLOPPYC_E_UNSUPPORTED_HEALTH_CHECKS`. It also rejects compiler-visible
`timeoutMs` and `cacheMs` instead of silently dropping them.

## Endpoint Semantics

`app.health().expose()` registers four GET endpoints:

| Endpoint | Mode | Default checks |
| --- | --- | --- |
| `/live` | liveness | checks tagged `live` |
| `/ready` | readiness | checks tagged `ready` |
| `/startup` | startup | checks tagged `startup` |
| `/health` | detailed | all checks |

`healthy` returns HTTP `200`. `degraded` returns HTTP `200` by default.
`unhealthy` returns HTTP `503`.

`app.mapHealthChecks()` is still supported for the earlier lightweight
`/health`, `/health/live`, and `/health/ready` compatibility API. New code should
use `app.health()`.
