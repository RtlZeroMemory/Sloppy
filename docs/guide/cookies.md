# Cookies

Use `ctx.cookies` for request cookies and `.cookie(...)` on a result descriptor
for response cookies.

## Read a Cookie

```ts
app.get("/me", (ctx) => {
    const session = ctx.cookies.get("session");
    if (session === null) {
        return Results.unauthorized({ error: "sign in required" });
    }

    return Results.ok({ session });
});
```

`cookies.get(name)` returns the decoded cookie value or `null`. If a request
contains the same cookie more than once, lookup returns the last value.

## Set a Cookie

```ts
app.post("/login", (ctx) => {
    const form = ctx.request.form();
    const session = signIn(form.get("email"));

    return Results.ok({ ok: true }).cookie("session", session.id, {
        httpOnly: true,
        secure: true,
        sameSite: "Strict",
        path: "/",
        maxAge: 3600,
    });
});
```

Each `.cookie(...)` call appends a separate `Set-Cookie` header. Cookie names
must be valid HTTP token names. Values and attributes are validated before the
descriptor reaches the native response writer.

## Signed Auth Sessions

For Sloppy auth sessions, prefer `Auth.cookieSession(...)` instead of hand
rolling cookie signing:

```ts
app.use(Auth.cookieSession({
    secret: Config.required("Auth:SessionSecret"),
}));
```

`Auth.signIn(ctx, claims)` sets a signed session cookie. `Auth.signOut(ctx)`
clears it. The default auth-session cookie flags are `Secure`, `HttpOnly`,
`SameSite=Lax`, and `Path=/`.

General-purpose cookie signing, encryption, rotation, and durable session
storage are still application logic. Sloppy's built-in auth session stores the
signed claims payload in the cookie.
