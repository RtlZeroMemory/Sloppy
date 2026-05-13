# Metrics

`Metrics` provides first-party counters, gauges, histograms, timers, JSON
snapshots, and Prometheus text output.

```ts
import { Metrics } from "sloppy";

const orders = Metrics.counter("orders_created_total");
orders.inc();

const queueDepth = Metrics.gauge("queue_depth");
queueDepth.set(42);

const dbQuery = Metrics.histogram("db_query_ms", { buckets: [1, 5, 10, 50, 100] });
dbQuery.observe({ provider: "sqlite" }, 12.5);
```

## Registry

`Metrics.createRegistry()` creates an isolated registry. The top-level
`Metrics.counter`, `Metrics.gauge`, `Metrics.histogram`, and `Metrics.timer`
helpers use the default registry.

Counters are monotonic. Gauges support `set`, `inc`, and `dec`. Histograms
support `observe`, `timer`, and `measure`.

```ts
const registry = Metrics.createRegistry();
const duration = registry.histogram("handler_duration_ms");

await duration.measure(async () => {
    await handleRequest();
}, { route: "/orders/{id}" });
```

## Labels

Labels must have stable names and bounded values. Each metric has a
`maxLabelSets` guard, defaulting to 128. When a metric exceeds its labelset
limit, the registry drops the new labelset and increments
`sloppy_metrics_cardinality_drops_total` in Prometheus output.

Route metrics use route patterns such as `/orders/{id}`, not raw request paths.
Do not place user IDs, request IDs, tokens, cookies, raw URLs, or SQL parameter
values in labels.

## Output

`registry.snapshot()` returns deterministic JSON data:

```json
{
  "metrics": [
    {
      "name": "orders_created_total",
      "type": "counter",
      "description": "",
      "series": [
        { "labels": {}, "value": 1 }
      ]
    }
  ],
  "cardinalityDrops": 0
}
```

`registry.renderPrometheus()` returns Prometheus text format with `HELP` and
`TYPE` lines. Dots and dashes in Sloppy metric names are rendered as underscores
for Prometheus compatibility.

The app test host records built-in HTTP metrics for matched routes:

- `http.requests.total`
- `http.requests.active`
- `http.route.hits`
- `http.request.bytes`
- `http.request.duration.ms`
- `http.response.bytes`
- `http.status.total`
- `http.errors.total`

Rate-limit enforcement emits:

- `rate_limit.requests.total`
- `rate_limit.allowed.total`
- `rate_limit.denied.total`
- `rate_limit.store.errors.total`
- `rate_limit.tokens.remaining`
- `rate_limit.concurrency.active`

Labels are limited to policy name, route pattern, algorithm, store kind, and
outcome. Partition values are hashed for diagnostics and are never metric
labels.

The native dispatch path records the same low-cardinality route-pattern
counters through `SlOpsMetricsRegistry` when a dispatch table is configured
with a registry. Native labels use the compiled route pattern, not the raw
request target.

Management metrics refresh safe runtime gauges before rendering:

- `runtime.uptime.seconds`
- `runtime.memory.rss.bytes`
- `runtime.memory.heap.bytes`
- `runtime.shutdown.state`
- `routing.route_table.size`

Provider executors can publish safe database/provider counters and gauges into
the native registry:

- `db.query.total`
- `db.query.errors`
- `db.timeouts`
- `db.pool.active`
- `db.pool.idle`
- `db.pool.exhausted`
- `db.queue.depth`

## Cache metrics

Cache services registered with `app.services.addCache(...)` publish safe
low-cardinality counters when resolved through the app service provider:

- `cache.gets.total`
- `cache.hits.total`
- `cache.misses.total`
- `cache.sets.total`
- `cache.removes.total`
- `cache.evictions.total`
- `cache.expired.total`
- `cache.tag_invalidations.total`
- `cache.get_or_create.factory.total`
- `cache.stampede.waiters.total`
- `output_cache.requests.total`

Cache labels use cache names, backend kinds, operation names, route patterns,
status classes, and bounded bypass reasons. They do not include raw cache keys,
raw URLs, user IDs, cookies, authorization headers, SQL parameters, or cached
values.

## Webhook metrics

Webhook metrics should use event name, outcome, status class, attempt bucket,
and storage provider labels. Do not use full endpoint URLs, payload fields,
secrets, tenants, or user identifiers as metric labels.

## Redis metrics

Redis clients publish safe client metrics into the default registry and expose
a client-local `metrics()` snapshot:

- `redis.commands.total`
- `redis.command.duration`
- pool active, idle, and queued counts

Redis labels are limited to client name, command, and outcome. Raw Redis keys,
URLs, passwords, tokens, and command arguments must not be metric labels.

## HTTP client metrics

Named clients created through `Http.client(...)` expose a client-local metrics
snapshot:

```ts
const snapshot = ctx.services.get("http.billing").metrics();
```

HTTP client factory counters include:

- `http.client.requests.total`
- `http.client.errors.total`
- `http.client.retries.total`

The snapshot also includes pool counters from the low-level `HttpClient`
transport:

- connections created
- connections reused
- idle closes
- pool wait count
- pool rejected count
- active requests
- idle connections
- queued requests

Client labels are bounded to client name, method, route template, status,
status class, and outcome. Do not use raw full URLs, user IDs, request IDs,
tokens, or query strings as HTTP client metric labels.
