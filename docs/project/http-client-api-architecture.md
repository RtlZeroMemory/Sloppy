# HTTP Client API Architecture

Status: CORE-HTTPCLIENT-01.A/B/C contract slice plus CORE-HTTPCLIENT-01.D/E/F/G/I partial
HTTP/1.1 client transport, helper surface, bounded body/deadline semantics, redirects,
pooling, DNS failure mapping, strict network policy, header redaction, doctor/audit
metadata goldens, source examples, and conformance indexing. This document defines the
outbound HTTP client API and policy model, and records the first executable cleartext
HTTP/1.1 request/response lane.

## Goal

`HttpClient` is Slop's backend-oriented outbound HTTP client. It is owned by Slop and is
not a Fetch, Node `http`/`https`, Bun, Deno, or browser compatibility wrapper.

Public import:

```ts
import { HttpClient } from "sloppy/net";
```

Runtime feature id: `stdlib.httpclient`.

`HttpClient` is contract-visible and Plan-visible as `stdlib.httpclient`. The current
runtime lane provides cleartext `http://` HTTP/1.1 request/response execution through the
Slop-owned CORE-NET TCP bridge and activates the `stdlib.net` dependency automatically.
Direct stdlib facade calls fail with `SLOPPY_E_HTTP_CLIENT_FEATURE_UNAVAILABLE` only when
the JS surface is reached without the required runtime bridge. The helper surface now
includes one-shot `text`, `json`, `bytes`, `getJson`, and `postJson` convenience methods
plus request `json` body serialization, bounded async-iterable request body consumption,
response `stream()` iteration over the buffered body, `signal` cancellation, and
`deadline` handling. It also includes bounded redirects, reusable-client per-origin
HTTP/1.1 pooling, strict-network preconnect denial, deterministic DNS-failure mapping, and
cross-origin sensitive-header stripping plus doctor/audit metadata for named, static, and
dynamic outbound clients. V8-enabled builds activate the existing private `__sloppy.net`
bridge when `stdlib.httpclient` is required, because the first HTTP/1.1 transport is built
from the Slop-owned TCP bridge rather than a Fetch or Node adapter. HTTPS/TLS, true
socket-level streaming, proxy policy, automatic compiler target inference, and a separate
HTTP-native intrinsic namespace remain future transport evolution, not requirements for
the HTTP/1.1-first public surface.

## Public Shape

The intended public surface is:

- `HttpClient.get(url, options?)`;
- `HttpClient.post(url, options?)`;
- `HttpClient.request(request)`;
- `HttpClient.text(url, options?)`;
- `HttpClient.json(url, options?)`;
- `HttpClient.bytes(url, options?)`;
- `HttpClient.getJson(url, options?)`;
- `HttpClient.postJson(url, value, options?)`;
- `HttpClient.create(options)`;
- reusable client methods `request`, `get`, `post`, `text`, `json`, `bytes`, `getJson`,
  and `postJson`;
- response helpers `text()`, `json()`, `bytes()`, and `stream()`.

Named clients are framework-owned reusable resources:

```ts
app.httpClient("billing", {
  baseUrl: config.getString("Billing:BaseUrl"),
  timeoutMs: config.getDuration("Billing:Timeout"),
});
```

Framework/runtime code later resolves them through `ctx.clients.http("billing")`. Normal
framework use should prefer named clients instead of per-request ad hoc construction.

## Architecture

The public model is:

```text
HttpClient
  -> HttpPipeline / request policy chain
  -> ConnectionPool
  -> HttpTransport
       -> HTTP/1.1 transport first
       -> HTTP/2 transport later
```

HTTP/1.1 parser, socket, TLS, and pooling details are implementation details. The public
API speaks in request, response, body, pipeline, policy, origin, deadline, and signal
terms so HTTP/2 multiplexing can be added later without a public rewrite.

Reusable clients may opt into a bounded per-origin connection pool with
`pool.maxConnectionsPerOrigin` and `pool.idleTimeoutMs`. The current HTTP/1.1 backend uses
one in-flight request per connection and reuses only safely delimited responses
(`Content-Length` or complete chunked bodies) when the peer does not send
`Connection: close`. Protocol errors, timeout, cancellation, truncated responses, and
close-delimited bodies discard the connection. Those details remain transport internals so
HTTP/2 multiplexing can later replace the backend without changing the public API.

## Request Bodies

A request body source is one of:

- `json`;
- `text`;
- `bytes`;
- `stream`.

Only one body source may be set. Ambiguous combinations fail before native work with
`SLOPPY_E_HTTP_CLIENT_AMBIGUOUS_BODY`.

The current helper lane implements `json`, `text`, `bytes`, and a bounded `stream` source.
`json` serializes with `JSON.stringify`, sets a default `Content-Type: application/json`
when the caller has not provided one, and fails with `SLOPPY_E_HTTP_CLIENT_INVALID_JSON`
before transport work if the value cannot be serialized. `text` sets a default UTF-8 text
content type. `bytes` clones the caller-provided `Uint8Array` before serialization.
`stream` accepts an async iterable of `Uint8Array` chunks, awaits each chunk in sequence,
enforces `maxRequestBytes`, and writes the resulting bounded body through the current
HTTP/1.1 transport. This is backpressure-aware at the JS source boundary, but not yet
socket-level chunked upload streaming.

## Response Bodies

Response bodies are consumed once. `json()`, `text()`, `bytes()`, and stream reads must
produce deterministic already-consumed errors. HTTP/1.1 connection reuse is allowed only
after the response is safely consumed or drained. Dirty, truncated, malformed, cancelled,
timed-out, or partially consumed bodies discard the connection.

`HttpClient.text`, `HttpClient.json`, `HttpClient.bytes`, and `getJson` perform a GET and
consume the response body once. Invalid response JSON fails with
`SLOPPY_E_HTTP_CLIENT_INVALID_JSON`. `postJson` returns the response object so callers can
inspect status and choose which body helper to consume, matching the reusable-client
workflow. `response.stream({ chunkSize })` returns a one-shot async iterable over the
already-buffered response body and marks the response consumed immediately.

## Deadline And Cancellation

- `deadline` is a caller-owned shared budget.
- `timeoutMs` creates an operation-local deadline.
- `signal` cancels externally.
- Timeout and cancellation remain distinguishable.
- Late native completion is cleanup-only and must never double-settle a Promise.

CORE-TIME owns the shared deadline/cancellation vocabulary.

The current lane honors `timeoutMs`, `deadline`, and `signal` for the whole request
operation. Expired deadlines and elapsed `timeoutMs` reject with
`SLOPPY_E_HTTP_CLIENT_REQUEST_TIMEOUT`; cancelled signals reject with
`SLOPPY_E_HTTP_CLIENT_REQUEST_CANCELLED`. The active TCP connection is aborted on timeout
or cancellation, and late transport completion is ignored after Promise settlement.

## TLS Policy

TLS uses operating-system facilities or vetted libraries only. Slop must not implement
custom TLS, custom certificate validation, custom hostname verification, or custom crypto.
Certificate validation and hostname mismatch failures have stable diagnostics:

- `SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE`;
- `SLOPPY_E_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED`;
- `SLOPPY_E_HTTP_CLIENT_TLS_HOSTNAME_MISMATCH`.

Dangerous development escape hatches, if later approved, must be explicit, noisy, gated by
strict package/runtime mode, and impossible to mistake for production behavior.

## Redirects, Pooling, And Redaction

Redirects are bounded and loop-aware. The current lane follows redirects by default with
`redirects.max` defaulting to 5 and rejects loops with
`SLOPPY_E_HTTP_CLIENT_REDIRECT_LOOP` or excess hops with
`SLOPPY_E_HTTP_CLIENT_MAX_REDIRECTS_EXCEEDED`. `GET` and `HEAD` redirects are automatic;
request-body method redirects require explicit `redirects.allowPost`.

Cross-origin redirects strip sensitive headers by default, including `Authorization`,
`Cookie`, `Proxy-Authorization`, common API-key headers, configured `secretHeaders`, bearer
tokens, and token/secret/API-key-shaped names. Callers may set
`redirects.crossOriginSensitiveHeaders: "deny"` to fail with
`SLOPPY_E_HTTP_CLIENT_SENSITIVE_HEADER_STRIPPED` instead of stripping.

Pooling is per origin and bounded. Origin identity currently includes scheme, host, and
port; future TLS/proxy identity extends that key when those backends exist. HTTP/1.1 uses
one in-flight request per connection as an internal detail. If all per-origin slots are in
use, the request fails deterministically with `SLOPPY_E_HTTP_CLIENT_POOL_EXHAUSTED` rather
than creating unbounded sockets.

Strict network policy is opt-in through `network: { strict: true, allow: [...] }`. Strict
mode denies disallowed origins before TCP connect with
`SLOPPY_E_HTTP_CLIENT_STRICT_NETWORK_DENIED`. Ordinary development remains permissive
unless strict policy is configured.

## Plan, Doctor, And Audit

Static outbound targets should become Plan/doctor-visible. Dynamic base URLs or hosts are
represented as dynamic or partial metadata, never guessed as static facts. Strict mode must
be able to deny external HTTP before native connect/TLS work. Doctor/audit output explains
outbound HTTP access without leaking secrets.

The current doctor/audit lane reads explicit Plan metadata for `requiredFeatures:
["stdlib.httpclient"]` and a small `httpClients[]` metadata array used by generated or
hand-authored artifacts. `sloppy doctor` reports Plan-visible HttpClient activation,
named-client metadata, static target visibility, dynamic/partial target markers, and
strict-network metadata without printing raw URLs, headers, cookies, bearer tokens, API
keys, or TLS-sensitive material. `sloppy audit` emits note-level findings for Plan and
named/static target visibility and a warning for dynamic targets that must be checked at
runtime. The compiler does not yet infer static outbound targets from arbitrary
`HttpClient.create(...)` source; that inference remains a follow-up so dynamic source is
not guessed as static.

## Diagnostics

CORE-HTTPCLIENT reserves stable diagnostics for:

- feature unavailable;
- invalid URL, base URL, path join, method, header, and body options;
- ambiguous body options;
- request/response body already consumed;
- response body limit exceeded;
- malformed response and header limits;
- connect, DNS, TLS backend, certificate validation, and hostname failures;
- request timeout and request cancellation;
- redirect loop and max redirects exceeded;
- cross-origin sensitive header stripped or denied;
- pool exhaustion/backpressure;
- strict-mode network denial;
- dynamic target metadata.

## Non-Goals

- no custom TLS, certificate validation, or crypto;
- no HTTP/2 or HTTP/3 in this epic;
- no cookie jar by default;
- no Fetch compatibility promise;
- no Node `http`/`https` compatibility;
- no inbound HTTP server policy duplication;
- no public alpha docs;
- no benchmark or performance claims.
