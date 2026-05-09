# CORS

`app.useCors(policy)` enables Cross-Origin Resource Sharing for routes
registered after the call. It also auto-registers an `OPTIONS` preflight route
for each subsequent path.

```ts
import { Sloppy, Results } from "sloppy";

const app = Sloppy.create();

app.useCors({
    origins: ["https://app.example.com"],
    credentials: true,
    methods: ["GET", "POST"],
    headers: ["content-type", "x-request-id"],
    exposedHeaders: ["x-trace-id"],
    maxAgeSeconds: 600,
});

app.get("/users", listUsers);
app.post("/users", createUser);
```

## Policy shape

| Field            | Default | Behavior                                                       |
| ---------------- | ------- | -------------------------------------------------------------- |
| `origins`        | —       | Allowed origins. Either an array of explicit origins or `["*"]`. Required. |
| `credentials`    | `false` | Send `Access-Control-Allow-Credentials: true`. Cannot combine with `"*"` origin. |
| `methods`        | `[]`    | Methods exposed to preflight. Empty falls back to the methods registered for the path. |
| `headers`        | `[]`    | Allowed request headers; sent in `Access-Control-Allow-Headers`. |
| `exposedHeaders` | `[]`    | Sent in `Access-Control-Expose-Headers` on actual responses.    |
| `maxAgeSeconds`  | unset   | Non-negative integer for `Access-Control-Max-Age`.              |

`origins`, `methods`, `headers`, and `exposedHeaders` accept a single string
or an array. `origin`, `allowHeaders`, `exposeHeaders`, and `maxAge` are
accepted as aliases.

`origins: ["*"]` allows any origin but rejects `credentials: true` — the
combination is invalid per the CORS spec.

## What it adds to responses

For an allowed origin, every wrapped route response gets:

- `Access-Control-Allow-Origin` — echoed origin, or `*` for the wildcard policy.
- `Vary: Origin` — set when the policy uses explicit origins (varies per origin).
- `Access-Control-Allow-Credentials: true` — only when `credentials: true`.
- `Access-Control-Expose-Headers` — only when `exposedHeaders` is non-empty.

## Preflight (`OPTIONS`)

Each subsequent route registration also installs an `OPTIONS` route at the
same path. The preflight handler answers `204` with:

- `Access-Control-Allow-Origin`
- `Access-Control-Allow-Methods` — the methods registered on this path under
  the current policy (or `policy.methods` if explicitly listed).
- `Access-Control-Allow-Headers` — `policy.headers`, when non-empty.
- `Access-Control-Max-Age` — `policy.maxAgeSeconds`, when set.
- `Vary: Origin, Access-Control-Request-Method, Access-Control-Request-Headers`
  — when origins are explicit.

A preflight request with an origin that isn't allowed, a method that isn't
registered, or a header outside `policy.headers` answers `403`.

If you re-register a route under a different policy on the same path, the
auto-registered preflight throws — install the policy once per path.

## Scope

`useCors` applies to routes registered after it. Route groups and controller
mappers see the *current* app policy at the time each child route registers,
so this is fine:

```ts
const mapper = app.mapController("/users", UsersController, (users) => {
    users.get("/", "list");
});
app.useCors({ origins: ["https://app.example.com"] });
mapper.get("/{id:int}", "get"); // CORS applies to this route
```

## Status

CORS preflight and response-header injection run in the bootstrap app-host
handler path. Compiler extraction and Plan-level CORS metadata for emitted
artifacts are planned in a separate compiler slice, so source-input builds
reject `app.useCors(...)` today instead of silently dropping the policy.
