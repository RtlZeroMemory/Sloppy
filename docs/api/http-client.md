# HTTP Client

Sloppy has two outbound HTTP layers:

- `Http` from `sloppy/http` is the application-facing factory for named
  clients, typed clients, resilience policies, DI registration, metrics, and
  TestHost mocks.
- `HttpClient` from `sloppy/net` is the low-level transport client. It owns
  protocol selection, bounded bodies, redirects, TLS options, and pooled
  reusable connections.

Use `Http` for repeated backend calls. Use `HttpClient` when you need direct
transport control in a small script or a low-level runtime test.

```ts
import { Http } from "sloppy/http";

const billing = Http.client("billing", {
  baseUrl: "https://billing.internal",
  timeoutMs: 2000,
  retry: Http.retry.exponential({
    maxAttempts: 3,
    retryOnStatus: [408, 429, 500, 502, 503, 504],
  }),
  pool: {
    maxConnectionsPerOrigin: 32,
    idleTimeoutMs: 60000,
    connectionLifetimeMs: 300000,
  },
});

const invoice = await billing
  .get("/invoices/{id}", { params: { id: "inv_1" } })
  .json(InvoiceSchema);
```

Do not create ad-hoc clients per request for repeated outbound calls. Named
clients centralize configuration and reuse the low-level `HttpClient.create`
transport resources.

## Named Clients

`Http.client(name, options)` creates a reusable named client. Client names must
start with a letter and contain only letters, digits, `.`, `_`, or `-`.

```ts
const github = Http.client("github", {
  baseUrl: "https://api.github.com",
  timeoutMs: 3000,
  headers: {
    "User-Agent": "sloppy-app",
  },
  bulkhead: Http.bulkhead({ maxConcurrent: 32, maxQueue: 128 }),
});

const repo = await github
  .get("/repos/{owner}/{repo}", {
    params: { owner: "RtlZeroMemory", repo: "Slop" },
    query: { per_page: 1 },
  })
  .json(RepoSchema);
```

The request builder supports path params, query, headers, JSON/text/bytes
bodies, timeout/deadline overrides, cancellation signals, per-request retry
overrides, and correlation IDs.

Response helpers include `text()`, `json(schema?)`, `bytes()`, `problem()`,
`expectStatus()`, `expectHeader()`, `expectJson()`, `expectProblem()`, and
`throwOnError()`.

## Typed Clients

`Http.typedClient(name, options)` validates outbound input and inbound output
with Sloppy schemas.

```ts
const BillingInvoice = schema.object({
  id: schema.string(),
  status: schema.string(),
  amount: schema.number(),
});

const Billing = Http.typedClient("billing", {
  baseUrl: Config.required("Billing:BaseUrl"),
  timeoutMs: 2000,
  retry: Http.retry.exponential({ maxAttempts: 3 }),
  endpoints: {
    getInvoice: Http.get("/invoices/{id}")
      .params(schema.object({ id: schema.string() }))
      .returns(200, BillingInvoice),
  },
});

const app = Sloppy.create();
app.services.addHttpClient(Billing);

app.get("/invoices/{id}", async (ctx) => {
  const billing = ctx.services.get(Billing);
  const invoice = await billing.getInvoice(
    { id: ctx.route.id },
    { signal: ctx.signal, correlationId: ctx.requestId },
  );
  return Results.json(invoice);
});
```

Typed clients validate params, query, and body values before sending. Responses
are selected by status code. Unknown statuses and schema mismatches throw
`SloppyHttpClientError` with stable error codes.

## Generated Clients

`Http.generateClientFromOpenApi(document, options)` generates a deterministic
Sloppy typed client module from an OpenAPI 3.0.3 document or Sloppy OpenAPI
artifact. The generated module uses first-party `Http.typedClient(...)`,
`Config.required(...)`, and Sloppy `schema.*` declarations.

```ts
import { File } from "sloppy/fs";
import { Http } from "sloppy/http";

const document = await File.readJson(".sloppy/openapi.json");
const generated = Http.generateClientFromOpenApi(document, {
  name: "Billing",
  baseUrlConfigKey: "Billing:BaseUrl",
});

await File.writeText("src/clients/billing.js", generated.source);
```

The generator maps literal paths, methods, path parameters, query parameters,
JSON request bodies, and JSON response schemas where the OpenAPI schema shape
is supported. Unsupported constructs are reported in `generated.warnings` and
as comments in the output; the generator does not invent schemas for shapes it
cannot prove.

## Resilience Policies

The factory includes first-party timeout, retry, circuit-breaker, and bulkhead
policies. Unsafe methods are not retried by default.

```ts
const client = Http.client("orders", {
  baseUrl: "https://orders.internal",
  timeoutMs: 2000,
  retry: Http.retry.exponential({
    maxAttempts: 3,
    initialDelayMs: 100,
    maxDelayMs: 2000,
    jitter: true,
    retryOnStatus: [408, 429, 500, 502, 503, 504],
    retryOnMethods: ["GET", "HEAD", "PUT", "DELETE"],
  }),
  circuitBreaker: Http.circuitBreaker({
    failureRatio: 0.5,
    minimumThroughput: 20,
    samplingWindowMs: 30000,
    breakDurationMs: 30000,
  }),
  bulkhead: Http.bulkhead({
    maxConcurrent: 32,
    maxQueue: 128,
    queueTimeoutMs: 1000,
  }),
});
```

`POST` retries require explicit method configuration and should be paired with
application idempotency, such as an `Idempotency-Key` header. Body streams are
not retried because they may not be replayable.

## Services And TestHost

Register named or typed clients through `app.services.addHttpClient(...)`.
Named clients are available under `http.<name>`. Typed clients are resolved by
the typed client object.

```ts
app.services.addHttpClient(Http.client("billing", {
  baseUrl: "https://billing.internal",
}));

const billing = ctx.services.get("http.billing");
```

Tests can replace outbound clients without external mocking packages:

```ts
const mock = TestHttp.mock()
  .get("/invoices/inv_1")
  .replyJson(200, { id: "inv_1", status: "paid", amount: 42 });

const host = await TestHost.create(app, {
  httpClients: { billing: mock },
});

await (await host.get("/invoices/inv_1").expectStatus(200))
  .expectJson({ id: "inv_1", status: "paid", amount: 42 });

mock.expectCalled("GET", "/invoices/inv_1").expectNoUnexpectedCalls();
```

`TestHost.fromArtifacts(...)`, `TestHost.fromPackage(...)`, and loopback modes
accept the same `httpClients` map. In process-backed modes TestHost starts a
local HTTP mock server for each named client and injects the matching
`<Client>__BaseUrl` config environment variable into the app process, so the
normal outbound `HttpClient` path is exercised.

## Metrics, Diagnostics, And Health

Named clients expose snapshots:

```ts
client.metrics();
client.diagnostics();
client.health();
```

Metrics use bounded labels: client name, method, route template, status,
status class, and outcome. Raw full URLs are not used as metric labels. Query
values are redacted in diagnostics, and `Authorization`, `Cookie`,
`Set-Cookie`, `x-api-key`, and `api-key` headers are redacted.

Pool counters include connections created, reused, closed while idle, rejected
pool acquisitions, active requests, idle connections, and queued requests where
the active transport can observe them.

## Low-Level Transport

`HttpClient` is the experimental outbound HTTP client exposed from
`sloppy` and `sloppy/net`. It uses HTTP/1.1 by default, can negotiate HTTP/2
over HTTPS with ALPN in `auto` mode, and supports explicit HTTP/2 h2/h2c when
requested with `protocol`.

```ts
import { HttpClient } from "sloppy";
```

Compiler source input should import `HttpClient` from `sloppy/net`:

```ts
import { HttpClient } from "sloppy/net";
```

The root export is available to JavaScript app-host code and tests. The
compiler-recognized import path is the `sloppy/net` subpath because the emitted
Plan records outbound HTTP client runtime feature metadata through the network
stdlib. See [Network](network.md) for the full `sloppy/net` surface
(TCP, local IPC, `NetworkAddress`).

Use method helpers for common requests:

```ts
const response = await HttpClient.get("http://api.internal/health");
const secure = await HttpClient.get("https://api.internal/health");
const created = await HttpClient.postJson("http://api.internal/items", { name: "test" });
const updated = await HttpClient.put("http://api.internal/items/1", { text: "value" });
const metadata = await HttpClient.head("http://api.internal/items/1");
```

The static helpers are:

- `HttpClient.request(request, options?)`
- `HttpClient.get(url, options?)`
- `HttpClient.post(url, options?)`
- `HttpClient.put(url, options?)`
- `HttpClient.patch(url, options?)`
- `HttpClient.delete(url, options?)`
- `HttpClient.head(url, options?)`
- `HttpClient.getJson(url, options?)`
- `HttpClient.postJson(url, value, options?)`
- `HttpClient.text(url, options?)`
- `HttpClient.json(url, options?)`
- `HttpClient.bytes(url, options?)`

Create a client when several requests share a base URL, default headers, network
policy, timeout, redirect policy, or connection pool:

```ts
const client = HttpClient.create({
  baseUrl: "http://api.internal/v1/",
  headers: { "user-agent": "sloppy-service" },
  pool: {
    maxConnectionsPerOrigin: 4,
    idleTimeoutMs: 5000,
    pendingQueueTimeoutMs: 1000,
  },
});

const response = await client.patch("/items/1", { json: { enabled: true } });
```

`request` accepts either a URL string or an object with `url`, `method`,
`headers`, `json`, `text`, `bytes`, `stream`, `timeoutMs`, `deadline`, `signal`,
`redirects`, `network`, `tls`, `protocol`, `maxRequestBytes`,
`maxHeaderBytes`, and `maxResponseBytes`. Body sources (`json`, `text`,
`bytes`, `stream`) are mutually exclusive. `stream` accepts an async iterable
of `Uint8Array` chunks, but the client buffers those chunks up to
`maxRequestBytes` before sending the request.

## Protocol Selection

The `protocol` option accepts:

- `"auto"` — the default. HTTPS requests offer ALPN `h2` / `http/1.1` when
  the TLS bridge supports ALPN, use HTTP/2 when the peer selects `h2`, and
  otherwise fall back to HTTP/1.1. HTTP URLs use HTTP/1.1 unless `h2c` is
  requested explicitly.
- `"http/1.1"` — force HTTP/1.1.
- `"h2c"` — use cleartext HTTP/2 prior knowledge for `http://` URLs.
- `"h2"` — use HTTP/2 over TLS for `https://` URLs with ALPN.

Examples:

```ts
const h2c = await HttpClient.get("http://api.internal/health", {
  protocol: "h2c",
});

const h2 = await HttpClient.get("https://api.internal/health", {
  protocol: "h2",
  tls: { trustStorePath: "certs/api-ca.pem" },
});
```

HTTP/2 requests use the normal method helpers, headers, body sources,
timeouts, deadlines, cancellation, redirect policy, strict-network policy, and
body/header limits. Callers do not construct frames manually.

When a created client has `pool` enabled, HTTP/2 h2 and h2c requests to the
same origin reuse a pooled HTTP/2 session and can run as concurrent streams on
that connection. Per-request timeout, deadline, cancellation, redirect,
strict-network, TLS validation, and body/header limits still apply to each
stream. Static helpers without a client pool close their HTTP/2 connection
after the request completes.

Response bodies are consumed once with `response.text()`, `response.json()`,
`response.bytes()`, or `response.stream(options?)`. `response.stream(...)`
chunks the already-buffered response body at the JavaScript boundary; it is not
a live socket stream and does not expose a native Core stream handle. Native
HTTP response descriptor streaming uses Core streams on the server side, but the
HTTP client response iterator remains a bounded body helper. `HEAD` responses
always produce an empty body.

`response.headers` exposes `get(name) → string | null` and
`entries() → [name, value][]`. Reading the body twice rejects with
`SLOPPY_E_HTTP_CLIENT_BODY_CONSUMED`.

## Defaults

| Option | Default |
| --- | --- |
| `maxHeaderBytes` | `16384` (16 KiB) |
| `maxRequestBytes` | `1048576` (1 MiB) |
| `maxResponseBytes` | `1048576` (1 MiB) |
| `redirects.max` | `5` (range 0..20) |
| `pool.maxConnectionsPerOrigin` | `8` (range 1..256) |
| `pool.idleTimeoutMs` | `30000` |
| `pool.pendingQueueTimeoutMs` | `1000` |

Size options accept integers or size strings such as `"4mb"`. Connections are
reused per origin (`scheme://host:port`) when the response is keep-alive
eligible.

## Current Boundaries

- HTTP/1.1 is the default for cleartext `http://` URLs. HTTPS requests with
  `protocol: "auto"` can negotiate `h2` through ALPN and otherwise fall back
  to HTTP/1.1.
- `http://` URLs are supported over the runtime TCP bridge.
- `https://` URLs use the experimental private outbound TLS bridge when the
  runtime exposes it. If the bridge is missing the request errors with
  `SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE`; cleartext HTTP support alone
  does not imply HTTPS support.
- `protocol: "h2"` requires an ALPN-capable outbound TLS bridge and errors
  if the peer does not negotiate `h2`.
- `protocol: "auto"` never downgrades an explicit `h2` request; fallback to
  HTTP/1.1 applies only when the caller leaves protocol selection on `auto`.
- `protocol: "h2c"` is prior-knowledge HTTP/2 over cleartext TCP; it does not
  perform an HTTP/1.1 Upgrade handshake.
- Server push is disabled. The client advertises `SETTINGS_ENABLE_PUSH = 0`.
- The HTTP/2 client accepts informational 1xx response headers and returns the
  final non-1xx response. It validates response `content-length`, enforces a
  conservative frame-size cap until peer SETTINGS are processed, ACKs peer
  SETTINGS, and splits large request header blocks into CONTINUATION frames.
- HTTP/2 client pooling is alpha. The client reuses h2/h2c sessions and can run
  concurrent streams, but it does not yet enforce peer
  `SETTINGS_MAX_CONCURRENT_STREAMS`, track outbound connection/stream send
  windows, delay large request bodies until `WINDOW_UPDATE`, or adjust active
  request send windows after peer `SETTINGS_INITIAL_WINDOW_SIZE`; this is
  tracked in #1015.
- Peer `GOAWAY` retires the pooled HTTP/2 connection and fails active streams
  conservatively. The client does not yet drain already-accepted streams by
  `last_stream_id`; this is tracked in #1015.
- HTTP/3, gRPC, WebTransport, and WebSocket-over-h2 are not included.
- `tls` is experimental. It accepts string path options `caPath`,
  `caBundlePath`, `trustStorePath`, `clientCertificatePath`,
  `clientPrivateKeyPath`, and `clientPrivateKeyPassphrase`, plus boolean
  `insecureSkipVerify`. These option names may still change before a broader
  stability contract exists. Certificate validation failures use
  `SLOPPY_E_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED`; hostname
  mismatches use `SLOPPY_E_HTTP_CLIENT_TLS_HOSTNAME_MISMATCH`. TLS paths and
  passphrases are not echoed in diagnostics. TLS options on `http://` requests
  are invalid rather than ignored.
- Request and response bodies are bounded before JS consumes them. Use
  `maxRequestBytes` and `maxResponseBytes` for per-call limits.
- Cross-origin redirects strip sensitive headers by default and can be denied
  when strict redirect policy is configured.

## Error Codes

`HttpClient` errors are subclasses of `SloppyNetError` (the `sloppy/net`
shared error class). Common codes:

- `SLOPPY_E_HTTP_CLIENT_FEATURE_UNAVAILABLE`
- `SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS`
- `SLOPPY_E_HTTP_CLIENT_INVALID_URL`
- `SLOPPY_E_HTTP_CLIENT_INVALID_JSON`
- `SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE`
- `SLOPPY_E_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED`
- `SLOPPY_E_HTTP_CLIENT_TLS_HOSTNAME_MISMATCH`
- `SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED`
- `SLOPPY_E_HTTP_CLIENT_DNS_FAILED`
- `SLOPPY_E_HTTP_CLIENT_REQUEST_TIMEOUT`
- `SLOPPY_E_HTTP_CLIENT_REQUEST_CANCELLED`
- `SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE`
- `SLOPPY_E_HTTP_CLIENT_REQUEST_BODY_LIMIT`
- `SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT`
- `SLOPPY_E_HTTP_CLIENT_BODY_CONSUMED`
- `SLOPPY_E_HTTP_CLIENT_AMBIGUOUS_BODY`
- `SLOPPY_E_HTTP_CLIENT_POOL_EXHAUSTED`
- `SLOPPY_E_HTTP_CLIENT_MAX_REDIRECTS_EXCEEDED`
- `SLOPPY_E_HTTP_CLIENT_REDIRECT_LOOP`
- `SLOPPY_E_HTTP_CLIENT_SENSITIVE_HEADER_STRIPPED`
- `SLOPPY_E_HTTP_CLIENT_STRICT_NETWORK_DENIED`
