# App test host

For the full first-party testing API, including artifact/package mode and
loopback mode, see [TestHost](testhost.md). This page documents the in-memory
app-host helper that backs `TestHost.create(app)`.

`Testing.createHost(app)` creates an in-memory host for Sloppy app tests. It
dispatches HTTP-like requests through the JavaScript app-host route table,
middleware pipeline, result conversion, CORS handlers, health routes, and
request service scopes without opening a socket.

```ts
import { Sloppy, Results, Testing } from "sloppy";

const app = Sloppy.create();

app.get("/hello/{name}", (ctx) =>
    Results.json({ hello: ctx.route.name })
);

const host = Testing.createHost(app);

const response = await host.get("/hello/Ada");

response.status;                   // 200
response.headers.get("content-type");
await response.json();             // { hello: "Ada" }

await host.close();
```

The bootstrap stdlib also exports `createTestHost` from `sloppy/testing` for
harnesses that load stdlib modules directly. The app test host is for tests; app
source compiled into runtime artifacts should keep importing the framework and
runtime APIs it uses in production.

`Testing` is not a compiler input API. `sloppyc` rejects `import { Testing } from
"sloppy"` with `SLOPPYC_E_UNSUPPORTED_TESTING_IMPORT`; use it from JavaScript
tests that execute the app-host surface.

## Requests

Use method helpers for common cases:

```ts
await host.get("/users/42?include=roles");
await host.post("/users", { json: { name: "Ada" } });
await host.put("/users/42", { text: "updated" });
await host.patch("/users/42", { body: new Uint8Array([1, 2, 3]), headers: {
    "content-type": "application/octet-stream",
} });
await host.delete("/users/42");
await host.options("/users", { headers: {
    Origin: "https://app.example",
    "Access-Control-Request-Method": "GET",
} });
```

The generic form is:

```ts
await host.request("POST", "/users", {
    headers: { "content-type": "application/json" },
    json: { name: "Ada" },
});
```

Request options:

| Option | Behavior |
| --- | --- |
| `headers` | Plain object. Names and values use the same safe HTTP token/value rules as Sloppy result headers where request headers allow managed names such as `content-type`. |
| `json` | Serialized as JSON and assigns `content-type: application/json; charset=utf-8` when absent. |
| `text` | Encoded as UTF-8 text and assigns `content-type: text/plain; charset=utf-8` when absent. |
| `body` | String or `Uint8Array`. Caller headers define the body media type. |

Use one body source per request. The host rejects multiple body sources before
dispatch. Targets must start with `/` and may include a query string.

## Response

Responses are immutable in-memory objects:

```ts
const response = await host.get("/health");

response.status;
response.headers.get("content-type");
Array.from(response.headers.entries());
await response.text();
await response.json();
await response.bytes();
```

`headers.get(name)` is case-insensitive. `headers.entries()` returns lower-case
header names in deterministic order. Body readers are synchronous internally,
but returning them from tests with `await` keeps test code compatible with
other response-shaped helpers.

## What It Exercises

The app test host freezes the app, snapshots routes, and dispatches against
that snapshot. It materializes:

- `ctx.route`
- `ctx.query`
- `ctx.request.method`
- `ctx.request.path`
- `ctx.request.rawTarget`
- `ctx.request.headers`
- `ctx.request.text()`
- `ctx.request.json()`
- `ctx.request.bytes()`
- `ctx.services`
- `ctx.config`, `ctx.log`, and `ctx.capabilities`

Each dispatch creates a request service scope and disposes it after the result,
middleware short-circuit, ProblemDetails response, or thrown handler error
settles.

Missing routes return `404`. Method mismatches return `405`. Malformed JSON
returns `400`. Unsupported request body media types return `415`.

## App-Host And Runtime Lanes

Use the app test host for fast framework and handler tests:

- route matching and route params
- query strings and request headers
- JSON, text, and byte request bodies
- `Results.*` response descriptors
- middleware and group middleware order
- short-circuit responses
- ProblemDetails handler-error mapping
- CORS actual responses and preflight handlers
- health, liveness, and readiness endpoints
- scoped service cleanup

Use `sloppy run --once` when the test needs compiled artifacts, Plan validation,
the native dispatch path, V8 handler execution, generated typed bindings,
runtime provider bridges, or the packaged stdlib layout.

Socket binding, package archives, TLS, keep-alive, streaming transport
behavior, and live database setup are covered by separate integration
checks.

The control-plane app-host test is the largest current app-host example. It
imports `examples/prealpha-control-plane` route modules, mounts a fake SQLite
provider, and checks the app contract before the same project goes through the
compiler, source-input, and V8 checks.

## Lifecycle

Call `host.close()` when a test is finished. It closes the app-level service
provider. Dispatching after close rejects.
