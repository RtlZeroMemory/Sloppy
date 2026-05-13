# Operations Reference

Sloppy's first-party operations layer covers health, readiness, liveness,
startup, metrics, and opt-in management endpoints. The health, metrics, and
management APIs are alpha and should be treated as pre-stable operations
surfaces.

## Public APIs

- `Health.createRegistry()`
- `Health.self()`
- `Health.runtime()`
- `Health.config(requiredKeys)`
- `Health.data(provider)`
- `Health.jobs(resource)`
- `Health.disk(options)`
- `Health.memory(options)`
- `Health.http(url, options)`
- `Health.tcp(host, port, options)`
- `Metrics.createRegistry()`
- `Metrics.counter(name, options)`
- `Metrics.gauge(name, options)`
- `Metrics.histogram(name, options)`
- `app.health()`
- `app.management(options)`

## Kubernetes Probes

Use distinct endpoints for distinct probe meanings:

```yaml
livenessProbe:
  httpGet:
    path: /live
    port: 8080
readinessProbe:
  httpGet:
    path: /ready
    port: 8080
startupProbe:
  httpGet:
    path: /startup
    port: 8080
```

`/live` should stay cheap. `/ready` should include critical dependencies.
`/startup` should reflect bootstrap completion. The app-host default
management checks keep liveness on the cheap `self` check, while readiness and
startup include runtime lifecycle state so shutdown or incomplete startup
returns `unhealthy`.

## Prometheus

Expose `/_sloppy/metrics` behind the same network controls used for other
operational endpoints. The content type is:

```text
text/plain; version=0.0.4; charset=utf-8
```

Sloppy metric names may contain dots for API readability. Prometheus output
renders dots and dashes as underscores.

Built-in app-host metrics include HTTP request totals, active requests,
route-pattern hits, request and response bytes, status totals, error totals,
duration histograms, runtime uptime, shutdown state, memory gauges, and route
table size.

Native runtime surfaces also expose a C registry for dispatch and provider
metrics. Dispatch labels use route patterns. Provider executor metrics use
provider kind and instance labels, and cover database operation totals, errors,
timeouts, pool active/idle state, pool exhaustion, and queue depth.

## Security

Do not expose detailed health, info, runtime, or metrics endpoints directly to
the public internet. Use `app.management({ protect })` or an external ingress
policy.

Compiler-generated management metadata cannot express a `protect` hook. Keep
those unprotected routes to test-only audit fixtures, or use the bootstrap
app-host `app.management({ protect })` API for recommended in-process
protection.

Operations output redacts common secret-bearing keys. Application-specific
checks should still avoid returning sensitive raw values.

## Boundaries

This backend does not include a web UI dashboard or OpenTelemetry exporter.
Live database and scheduler checks depend on the configured provider or
scheduler resource supplied by the app. When a resource is unavailable, the
corresponding health check returns an explicit unavailable/unhealthy result
instead of fabricating success.
