# HTTP Client Reference

This page lists the first-party outbound HTTP factory surface.

## Imports

```ts
import { Http, HttpClientFactory, TestHttp } from "sloppy/http";
import { Http } from "sloppy";
```

`HttpClient` remains available from `sloppy/net` for direct low-level
transport calls.

## `Http.client(name, options)`

Creates a named client. Names must start with a letter and contain only
letters, digits, `.`, `_`, or `-`.

Options:

| Option | Type | Notes |
| --- | --- | --- |
| `baseUrl` | absolute `http://` or `https://` URL, or `Config.required(...)` in app services | Required. Fragments are rejected. |
| `headers` | object of safe HTTP header names and string values | Defaults applied to every request. |
| `timeoutMs` | positive integer | Default per-request timeout. |
| `pool.maxConnectionsPerOrigin` | positive integer | Defaults to `8`. |
| `pool.idleTimeoutMs` | non-negative integer | Defaults to `30000`. |
| `pool.connectionLifetimeMs` | non-negative integer | Accepted by the factory descriptor and passed to the low-level pool descriptor. |
| `pool.pendingQueueLimit` | non-negative integer | Accepted by the factory descriptor and passed to the low-level pool descriptor. |
| `retry` | `Http.retry.*` policy | Defaults to no retry. |
| `circuitBreaker` | `Http.circuitBreaker(...)` | Optional. |
| `bulkhead` | `Http.bulkhead(...)` | Optional. |
| `metrics` | boolean | `false` disables factory-level metrics collection. |
| `diagnostics` | boolean | `false` disables factory-level diagnostics collection. |

Request helpers:

- `client.request(method, path, options?)`
- `client.get(path, options?)`
- `client.post(path, options?)`
- `client.put(path, options?)`
- `client.patch(path, options?)`
- `client.delete(path, options?)`
- `client.head(path, options?)`

Request options:

- `params`
- `query`
- `headers`
- `json`
- `text`
- `bytes`
- `stream`
- `timeoutMs`
- `deadline`
- `signal`
- `retry`
- `correlationId`

Response helpers:

- `send()`
- `text()`
- `json(schema?)`
- `bytes()`
- `problem()`
- `expectStatus(status)`
- `expectHeader(name, expected)`
- `expectJson(value)`
- `expectProblem(value)`
- `throwOnError()`

## `Http.typedClient(name, options)`

Creates a typed client. `options` accepts the same client options as
`Http.client` plus `endpoints`.

```ts
const Api = Http.typedClient("api", {
  baseUrl: "https://api.internal",
  endpoints: {
    getUser: Http.get("/users/{id}")
      .params(schema.object({ id: schema.string() }))
      .query(schema.object({ include: schema.string().optional() }))
      .returns(200, UserSchema)
      .returns(404, ProblemSchema),
  },
});
```

Endpoint builders:

- `Http.get(path)`
- `Http.post(path)`
- `Http.put(path)`
- `Http.patch(path)`
- `Http.delete(path)`

Endpoint methods:

- `.params(schema)`
- `.query(schema)`
- `.body(schema)`
- `.returns(status, schema?)`

Typed endpoint calls validate path params, query, and request body before a
request is sent. Successful known statuses return the validated response body.
Unknown statuses, non-success statuses, and response schema mismatches throw
`SloppyHttpClientError`.

## Resilience

Retry policies:

- `Http.retry.none()`
- `Http.retry.fixed({ maxAttempts, delayMs, retryOnStatus, retryOnMethods, jitter, allowPostWithIdempotencyKey })`
- `Http.retry.exponential({ maxAttempts, initialDelayMs, maxDelayMs, retryOnStatus, retryOnMethods, jitter, allowPostWithIdempotencyKey })`

Circuit breaker:

```ts
Http.circuitBreaker({
  failureRatio: 0.5,
  minimumThroughput: 20,
  samplingWindowMs: 30000,
  breakDurationMs: 30000,
  halfOpenMaxCalls: 1,
});
```

Bulkhead:

```ts
Http.bulkhead({
  maxConcurrent: 32,
  maxQueue: 128,
  queueTimeoutMs: 1000,
});
```

## Services

Register clients through the service registry:

```ts
app.services.addHttpClient(Http.client("billing", { baseUrl }));
app.services.addHttpClient(BillingTypedClient);
app.services.addHttpClient("billing", { baseUrl });
```

Resolution:

- Named clients: `ctx.services.get("http.billing")`
- Typed clients: `ctx.services.get(BillingTypedClient)`

Duplicate registrations throw.

## TestHost

`TestHttp.mock()` creates an outbound mock client:

```ts
const mock = TestHttp.mock()
  .get("/users/u_1")
  .replyJson(200, { id: "u_1" });

const host = await TestHost.create(app, {
  httpClients: { api: mock },
});
```

Mock features:

- match method/path templates
- return JSON, text, or bytes
- sequence multiple responses for retry tests
- simulate timeout or connection failure
- assert expected calls
- assert no unexpected calls

## Error Codes

Factory errors use `SloppyHttpClientError`.

- `SLOPPY_E_HTTP_CLIENT_DUPLICATE`
- `SLOPPY_E_HTTP_CLIENT_MISSING`
- `SLOPPY_E_HTTP_CONFIG_UNAVAILABLE`
- `SLOPPY_E_HTTP_MISSING_PATH_PARAM`
- `SLOPPY_E_HTTP_REQUEST_VALIDATION_FAILED`
- `SLOPPY_E_HTTP_RESPONSE_VALIDATION_FAILED`
- `SLOPPY_E_HTTP_UNEXPECTED_STATUS`
- `SLOPPY_E_HTTP_STATUS_ERROR`
- `SLOPPY_E_HTTP_RETRY_EXHAUSTED`
- `SLOPPY_E_HTTP_CIRCUIT_OPEN`
- `SLOPPY_E_HTTP_BULKHEAD_REJECTED`
- `SLOPPY_E_HTTP_MOCK_UNEXPECTED_CALL`
- `SLOPPY_E_HTTP_MOCK_EXHAUSTED`
