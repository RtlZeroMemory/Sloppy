# HTTP Client Runtime

The outbound HTTP stack has two layers.

`stdlib/sloppy/net.js` owns the low-level `HttpClient` transport. It validates
URLs, headers, bodies, TLS options, redirect policy, network policy, protocol
selection, timeouts, deadlines, cancellation, body limits, and pooled
connection/session reuse.

`stdlib/sloppy/http.js` owns the app-facing factory layer. It builds named and
typed clients on top of `HttpClient.create(...)`, applies resilience policies,
tracks client-level metrics and diagnostics, registers clients with services,
and provides TestHost mocks.

## Pooling Model

Created low-level clients can own a pool. The pool is keyed by origin:

```text
scheme://host:port
```

HTTP/1.1 connections are reused when the response is keep-alive eligible.
HTTP/2 h2/h2c sessions are reused for the same origin and can carry concurrent
streams where the current runtime path supports it.

Factory named clients use a created low-level client by default, so repeated
calls through the same named client share the pool. Static `HttpClient.get(...)`
helpers do not share a named-client pool.

Observable pool counters:

- `connectionsCreated`
- `connectionsReused`
- `connectionsClosedIdle`
- `connectionsClosed`
- `poolWaitCount`
- `poolRejectedCount`
- `activeRequests`
- `idleConnections`
- `queuedRequests`

The low-level pool currently rejects over-capacity HTTP/1.1 acquisition rather
than keeping a transport-level wait queue. The factory bulkhead policy provides
the app-facing concurrency queue and rejection behavior.

## Request Pipeline

The factory request pipeline is:

1. Expand path templates and append query values.
2. Apply default headers and per-request headers.
3. Enter the bulkhead, if configured.
4. Check circuit state.
5. Send through the low-level client.
6. Retry eligible failures or configured statuses.
7. Record bounded metrics and diagnostics.
8. Return a response wrapper.

Retries are bounded by `maxAttempts`. Unsafe methods are not retried unless the
caller explicitly configures retry methods. Request body streams are not
retried because they may not be replayable.

## Typed Clients

Typed clients are descriptors plus a lightweight proxy. The descriptor keeps
the client name, base options, endpoint contracts, and service token metadata.

When registered with services:

- the named token `http.<name>` resolves to the pooled named client
- the typed client object resolves to a proxy over that named client

TestHost can replace `http.<name>` with a mock while preserving typed-client
validation and resilience behavior.

## Diagnostics And Labels

Diagnostics record the client name, method, route template, and redacted
headers. Sensitive query values are represented by placeholders instead of raw
values.

Metrics avoid raw URLs. Factory metrics use low-cardinality labels:

- client name
- method
- route template
- status
- status class
- outcome

Do not add raw full URL, request ID, user ID, token, or query labels to HTTP
client metrics.
