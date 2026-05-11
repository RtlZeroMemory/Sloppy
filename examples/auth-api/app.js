import { Sloppy, Results, Auth, Config } from "sloppy";

const app = Sloppy.create();

app.use(Auth.jwtBearer({
    issuer: "sloppy.local",
    audience: "api",
    secret: Config.required("Auth:JwtSecret"),
}));

app.get("/public", () => Results.ok({ ok: true }));

app.get("/me", (ctx) => Results.ok({
    subject: ctx.user.sub,
    roles: ctx.user.roles,
})).requireAuth();

app.get("/admin", () => Results.ok({ ok: true }))
    .requireAuth({ role: "admin" });

app.use(Auth.apiKey({
    header: "x-api-key",
    validate: (key) => key === Config.required("Auth:ApiKey"),
}));

app.get("/internal/status", () => Results.ok({ ok: true }))
    .requireAuth();

app.auth.addPolicy("admin-or-ops", (user) =>
    user.roles.includes("admin") || user.claims.department === "ops",
);

app.get("/ops", () => Results.ok({ ok: true }))
    .requireAuth({ policy: "admin-or-ops" });

export default app;
