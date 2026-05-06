# HTTP Client API Architecture

Status: CORE-HTTPCLIENT-01.A/B/C contract slice plus CORE-HTTPCLIENT-01.D/E partial
HTTP/1.1 client transport and helper surface. This document defines the outbound HTTP
client API and policy model, and records the first executable cleartext HTTP/1.1
request/response lane.

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
plus request `json` body serialization. HTTPS/TLS, redirects, pooling, streaming bodies,
and named-client doctor metadata remain deferred to later CORE-HTTPCLIENT-01 slices.

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
- response helpers `text()`, `json()`, `bytes()`, and later `stream()`.

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

The current HTTP/1.1 transport uses one request per TCP connection and closes the
connection after the response. That is an implementation detail of the first transport
lane, not a public pooling contract.

## Request Bodies

A request body source is one of:

- `json`;
- `text`;
- `bytes`;
- `stream`.

Only one body source may be set. Ambiguous combinations fail before native work with
`SLOPPY_E_HTTP_CLIENT_AMBIGUOUS_BODY`.

The current helper lane implements `json`, `text`, and `bytes`. `json` serializes with
`JSON.stringify`, sets a default `Content-Type: application/json` when the caller has not
provided one, and fails with `SLOPPY_E_HTTP_CLIENT_INVALID_JSON` before transport work if
the value cannot be serialized. `text` sets a default UTF-8 text content type. `bytes`
clones the caller-provided `Uint8Array` before serialization. `stream` remains reserved
for CORE-HTTPCLIENT-01.F and fails with `SLOPPY_E_HTTP_CLIENT_FEATURE_UNAVAILABLE` in this
lane.

## Response Bodies

Response bodies are consumed once. `json()`, `text()`, `bytes()`, and stream reads must
produce deterministic already-consumed errors. HTTP/1.1 connection reuse is allowed only
after the response is safely consumed or drained. Dirty, truncated, malformed, cancelled,
timed-out, or partially consumed bodies discard the connection.

`HttpClient.text`, `HttpClient.json`, `HttpClient.bytes`, and `getJson` perform a GET and
consume the response body once. Invalid response JSON fails with
`SLOPPY_E_HTTP_CLIENT_INVALID_JSON`. `postJson` returns the response object so callers can
inspect status and choose which body helper to consume, matching the reusable-client
workflow.

## Deadline And Cancellation

- `deadline` is a caller-owned shared budget.
- `timeoutMs` creates an operation-local deadline.
- `signal` cancels externally.
- Timeout and cancellation remain distinguishable.
- Late native completion is cleanup-only and must never double-settle a Promise.

CORE-TIME owns the shared deadline/cancellation vocabulary.

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

Redirects are bounded and loop-aware. Cross-origin redirects strip or deny sensitive
headers by default, including `Authorization`, `Cookie`, `Proxy-Authorization`, configured
secret headers, bearer tokens, API keys, config-backed secret values, and TLS-sensitive
material. POST redirect behavior must be explicit.

Pooling is per origin and bounded. Origin identity includes scheme, host, port, TLS policy,
and proxy identity. HTTP/1.1 uses one in-flight request per connection as an internal
detail. Pool exhaustion/backpressure is deterministic.

## Plan, Doctor, And Audit

Static outbound targets should become Plan/doctor-visible. Dynamic base URLs or hosts are
represented as dynamic or partial metadata, never guessed as static facts. Strict mode must
be able to deny external HTTP before native connect/TLS work. Doctor/audit output explains
outbound HTTP access without leaking secrets.

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
