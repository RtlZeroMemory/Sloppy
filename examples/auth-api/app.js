import { Sloppy, Results, Auth, Config } from "sloppy";

const app = Sloppy.create();

app.use(Auth.jwtBearer({
    issuer: "sloppy.local",
    audience: "api",
    secret: Config.required("Auth:JwtSecret"),
}));

app.use(Auth.cookieSession({
    name: "sloppy.session",
    secret: Config.required("Auth:SessionSecret"),
    store: Auth.sessionStore.memory({ maxEntries: 128 }),
    idleTimeoutMs: 1800000,
    absoluteTimeoutMs: 86400000,
    rotation: true,
    csrf: true,
}));

app.get("/public", () => Results.ok({ ok: true }));

app.post("/login", (ctx) => Auth.signIn(ctx, {
    sub: "user-1",
    roles: ["user"],
    claims: { email: "ada@example.com" },
}));

app.post("/logout", (ctx) => Auth.signOut(ctx));

app.get("/me", (ctx) => Results.ok({
    subject: ctx.user.sub,
    roles: ctx.user.roles,
    scheme: ctx.user.scheme,
})).requireAuth();

app.get("/admin", () => Results.ok({ ok: true }))
    .requireAuth({ role: "admin" });

app.use(Auth.apiKey({
    header: "x-api-key",
    validate: (key) => key === Config.required("Auth:ApiKey"),
}));

app.get("/internal/status", () => Results.ok({ ok: true }))
    .requireAuth("apiKeyAuth");

app.auth.addPolicy("admin-or-ops", (user) =>
    user.roles.includes("admin") || user.claims.department === "ops",
);
app.auth.addPolicy("users-read", Auth.policy((policy) =>
    policy.requireAuthenticated().requireScope("users:read"),
));

app.get("/ops", () => Results.ok({ ok: true }))
    .requireAuth({ policy: "admin-or-ops" });

app.get("/users", () => Results.ok({ ok: true }))
    .requireAuth({ policy: "users-read" });

export default app;
