# Add Authentication To An API

This guide shows a small API protected with JWT bearer tokens, API keys, and a
named authorization policy.

Auth is public-alpha/experimental in this pre-alpha runtime.

## Configure Secrets

Keep auth secrets in application configuration, not source code.

```json
{
  "Auth": {
    "JwtSecret": "replace-in-your-environment",
    "ApiKey": "replace-in-your-environment"
  }
}
```

Use environment-specific config or your deployment secret store for real
values.

## Register JWT Bearer Auth

```ts
import { Sloppy, Results, Auth, Config } from "sloppy";

const app = Sloppy.create();

app.use(Auth.jwtBearer({
  issuer: "sloppy.local",
  audience: "api",
  secret: Config.required("Auth:JwtSecret"),
}));

app.get("/me", (ctx) => Results.ok({
  subject: ctx.user.sub,
  roles: ctx.user.roles,
})).requireAuth();
```

Requests to `/me` now need `Authorization: Bearer <jwt>`. The token must be
HS256-signed with the configured secret and match the configured issuer and
audience.

## Require Roles

```ts
app.get("/admin", () => Results.ok({ ok: true }))
  .requireAuth({ role: "admin" });
```

Missing or invalid credentials return `401`. A valid user without the required
role returns `403`.

## Add API Key Auth

```ts
app.use(Auth.apiKey({
  header: "x-api-key",
  validate: (key) => key === Config.required("Auth:ApiKey"),
}));

app.get("/internal/status", () => Results.ok({ ok: true }))
  .requireAuth();
```

API-key validators can return `true` or a claims object. Use plain strings
inside custom comparisons; `Config.required("...")` is recognized for the
simple config-backed equality shape shown above.

```ts
validate: (key, helpers) =>
  helpers.constantTimeEquals(key, expectedKey)
    ? { sub: "internal-client", roles: ["internal"] }
    : false
```

## Add A Policy

```ts
app.auth.addPolicy("admin-or-ops", (user) =>
  user.roles.includes("admin") || user.claims.department === "ops",
);

app.get("/ops", () => Results.ok({ ok: true }))
  .requireAuth({ policy: "admin-or-ops" });
```

Policies receive `ctx.user` and may inspect roles and claims.

## Protect A Route Group

```ts
const internal = app.group("/internal").requireAuth();

internal.get("/status", () => Results.ok({ ok: true }));
```

Every route registered through `internal` inherits the group requirement unless
the route declares its own `requireAuth(...)` requirement.

## Build-Time Metadata

When the compiler can see static auth setup, the Plan records auth schemes,
route requirements, policy names, and required config keys. It does not record
secret values.

`sloppy openapi --plan .sloppy/app.plan.json` emits `bearerAuth` and
`apiKeyAuth` security schemes for protected routes.

`sloppy audit --plan .sloppy/app.plan.json` warns when a protected route has no
auth scheme metadata.

## Current Limits

- JWT support is HS256 only.
- OIDC discovery, JWKS, OAuth flows, and session cookies are not implemented.
- `sloppy run --once` has a minimal synthetic request path and is not the best
  interface for manual auth-header testing.
