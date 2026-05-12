# HTTP Client

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
  pool: { maxConnectionsPerOrigin: 4, idleTimeoutMs: 5000 },
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
