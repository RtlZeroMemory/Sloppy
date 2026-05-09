# HTTP Client

`HttpClient` is the experimental HTTP/1.1 outbound client exposed from
`sloppy` and `sloppy/net`.

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
stdlib lane.

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
`redirects`, `network`, `tls`, `maxRequestBytes`, and `maxResponseBytes`.

Response bodies are consumed once with `response.text()`, `response.json()`,
`response.bytes()`, or `response.stream(options?)`. `HEAD` responses always
produce an empty body.

## Current Boundaries

- `http://` URLs are supported over the runtime TCP bridge.
- `https://` URLs use the experimental private outbound TLS bridge when the
  runtime exposes it. Missing bridge support fails closed with
  `SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE`; HTTP support alone does not
  imply HTTPS support.
- `tls` is experimental. It accepts string path options `caPath`,
  `caBundlePath`, `trustStorePath`, `clientCertificatePath`,
  `clientPrivateKeyPath`, and `clientPrivateKeyPassphrase`, plus boolean
  `insecureSkipVerify`. These option names may still change before a broader
  stability contract exists. Certificate validation failures use
  `SLOPPY_E_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED`; hostname
  mismatches use `SLOPPY_E_HTTP_CLIENT_TLS_HOSTNAME_MISMATCH`. TLS paths and
  passphrases are not echoed in diagnostics. TLS options on `http://` requests
  are invalid rather than ignored.
- Request bodies are bounded before dispatch. Use `maxRequestBytes` and
  `maxResponseBytes` for per-call limits.
- Cross-origin redirects strip sensitive headers by default and can be denied
  when strict redirect policy is configured.
