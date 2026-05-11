# Auth

Sloppy auth is an experimental app-host and compiler-visible API for protecting
routes with JWT bearer tokens, API keys, signed session cookies, roles,
claims, and named policies.

```ts
import { Sloppy, Results, Auth, Config } from "sloppy";

const app = Sloppy.create();

app.use(Auth.jwtBearer({
  issuer: "sloppy.local",
  audience: "api",
  secret: Config.required("Auth:JwtSecret"),
}));

app.get("/me", (ctx) => Results.ok({ subject: ctx.user.sub }))
  .requireAuth();
```

## Providers

### `Auth.jwtBearer(options)`

Registers a JWT bearer provider for `Authorization: Bearer <token>`.

| Option | Type | Notes |
| --- | --- | --- |
| `issuer` | `string?` | Expected `iss` claim. |
| `audience` | `string?` | Expected `aud` claim. Array audiences are accepted when one entry matches. |
| `secret` | `string \| Config.required(...)` | HS256 signing secret. Use config references for app secrets. Compiler extraction requires `Config.required(...)`. |
| `clockSkewSeconds` | `number?` | Integer leeway for `exp`, `nbf`, and future `iat` checks. Defaults to `0`. |

JWT support is intentionally small for alpha:

- supported algorithm: `HS256`;
- supported validation: `iss`, `aud`, `exp`, `nbf`, future `iat`, and `sub`;
- rejected algorithms: `none` and anything other than `HS256`;
- not included: OIDC discovery, JWKS, asymmetric algorithms, refresh tokens, or OAuth flows.

### `Auth.cookieSession(options)`

Registers signed cookie-session auth. The cookie payload is signed with HMAC
SHA-256 using the configured secret. Tampered cookies are rejected.

```ts
app.use(Auth.cookieSession({
  name: "sloppy.session",
  secret: Config.required("Auth:SessionSecret"),
}));

app.post("/login", (ctx) => Auth.signIn(ctx, {
  sub: "user-1",
  roles: ["user"],
  claims: { email: "ada@example.com" },
}));

app.post("/logout", (ctx) => Auth.signOut(ctx));
```

| Option | Type | Notes |
| --- | --- | --- |
| `name` | `string?` | Cookie name. Defaults to `sloppy.session`. |
| `secret` | `string \| Config.required(...)` | Session signing secret. Compiler extraction requires `Config.required(...)`. |
| `secure` | `boolean?` | Defaults to `true`. |
| `httpOnly` | `boolean?` | Defaults to `true`. |
| `sameSite` | `"lax" \| "strict" \| "none"?` | Defaults to `lax`. |
| `path` | `string?` | Defaults to `/`. |
| `maxAgeSeconds` | `number?` | Adds session expiry and `Max-Age` when set. |

`Auth.signIn(ctx, claims)` returns `200` with `Set-Cookie`. `Auth.signOut(ctx)`
returns `204` and clears the cookie. Session secrets are not written to Plan
metadata.

### `Auth.apiKey(options)`

Registers an API key provider.

```ts
app.use(Auth.apiKey({
  header: "x-api-key",
  validate: (key) => key === Config.required("Auth:ApiKey"),
}));
```

| Option | Type | Notes |
| --- | --- | --- |
| `header` | `string` | Header to read. Defaults to `x-api-key`. |
| `validate` | `(key, helpers) => boolean \| object \| Promise<boolean \| object>` | Return `true` for the default API-key user or a claims object for a custom user. |
| `configKey` | `string` | Config key for direct API-key comparison when no custom validator is needed. Compiler extraction requires either this or exactly one literal `Config.required(...)` reference in `validate`. |

When the validator is the direct `key === Config.required("...")` shape, Sloppy
treats that key as the config-backed API key and compares it without writing
the value to Plan metadata. Custom validators still run when `configKey` is
present. They receive `helpers.expectedKey` for the resolved config value and
`helpers.constantTimeEquals(left, right)` for custom string comparison.

## Route Authorization

`requireAuth(options?)` is available on route builders:

```ts
app.get("/admin", () => Results.ok({ ok: true }))
  .requireAuth({ role: "admin" });
```

| Option | Type | Behavior |
| --- | --- | --- |
| omitted | | Any authenticated user may call the route. |
| `role` | `string` | User must have the role. |
| `roles` | `string[]` | User must have at least one role. |
| `claim` | `string` | User must have the claim. |
| `policy` | `string` | Named policy must return `true`. |

Groups can require auth for every child route:

```ts
const internal = app.group("/internal").requireAuth();
internal.get("/status", () => Results.ok({ ok: true }));
```

## Policies

Policies are named functions:

```ts
app.auth.addPolicy("admin-or-ops", (user) =>
  user.roles.includes("admin") || user.claims.department === "ops",
);

app.get("/ops", () => Results.ok({ ok: true }))
  .requireAuth({ policy: "admin-or-ops" });
```

## `ctx.user`

Every request context has `ctx.user`.

| Field | Type | Notes |
| --- | --- | --- |
| `authenticated` | `boolean` | `true` after a provider accepts credentials. |
| `sub` | `string` | Subject. Empty for anonymous users. |
| `name` | `string` | Optional display name. |
| `roles` | `string[]` | Role list from claims or provider output. |
| `claims` | `object` | Full claims object. |
| `scheme` | `string` | `jwtBearer`, `apiKey`, `cookieSession`, or empty for anonymous users. |
| `hasRole(role)` | `function` | Role helper. |
| `hasClaim(name, value?)` | `function` | Claim helper. |

## Responses

Protected routes return ProblemDetails-compatible result descriptors:

- `401` for missing or invalid credentials;
- `403` for authenticated users that fail role, claim, or policy checks.

If `ProblemDetails.defaults(...)` is also enabled, handler exceptions use the
normal ProblemDetails behavior. Auth failures already use
`application/problem+json`.

## Plan and OpenAPI Metadata

The compiler emits:

- configured auth schemes without secret values;
- protected route requirements;
- policy names;
- config requirements for `Config.required(...)`;
- OpenAPI `bearerAuth`, `apiKeyAuth`, and `cookieSessionAuth` security schemes;
- per-route OpenAPI security requirements.

Secrets are never written to Plan JSON. Config keys may appear so `sloppy
doctor`, `sloppy audit`, and generated execution can require the value at run
time.
