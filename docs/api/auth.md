# Auth

Sloppy auth is an experimental app-host and compiler-visible API for protecting
routes with JWT bearer tokens, API keys, signed session cookies, scopes, roles,
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

### `Auth.configure(options)`

Registers multiple named schemes at once and sets the default scheme name used
by route metadata.

```ts
app.use(Auth.configure({
  defaultScheme: "jwt",
  schemes: {
    jwt: Auth.jwtBearer({ secret: Config.required("Auth:JwtSecret") }),
    apiKey: Auth.apiKey({ configKey: "Auth:ApiKey" }),
    session: Auth.cookieSession({
      secret: Config.required("Auth:SessionSecret"),
      csrf: true,
    }),
  },
}));
```

Routes can require the configured scheme name:

```ts
app.get("/me", handler).requiresAuth("jwt");
```

The `defaultScheme` controls bare `.requiresAuth()` checks. A route without an
explicit scheme accepts only the configured default scheme. A route with
`.requiresAuth("apiKey")` accepts the named `apiKey` scheme even when another
scheme is the default.

### `Auth.jwtBearer(options)`

Registers a JWT bearer provider for `Authorization: Bearer <token>`.

| Option | Type | Notes |
| --- | --- | --- |
| `issuer` | `string?` | Expected `iss` claim. |
| `audience` | `string?` | Expected `aud` claim. Array audiences are accepted when one entry matches. |
| `secret` | `string \| Config.required(...)` | HS256 signing secret. Use config references for app secrets. Compiler extraction requires `Config.required(...)`. |
| `algorithms` | `string \| string[]?` | Algorithm allowlist. Defaults to `HS256`. `RS256` is supported only with static JWK keys and WebCrypto availability. |
| `keys` | `object[]?` | Static key list for `kid` lookup. HS256 entries use `secret`; RS256 entries use a public JWK. |
| `jwks` | `{ keys: object[] }?` | Local JWKS-shaped static key set. Equivalent to `keys` for token validation. |
| `clockSkewSeconds` | `number?` | Integer leeway for `exp`, `nbf`, and future `iat` checks. Defaults to `0`. |

JWT validation rejects malformed compact tokens, `alg: "none"`, algorithms not
in the allowlist, invalid signatures, expired tokens, future `nbf`, future
`iat`, wrong issuer, and wrong audience. Static `kid` lookup is supported from
`keys` / local JWKS-shaped `jwks.keys`. Remote JWKS options such as `jwksUri`
fail closed with a configuration error; configure static keys for
compiler-visible apps.

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
| `sameSite` | `"lax" \| "strict" \| "none"?` | Defaults to `lax`. `none` requires `secure: true`. |
| `path` | `string?` | Defaults to `/`. |
| `maxAgeSeconds` | `number?` | Sets signed-cookie session expiry and `Max-Age`. Defaults to 24 hours for signed-cookie sessions. Store-backed sessions use absolute/idle timeouts instead unless set. |
| `csrf` | `boolean \| object?` | Enables double-submit CSRF for unsafe methods. The compiler extracts `csrf: true` and `csrf: false`; custom object options such as `{ header, cookieName }` are runtime-only and currently emitted as partial metadata. `__Host-` CSRF cookies require `secure: true` and `path: "/"` at runtime. |

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
| `keys` | `object[]?` | Static test/dev key entries. Prefer `hash: "sha256:<hex>"` over plaintext `key`. |
| `authorizationScheme` | `string?` | Optional `Authorization: <scheme> <key>` reader. Query-string keys are not read. |

When the validator is the direct `key === Config.required("...")` shape, Sloppy
treats that key as the config-backed API key and compares it without writing
the value to Plan metadata. Custom validators still run when `configKey` is
present. They receive `helpers.expectedKey` for the resolved config value and
`helpers.constantTimeEquals(left, right)` for custom string comparison.

## Route Authorization

`requireAuth(options?)` / `requiresAuth(options?)` are available on route
builders:

```ts
app.get("/admin", () => Results.ok({ ok: true }))
  .requireAuth({ role: "admin" });
```

| Option | Type | Behavior |
| --- | --- | --- |
| omitted | | Any authenticated user may call the route. |
| `"scheme"` | `string` | User must authenticate with the named scheme. |
| `scheme` / `schemes` | `string \| string[]` | User must authenticate with one of the listed schemes. |
| `scope` / `scopes` | `string \| string[]` | User must have every listed scope. |
| `role` | `string` | User must have the role. |
| `roles` | `string[]` | User must have at least one role. |
| `claim` | `string` | User must have the claim. |
| `policy` | `string` | Named policy must return `true`. |

Builder aliases compose with `requiresAuth()`:

```ts
app.get("/jobs", handler)
  .requiresAuth("apiKey")
  .requiresScope("jobs:write")
  .requiresRole("worker")
  .authorize("InternalJobs");

app.get("/status", handler).allowAnonymous();
```

Auth-aware rate limits compose with the same route builder:

```ts
app.get("/me", handler)
  .requiresAuth()
  .rateLimit(RateLimit.tokenBucket({
    capacity: 100,
    refillPerSecond: 10,
    partitionBy: RateLimit.partition.user(),
  }));
```

`RateLimit.partition.user()` and `RateLimit.partition.claim(name)` require an
authenticated request. `RateLimit.partition.apiKey()` uses the authenticated
API-key principal when present. Raw tokens, API keys, user IDs, and IPs are not
emitted as metric labels or diagnostic fields.

Groups can require auth for every child route:

```ts
const internal = app.group("/internal").requireAuth();
internal.get("/status", () => Results.ok({ ok: true }));
```

Route builders also expose `.requiresScope(scope)`, which adds a scope
requirement while keeping the route protected:

```ts
app.websocket("/secure/ws", async (socket) => {
  await socket.accept();
  await socket.sendJson({ sub: socket.ctx.user.sub });
})
  .requiresAuth()
  .requiresScope("realtime");
```

WebSocket routes use the same auth ordering as HTTP routes. Missing credentials
reject the handshake with `401`; authenticated users without the required role,
scope, claim, or policy reject with `403`.

## Policies

Policies can be named functions or builder descriptors:

```ts
app.auth.addPolicy("admin-or-ops", (user) =>
  user.roles.includes("admin") || user.claims.department === "ops",
);

app.get("/ops", () => Results.ok({ ok: true }))
  .requireAuth({ policy: "admin-or-ops" });
```

```ts
app.auth.addPolicy("Users.Read", Auth.policy((policy) =>
  policy
    .requireAuthenticated()
    .requireScope("users:read")
    .requireRole("admin")
    .requireClaim("tenant", "alpha"),
));
```

Handlers can run resource checks:

```ts
const allowed = await ctx.authorize("Users.Read", userRecord);
```

## `ctx.user`

Every request context has `ctx.user`.

| Field | Type | Notes |
| --- | --- | --- |
| `authenticated` | `boolean` | `true` after a provider accepts credentials. |
| `sub` | `string` | Subject. Empty for anonymous users. |
| `name` | `string` | Optional display name. |
| `roles` | `string[]` | Role list from claims or provider output. |
| `scopes` | `string[]` | Scope list from `scope`, `scp`, or `scopes` claims. |
| `claims` | `object` | Full claims object. |
| `scheme` | `string` | Compatibility-facing provider principal scheme such as `jwtBearer`, `apiKey`, `cookieSession`, or a configured scheme name. |
| `authScheme` | `string` | Configured scheme name used by route requirements. Matches `scheme` for directly installed providers. |
| `hasRole(role)` | `function` | Role helper. |
| `hasScope(scope)` | `function` | Scope helper. |
| `hasClaim(name, value?)` | `function` | Claim helper. |

The request context also exposes `ctx.auth`, `ctx.claims`, `ctx.requireUser()`,
`ctx.hasScope(...)`, `ctx.hasRole(...)`, `ctx.hasClaim(...)`, and
`ctx.authorize(policy, resource?)`.

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
- route schemes and scopes;
- policy names;
- config requirements for `Config.required(...)`;
- OpenAPI `bearerAuth`, `apiKeyAuth`, and `cookieSessionAuth` security schemes;
- per-route OpenAPI security requirements.

Secrets are never written to Plan JSON. Config keys may appear so `sloppy
doctor`, `sloppy audit`, and generated execution can require the value at run
time.
