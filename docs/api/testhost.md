# TestHost

`TestHost` is Sloppy's first-party API test harness. It gives tests a plain
JavaScript API with no required test runner dependency.

```ts
import { Results, Sloppy, TestHost } from "sloppy";

const app = Sloppy.create();

app.post("/users", (ctx) => Results.json({
    email: ctx.request.json().email,
}, { status: 201 }));

await using host = await TestHost.create(app);

const response = await host
    .post("/users")
    .json({ email: "ada@example.com" })
    .expectStatus(201);

await response.expectJson({ email: "ada@example.com" });
```

The API works from `node:test`, Vitest, Bun tests, or plain scripts. Assertions
throw normal JavaScript errors.

## Creation

```ts
await TestHost.create(app);
await TestHost.create(app, { mode: "inProcess" });
await TestHost.fromArtifacts(".sloppy");
await TestHost.fromPackage("./dist/package");
await TestHost.fromArtifacts(".sloppy", { mode: "loopback" });
```

`TestHost.create(app)` runs the JavaScript app-host pipeline in memory. It is
for fast framework tests around routes, middleware, auth policies, health
routes, request bodies, response descriptors, and service scopes.

`TestHost.fromArtifacts(path)` and `TestHost.fromPackage(path)` run requests
through `sloppy run --once`. Those requests load the Plan, validate artifacts,
initialize the runtime, enter V8 when available, use native dispatch, apply
native request validation, and serialize responses through the runtime writer.

`mode: "loopback"` is available for artifacts and packages. It starts
`sloppy run` on a reserved localhost port and sends real HTTP/1.1 requests to
the listener. `host.baseUrl` and `host.port` expose the selected endpoint.

Pass `cliPath`, `cwd`, `env`, `timeoutMs`, or `port` when the test needs a
specific Sloppy executable or process environment.

## Requests

Every verb helper returns a request builder. Builders are awaitable, so existing
`await host.get("/health")` style remains valid.

```ts
await host.get("/users/42").query({ include: "roles" });

await host.post("/users")
    .header("x-trace", "test-1")
    .cookie("session", "s_1")
    .json({ name: "Ada" });

await host.put("/users/42").text("updated");
await host.patch("/avatar").bytes(new Uint8Array([1, 2, 3]));
await host.post("/login").form({ email: "ada@example.com" });
```

Supported methods are `GET`, `POST`, `PUT`, `PATCH`, `DELETE`, `OPTIONS`, and
`HEAD`.

Auth helpers set normal request state:

```ts
await host.get("/me").bearer(token);
await host.get("/admin").apiKey("test-key");
await host.get("/me").withJwt({ sub: "u_1" }, { secret: "test-secret" });
await host.get("/admin").asUser({ sub: "u_1", roles: ["admin"] });
```

`asUser` is app-host only. Artifact and loopback modes must use headers,
cookies, API keys, or JWTs so the production auth pipeline still runs.

## WebSockets

`TestHost.create(app)` supports in-memory WebSocket connections for
`app.websocket(...)` routes:

```ts
await using host = await TestHost.create(app);

const ws = await host.websocket("/ws")
    .origin("https://app.example.com")
    .protocols(["sloppy.realtime"])
    .asUser({ sub: "u_1", scopes: ["realtime"] })
    .connect();

await ws.sendText("hello");
await ws.expectText("echo:hello");

await ws.sendJson({ type: "ping" });
await ws.expectJson({ type: "pong" });

await ws.close();
```

The WebSocket builder supports `.header(...)`, `.headers(...)`, `.origin(...)`,
`.protocols(...)`, `.bearer(...)`, `.apiKey(...)`, `.withJwt(...)`,
`.withSession(...)`, `.asUser(...)`, and `.timeout(...)`.
`.protocols(...)` validates each value as a WebSocket subprotocol token.
`ws.expectJson(...)` accepts JSON messages and text messages containing JSON; it
rejects binary, ping, pong, and close frames with a clear assertion error.

Rejected handshakes use `expectRejected(status)`:

```ts
await host.websocket("/secure/ws")
    .connect()
    .expectRejected(401);
```

App-host WebSocket tests run the normal route middleware and auth ordering
before the socket handler accepts. Origin and subprotocol policy are checked
before the handler runs. Message size and send-queue limits are enforced by the
in-memory socket.

Artifact and package hosts do not connect their WebSocket helper to native
`sloppy run` yet. `host.websocket(...).connect()` rejects with `501` and
records `SLOPPY_E_TESTHOST_WEBSOCKET_UNSUPPORTED` unless a supplied runtime
host implements `websocketConnect`.

## Responses

Responses expose status, case-insensitive headers, and body readers:

```ts
const response = await host.get("/health");

response.status;
response.headers.get("content-type");
await response.text();
await response.json();
await response.bytes();
```

Assertion helpers return the response:

```ts
await host.get("/health").expectStatus(200);
await host.get("/health").expectHeader("content-type", /json/);
await host.get("/health").expectJson({ status: "healthy", checks: [] });
await host.get("/missing").expectProblem({ status: 404 });
await host.delete("/users/42").expectNoBody();
```

`HEAD`, `204`, and `304` responses expose no body.

## Overrides And Test Data

Use TestHost options for per-host app-host overrides:

```ts
const host = await TestHost.create(app, {
    config: {
        "Feature:Enabled": true,
    },
    secrets: {
        JWT_SECRET: "test-secret",
    },
    services: {
        mail: FakeMail.sink(),
    },
    providers: {
        main: TestData.sqliteMemory(),
    },
});
```

Config and secret overrides are visible through `ctx.config` during app-host
requests. Service overrides are resolved before the app's service provider for
matching tokens. Provider overrides are exposed under both the provider name
and `data.<name>` service token.

`FakeClock.fixed(...)` implements Sloppy's test clock shape for app-host code
that accepts clock injection.

`TestData.sqliteMemory()` and `TestData.sqliteTempFile()` create test data
provider descriptors with `open()` helpers. SQLite native bridge availability
still depends on the active runtime lane.

For real PostgreSQL and SQL Server integration tests, use
[`TestServices`](testservices.md) (experimental):

```ts
import { Sloppy, TestHost, TestServices } from "sloppy";

const app = Sloppy.create();
await using pg = await TestServices.postgres();

await using host = await TestHost.create(app, {
    providers: {
        main: pg.provider(),
    },
});
```

Artifact, package, and loopback hosts receive service environment through
normal `TestHost` options:

```ts
import { TestHost, TestServices } from "sloppy";

await using pg = await TestServices.postgres();
await using host = await TestHost.fromArtifacts(".sloppy", {
    env: pg.env(),
});
```

## Diagnostics, Health, Metrics, Jobs, OpenAPI

App-host tests can assert health endpoints through normal requests:

```ts
await host.get("/health/ready").expectStatus(200);
await host.health.expect("database", "healthy", "/health/ready");
```

Diagnostics, metrics, and job hooks are runner-neutral JavaScript helpers:

```ts
host.diagnostics.expectCode("SLOPPY_E_JSON_INVALID");
host.diagnostics.expectNoSecretLeaks();

host.metrics.expectCounter("http.requests.total", 1, {
    method: "GET",
    status: "200",
});

host.jobs.enqueue("send-welcome-email", { email: "ada@example.com" });
host.jobs.expectEnqueued("send-welcome-email");
```

`host.openapi()` returns a minimal app-host OpenAPI snapshot for dynamic apps.
Artifact and package hosts call `sloppy openapi` through Sloppy's process API.

## Cleanup

Call `host.close()` or `host.dispose()`. `await using` calls async disposal when
the JavaScript runtime supports it. Cleanup is idempotent.

Loopback hosts stop their child server process. Artifact/package one-off CLI
requests create per-request temporary body files only when needed and delete
them after dispatch.

## Hard Limits

- `TestHost.create(app)` is an app-host test lane, not a native runtime lane.
- Artifact and package one-off CLI mode starts `sloppy run --once` per request.
- Loopback mode requires an artifact or package path.
- WebSocket helpers are app-host only unless the supplied runtime host provides
  a `websocketConnect` implementation. Artifact/package WebSockets use an
  explicit unsupported diagnostic.
- Multipart request builder sugar is not exposed yet.
- Explicit multipart bytes plus a `Content-Type: multipart/form-data; boundary=...`
  header are supported through `.bytes(...)` or request `body` options.
- App-host multipart parsing is a bounded helper for form fields and file
  descriptors; it is not binary-fidelity upload testing unless the native
  app-host upload lane implements that behavior.
- Docker-backed PostgreSQL and SQL Server helpers are opt-in through
  `TestServices`; default CI must report those live container lanes separately.
