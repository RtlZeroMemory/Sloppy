# HTTP Client

Status: Experimental.

`HttpClient` is the cleartext HTTP/1.1 outbound client exposed from `sloppy`
and `sloppy/net`.

```ts
import { HttpClient } from "sloppy";
```

Use method helpers for common requests:

```ts
const response = await HttpClient.get("http://api.internal/health");
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
`redirects`, `network`, `tls`, `maxRequestBytes`, and `maxResponseBytes`.

Response bodies are consumed once with `response.text()`, `response.json()`,
`response.bytes()`, or `response.stream(options?)`. `HEAD` responses always
produce an empty body.

## Current Boundaries

- `http://` URLs are supported over the runtime TCP bridge.
- `https://` URLs fail closed with
  `SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE`; outbound TLS, trust-store
  configuration, custom CA bundles, and client certificates are not implemented
  in this lane.
- `tls` options are validated for shape and then fail closed with
  `SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE` until a real outbound TLS
  backend exists. TLS paths and passphrases are not echoed in diagnostics.
- Request bodies are bounded before dispatch. Use `maxRequestBytes` and
  `maxResponseBytes` for per-call limits.
- Cross-origin redirects strip sensitive headers by default and can be denied
  when strict redirect policy is configured.
