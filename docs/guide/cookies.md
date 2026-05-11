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

Cookie signing, encryption, rotation, and session storage are application
logic. Sloppy only moves cookie values through the request/response surface.
