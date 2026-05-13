# Sessions And CSRF

`Auth.cookieSession(...)` provides signed cookie sessions for the app host.
Without a store, the cookie contains HMAC-signed claims and expiry metadata. With
a store, the cookie contains a signed opaque session ID and the store holds the
claims and lifecycle state.

```ts
app.use(Auth.cookieSession({
  name: "sloppy.session",
  secret: Config.required("Auth:SessionSecret"),
  maxAgeSeconds: 3600,
  csrf: true,
}));

app.post("/login", (ctx) => Auth.signIn(ctx, {
  sub: "user-1",
  roles: ["user"],
}));
```

Store-backed sessions:

```ts
app.use(Auth.cookieSession({
  name: "__Host-sloppy_session",
  secret: Config.required("Auth:SessionSecret"),
  store: Auth.sessionStore.memory({ maxEntries: 4096 }),
  idleTimeoutMs: 30 * 60_000,
  absoluteTimeoutMs: 24 * 60 * 60_000,
  csrf: true,
}));
```

`Auth.sessionStore.memory(...)` is process-local and non-durable. Use it for
local development and tests; inspection tooling warns on it, and production apps
should use a durable provider-backed store.

For durable sessions, pass an opened Sloppy data connection:

```ts
app.use(Auth.cookieSession({
  secret: Config.required("Auth:SessionSecret"),
  store: Auth.sessionStore.dataProvider({ db }),
  idleTimeoutMs: 30 * 60_000,
  absoluteTimeoutMs: 24 * 60 * 60_000,
}));
```

The data-provider store creates and uses `sloppy_auth_sessions` with JSON claim
storage, `created_at_ms`, `last_seen_at_ms`, `expires_at_ms`,
`idle_expires_at_ms`, `revoked_at_ms`, CSRF, and metadata columns. Claims are
stored as supplied; do not put passwords, tokens, connection strings, or other
secrets in session claims unless your application adds its own encryption.

Defaults:

- `HttpOnly`
- `Secure`
- `SameSite=Lax`
- `Path=/`

When `csrf` is enabled, `Auth.signIn` also sets a CSRF cookie. Unsafe methods
must send both the CSRF cookie and the matching `X-CSRF-Token` header. Safe
methods (`GET`, `HEAD`, `OPTIONS`, `TRACE`) do not require the token.
Store-backed session rotation preserves the original absolute expiry, slides
only idle expiry, caps the rotated cookie lifetime to the remaining absolute
lifetime, and rotates the CSRF token when CSRF protection is enabled.

`Auth.signOut(ctx)` clears the session cookie. Session and CSRF values are not
written into Plan metadata or diagnostics.

`ctx.session.id` is present for store-backed sessions. `ctx.session.revoke()`
revokes the active stored session. `Auth.signOut(ctx)` also revokes the active
stored session before clearing the cookie.
