import assert from "node:assert/strict";
import { createHmac } from "node:crypto";

import { Auth, Base64Url, Config, Results, Sloppy, Testing, Text } from "../../stdlib/sloppy/index.js";

const previousSloppy = globalThis.__sloppy;

globalThis.__sloppy = {
    crypto: {
        hmac(algorithm, key, bytes) {
            assert.equal(algorithm, "sha256");
            return new Uint8Array(createHmac("sha256", Buffer.from(key)).update(Buffer.from(bytes)).digest());
        },
    },
};

function jwt(secret, claims, header = { alg: "HS256", typ: "JWT" }) {
    const encodedHeader = Base64Url.encode(Text.utf8.encode(JSON.stringify(header)));
    const encodedClaims = Base64Url.encode(Text.utf8.encode(JSON.stringify(claims)));
    const signingInput = `${encodedHeader}.${encodedClaims}`;
    const signature = createHmac("sha256", secret).update(signingInput).digest();
    return `${signingInput}.${Base64Url.encode(new Uint8Array(signature))}`;
}

function cookieValue(setCookie) {
    return setCookie.split(";", 1)[0].split("=", 2)[1];
}

function assertCookieDirective(setCookie, directive) {
    assert.match(setCookie, new RegExp(`(?:^|;\\s*)${directive}(?:;|$)`));
}

async function requestJson(host, method, target, options = undefined) {
    const response = await host.request(method, target, options);
    return { response, body: response.text().length === 0 ? null : response.json() };
}

try {
{
    const builder = Sloppy.createBuilder();
    builder.config.addObject({
        Auth: {
            JwtSecret: "test-secret",
        },
    });
    const app = builder.build();
    app.use(Auth.jwtBearer({
        issuer: "sloppy.local",
        audience: "api",
        secret: Config.required("Auth:JwtSecret"),
        clock: () => 1_700_000_000_000,
    }));
    app.get("/public", () => Results.ok({ ok: true }));
    app.get("/me", (ctx) => Results.ok({
        subject: ctx.user.sub,
        roles: ctx.user.roles,
        scheme: ctx.user.scheme,
        authenticated: ctx.user.authenticated,
    })).requireAuth();
    app.get("/admin", () => Results.ok({ ok: true })).requireAuth({ role: "admin" });
    app.auth.addPolicy("ops", (user) => user.claims.department === "ops");
    assert.throws(
        () => app.auth.addPolicy("ops", () => true),
        /already registered/,
    );
    app.get("/ops", () => Results.ok({ ok: true })).requireAuth({ policy: "ops" });

    const host = Testing.createHost(app);
    const valid = jwt("test-secret", {
        iss: "sloppy.local",
        aud: "api",
        sub: "user-1",
        roles: ["admin"],
        department: "ops",
        exp: 1_700_000_600,
    });
    const expired = jwt("test-secret", {
        iss: "sloppy.local",
        aud: "api",
        sub: "user-1",
        exp: 1_699_999_999,
    });
    const barelyExpired = jwt("test-secret", {
        iss: "sloppy.local",
        aud: "api",
        sub: "user-1",
        exp: 1_699_999_995,
    });
    const futureIssued = jwt("test-secret", {
        iss: "sloppy.local",
        aud: "api",
        sub: "user-1",
        iat: 1_700_000_601,
        exp: 1_700_000_700,
    });
    const noAdmin = jwt("test-secret", {
        iss: "sloppy.local",
        aud: "api",
        sub: "user-2",
        roles: ["reader"],
        exp: 1_700_000_600,
    });

    assert.equal((await requestJson(host, "GET", "/public")).response.status, 200);
    assert.equal((await requestJson(host, "GET", "/me")).response.status, 401);
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { Authorization: `Bearer ${expired}` },
    })).response.status, 401);
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { Authorization: `Bearer ${futureIssued}` },
    })).response.status, 401);
    const noneHeader = Base64Url.encode(Text.utf8.encode(JSON.stringify({ alg: "none", typ: "JWT" })));
    const noneClaims = Base64Url.encode(Text.utf8.encode(JSON.stringify({ sub: "user-1" })));
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { Authorization: `Bearer ${noneHeader}.${noneClaims}.` },
    })).response.status, 401);
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { Authorization: `Bearer ${jwt("test-secret", ["not-object"])}` },
    })).response.status, 401);

    const me = await requestJson(host, "GET", "/me", {
        headers: { Authorization: `Bearer ${valid}` },
    });
    assert.equal(me.response.status, 200);
    assert.equal(me.body.subject, "user-1");
    assert.deepEqual(me.body.roles, ["admin"]);
    assert.equal(me.body.scheme, "jwtBearer");
    assert.equal(me.body.authenticated, true);

    assert.equal((await requestJson(host, "GET", "/admin", {
        headers: { Authorization: `Bearer ${noAdmin}` },
    })).response.status, 403);
    assert.equal((await requestJson(host, "GET", "/ops", {
        headers: { Authorization: `Bearer ${valid}` },
    })).response.status, 200);

    await host.close();

    const skewBuilder = Sloppy.createBuilder();
    skewBuilder.config.addObject({
        Auth: {
            JwtSecret: "test-secret",
        },
    });
    const skewApp = skewBuilder.build();
    skewApp.use(Auth.jwtBearer({
        audience: "api",
        secret: Config.required("Auth:JwtSecret"),
        clock: () => 1_700_000_000_000,
        clockSkewSeconds: 10,
    }));
    skewApp.get("/me", (ctx) => Results.ok({ subject: ctx.user.sub })).requireAuth();
    const skewHost = Testing.createHost(skewApp);
    assert.equal((await requestJson(skewHost, "GET", "/me", {
        headers: { Authorization: `Bearer ${barelyExpired}` },
    })).response.status, 200);
    await skewHost.close();
}

{
    const builder = Sloppy.createBuilder();
    builder.config.addObject({
        Auth: {
            SessionSecret: "session-secret",
        },
    });
    const app = builder.build();
    app.use(Auth.cookieSession({
        secret: Config.required("Auth:SessionSecret"),
        clock: () => 1_700_000_000_000,
        maxAgeSeconds: 3600,
    }));
    app.post("/login", (ctx) => Auth.signIn(ctx, {
        sub: "user-1",
        roles: ["user"],
        claims: { email: "ada@example.com" },
    }));
    app.post("/logout", (ctx) => Auth.signOut(ctx));
    app.get("/me", (ctx) => Results.ok({
        subject: ctx.user.sub,
        roles: ctx.user.roles,
        email: ctx.user.claims.email,
        scheme: ctx.user.scheme,
    })).requireAuth();

    const host = Testing.createHost(app);
    assert.equal((await requestJson(host, "GET", "/me")).response.status, 401);

    const login = await requestJson(host, "POST", "/login");
    assert.equal(login.response.status, 200);
    const setCookie = login.response.headers.get("set-cookie");
    assert.match(setCookie, /^sloppy\.session=[^;]+/);
    for (const directive of ["Path=/", "Max-Age=3600", "SameSite=Lax", "HttpOnly", "Secure"]) {
        assertCookieDirective(setCookie, directive);
    }

    const session = cookieValue(setCookie);
    const me = await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${session}` },
    });
    assert.equal(me.response.status, 200);
    assert.equal(me.body.subject, "user-1");
    assert.deepEqual(me.body.roles, ["user"]);
    assert.equal(me.body.email, "ada@example.com");
    assert.equal(me.body.scheme, "cookieSession");

    const tampered = `${session}A`;
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${tampered}` },
    })).response.status, 401);
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { cookie: "sloppy.session=malformed" },
    })).response.status, 401);
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { cookie: "sloppy.session=payload.not-base64url" },
    })).response.status, 401);

    const logout = await requestJson(host, "POST", "/logout", {
        headers: { cookie: `sloppy.session=${session}` },
    });
    assert.equal(logout.response.status, 204);
    const logoutCookie = logout.response.headers.get("set-cookie");
    assert.match(logoutCookie, /^sloppy\.session=/);
    for (const directive of [
        "Path=/",
        "Max-Age=0",
        "Expires=Thu, 01 Jan 1970 00:00:00 GMT",
        "SameSite=Lax",
        "HttpOnly",
        "Secure",
    ]) {
        assertCookieDirective(logoutCookie, directive);
    }

    await host.close();
}

{
    const builder = Sloppy.createBuilder();
    builder.config.addObject({
        Auth: {
            ApiKey: "secret-key",
        },
    });
    const app = builder.build();
    app.use(Auth.apiKey({
        header: "x-api-key",
        validate: (key) => key === Config.required("Auth:ApiKey"),
    }));
    app.get("/internal/status", (ctx) => Results.ok({
        authenticated: ctx.user.authenticated,
        scheme: ctx.user.scheme,
    })).requireAuth();
    const group = app.group("/group").requireAuth();
    group.get("/status", () => Results.ok({ ok: true }));

    const host = Testing.createHost(app);
    assert.equal((await requestJson(host, "GET", "/internal/status")).response.status, 401);
    assert.equal((await requestJson(host, "GET", "/internal/status", {
        headers: { "x-api-key": "wrong" },
    })).response.status, 401);

    const status = await requestJson(host, "GET", "/internal/status", {
        headers: { "x-api-key": "secret-key" },
    });
    assert.equal(status.response.status, 200);
    assert.equal(status.body.authenticated, true);
    assert.equal(status.body.scheme, "apiKey");

    assert.equal((await requestJson(host, "GET", "/group/status", {
        headers: { "x-api-key": "secret-key" },
    })).response.status, 200);

    await host.close();
}

{
    const builder = Sloppy.createBuilder();
    builder.config.addObject({
        Auth: {
            ApiKey: "static-secret",
        },
    });
    const app = builder.build();
    app.use(Auth.apiKey({
        header: "x-static-key",
        configKey: "Auth:ApiKey",
    }));
    app.get("/static-key", (ctx) => Results.ok({ scheme: ctx.user.scheme })).requireAuth();

    const host = Testing.createHost(app);
    assert.equal((await requestJson(host, "GET", "/static-key", {
        headers: { "x-static-key": "wrong" },
    })).response.status, 401);
    const status = await requestJson(host, "GET", "/static-key", {
        headers: { "x-static-key": "static-secret" },
    });
    assert.equal(status.response.status, 200);
    assert.equal(status.body.scheme, "apiKey");

    await host.close();
}

{
    const builder = Sloppy.createBuilder();
    builder.config.addObject({
        Auth: {
            ApiKey: "stored-secret",
        },
    });
    const app = builder.build();
    app.use(Auth.apiKey({
        header: "x-custom-key",
        configKey: "Auth:ApiKey",
        validate: (key, helpers) =>
            helpers.constantTimeEquals(key, helpers.expectedKey) && key.endsWith("-allowed"),
    }));
    app.get("/custom-key", () => Results.ok({ ok: true })).requireAuth();

    const host = Testing.createHost(app);
    assert.equal((await requestJson(host, "GET", "/custom-key", {
        headers: { "x-custom-key": "stored-secret" },
    })).response.status, 401);

    await host.close();
}

{
    const builder = Sloppy.createBuilder();
    builder.config.addObject({
        Auth: {
            ApiKey: "stored-allowed",
        },
    });
    const app = builder.build();
    app.use(Auth.apiKey({
        header: "x-custom-key",
        configKey: "Auth:ApiKey",
        validate: (key, helpers) =>
            helpers.constantTimeEquals(key, helpers.expectedKey) && key.endsWith("-allowed"),
    }));
    app.get("/custom-key", () => Results.ok({ ok: true })).requireAuth();

    const host = Testing.createHost(app);
    assert.equal((await requestJson(host, "GET", "/custom-key", {
        headers: { "x-custom-key": "stored-secret" },
    })).response.status, 401);
    assert.equal((await requestJson(host, "GET", "/custom-key", {
        headers: { "x-custom-key": "stored-allowed" },
    })).response.status, 200);

    await host.close();
}

{
    assert.throws(
        () => Sloppy.create().use(Auth.jwtBearer({ secret: Config.required("Auth:Missing") })),
        /required but was not provided/,
    );
}
} finally {
    if (previousSloppy === undefined) {
        delete globalThis.__sloppy;
    } else {
        globalThis.__sloppy = previousSloppy;
    }
}
