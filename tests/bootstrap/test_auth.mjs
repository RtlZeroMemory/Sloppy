import assert from "node:assert/strict";
import { createHash, createHmac, createSign, generateKeyPairSync } from "node:crypto";

import { Auth, Base64Url, Config, Results, Sloppy, Testing, Text } from "../../stdlib/sloppy/index.js";

const previousSloppy = globalThis.__sloppy;
let randomTokenCounter = 0;

globalThis.__sloppy = {
    crypto: {
        hash(algorithm, bytes) {
            assert.equal(algorithm, "sha256");
            return new Uint8Array(createHash("sha256").update(Buffer.from(bytes)).digest());
        },
        hmac(algorithm, key, bytes) {
            assert.equal(algorithm, "sha256");
            return new Uint8Array(createHmac("sha256", Buffer.from(key)).update(Buffer.from(bytes)).digest());
        },
        randomToken(length) {
            randomTokenCounter += 1;
            return `session-${length}-token-${randomTokenCounter}`;
        },
        passwordHash(bytes, opsLimit, memoryLimitBytes) {
            assert.deepEqual(Buffer.from(bytes), Buffer.from("password"));
            assert.equal(opsLimit, 2);
            assert.equal(memoryLimitBytes, 67108864);
            return Promise.resolve("$argon2id$v=19$m=65536,t=2,p=1$test$hash");
        },
        passwordVerify(bytes, encodedHash) {
            assert.deepEqual(Buffer.from(bytes), Buffer.from("password"));
            assert.equal(encodedHash.startsWith("$argon2id$"), true);
            return Promise.resolve(true);
        },
        passwordNeedsRehash(encodedHash, opsLimit, memoryLimitBytes) {
            assert.equal(encodedHash.startsWith("$argon2id$"), true);
            assert.equal(opsLimit, 3);
            assert.equal(memoryLimitBytes, 67108864);
            return Promise.resolve(true);
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

function jwtRs256(privateKey, claims, header = { alg: "RS256", typ: "JWT" }) {
    const encodedHeader = Base64Url.encode(Text.utf8.encode(JSON.stringify(header)));
    const encodedClaims = Base64Url.encode(Text.utf8.encode(JSON.stringify(claims)));
    const signingInput = `${encodedHeader}.${encodedClaims}`;
    const signature = createSign("RSA-SHA256").update(signingInput).sign(privateKey);
    return `${signingInput}.${Base64Url.encode(new Uint8Array(signature))}`;
}

function cookieValue(setCookie) {
    return setCookie.split(";", 1)[0].split("=", 2)[1];
}

function setCookies(response) {
    return [...response.headers.entries()]
        .filter(([name]) => name === "set-cookie")
        .map(([, value]) => value);
}

function namedCookieValue(cookies, name) {
    const cookie = cookies.find((value) => value.startsWith(`${name}=`));
    assert.ok(cookie, `missing ${name} cookie`);
    return cookieValue(cookie);
}

function namedSetCookie(cookies, name) {
    const cookie = cookies.find((value) => value.startsWith(`${name}=`));
    assert.ok(cookie, `missing ${name} set-cookie`);
    return cookie;
}

function cookieMaxAgeSeconds(setCookie) {
    const match = /(?:^|;\s*)Max-Age=(\d+)(?:;|$)/u.exec(setCookie);
    assert.ok(match, "missing Max-Age directive");
    return Number(match[1]);
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
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { Authorization: `bearer ${valid}` },
    })).response.status, 200);

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
    assert.ok(globalThis.crypto?.subtle, "RS256 auth test requires WebCrypto subtle support");
    const { privateKey, publicKey } = generateKeyPairSync("rsa", { modulusLength: 2048 });
    const publicJwk = publicKey.export({ format: "jwk" });
    const app = Sloppy.create();
    app.use(Auth.jwtBearer({
        name: "jwt",
        algorithms: ["RS256", "HS256"],
        secret: "hmac-confusion-secret",
        keys: [
            { ...publicJwk, kid: "rsa-key", alg: "RS256" },
        ],
        clock: () => 1_700_000_000_000,
    }));
    app.get("/rsa", (ctx) => Results.ok({ subject: ctx.user.sub, scheme: ctx.user.scheme })).requiresAuth("jwt");

    const host = Testing.createHost(app);
    const valid = jwtRs256(privateKey, {
        sub: "rsa-user",
        exp: 1_700_000_600,
    }, { alg: "RS256", typ: "JWT", kid: "rsa-key" });
    const ok = await requestJson(host, "GET", "/rsa", {
        headers: { Authorization: `Bearer ${valid}` },
    });
    assert.equal(ok.response.status, 200);
    assert.equal(ok.body.subject, "rsa-user");
    assert.equal(ok.body.scheme, "jwt");

    const confusion = jwt("hmac-confusion-secret", {
        sub: "confused",
        exp: 1_700_000_600,
    }, { alg: "HS256", typ: "JWT", kid: "rsa-key" });
    assert.equal((await requestJson(host, "GET", "/rsa", {
        headers: { Authorization: `Bearer ${confusion}` },
    })).response.status, 401);
    await host.close();
}

{
    assert.throws(
        () => Auth.jwtBearer({ jwksUri: "https://issuer.example/.well-known/jwks.json" }),
        /remote JWKS discovery requires runtime HTTP client integration/,
    );
}

{
    const signed = Auth.cookieSession({ secret: "signed-session-secret" });
    assert.equal(signed.maxAgeSeconds, 86400);
    const stored = Auth.cookieSession({
        secret: "stored-session-secret",
        store: Auth.sessionStore.memory(),
    });
    assert.equal(stored.maxAgeSeconds, undefined);
    assert.throws(
        () => Auth.cookieSession({ secret: "signed-session-secret", sameSite: "none", secure: false }),
        /sameSite none requires secure cookies/,
    );
    assert.throws(
        () => Auth.cookieSession({ secret: "signed-session-secret", csrf: { cookieName: "bad;name" } }),
        /CSRF cookie name must be a safe HTTP token/,
    );
    assert.throws(
        () => Auth.cookieSession({ secret: "signed-session-secret", csrf: true, secure: false }),
        /__Host- CSRF cookies require secure true and path/,
    );
    assert.throws(
        () => Auth.cookieSession({ secret: "signed-session-secret", csrf: true, path: "/app" }),
        /__Host- CSRF cookies require secure true and path/,
    );
}

{
    const builder = Sloppy.createBuilder();
    builder.config.addObject({
        Auth: {
            SessionSecret: "session-secret",
        },
    });
    const app = builder.build();
    let sessionClock = 1_700_000_000_000;
    app.use(Auth.cookieSession({
        secret: Config.required("Auth:SessionSecret"),
        clock: () => sessionClock,
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
    sessionClock = 1_700_003_601_000;
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${session}` },
    })).response.status, 401);
    sessionClock = 1_700_000_000_000;

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
    let sessionClock = 1_700_000_000_000;
    const app = Sloppy.create();
    app.use(Auth.cookieSession({
        secret: "signed-override-secret",
        clock: () => sessionClock,
        maxAgeSeconds: 3600,
    }));
    app.post("/login", (ctx) => Auth.signIn(ctx, { sub: "short-signed" }, { maxAgeSeconds: 1 }));
    app.get("/me", (ctx) => Results.ok({ subject: ctx.user.sub })).requireAuth();

    const host = Testing.createHost(app);
    const login = await host.post("/login");
    const setCookie = login.headers.get("set-cookie");
    assert.equal(cookieMaxAgeSeconds(setCookie), 1);
    const session = cookieValue(setCookie);
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${session}` },
    })).response.status, 200);
    sessionClock += 1_001;
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${session}` },
    })).response.status, 401);
    await host.close();
}

{
    let sessionClock = 1_700_000_000_000;
    const store = Auth.sessionStore.memory({ maxEntries: 8 });
    const app = Sloppy.create();
    app.use(Auth.cookieSession({
        secret: "store-session-secret",
        store,
        idleTimeoutMs: 1_000,
        absoluteTimeoutMs: 5_000,
        csrf: true,
        clock: () => sessionClock,
    }));
    app.post("/login", (ctx) => Auth.signIn(ctx, { sub: "stored-user" }));
    app.get("/me", (ctx) => Results.ok({ id: ctx.session.id, subject: ctx.user.sub })).requireAuth();
    app.post("/unsafe", () => Results.ok({ ok: true })).requireAuth();
    app.post("/logout", (ctx) => Auth.signOut(ctx));

    const host = Testing.createHost(app);
    const login = await host.post("/login");
    const cookies = setCookies(login);
    const session = namedCookieValue(cookies, "sloppy.session");
    const csrf = namedCookieValue(cookies, "__Host-sloppy_csrf");
    const me = await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${session}` },
    });
    assert.equal(me.response.status, 200);
    assert.equal(me.body.subject, "stored-user");
    assert.match(me.body.id, /^session-32-token-\d+$/u);
    assert.equal((await requestJson(host, "POST", "/unsafe", {
        headers: {
            cookie: `sloppy.session=${session}; __Host-sloppy_csrf=${csrf}`,
            "x-csrf-token": csrf,
        },
    })).response.status, 200);

    sessionClock = 1_700_000_001_001;
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${session}` },
    })).response.status, 401);
    await host.close();
}

{
    let sessionClock = 1_700_000_000_000;
    const app = Sloppy.create();
    app.use(Auth.cookieSession({
        secret: "store-override-secret",
        store: Auth.sessionStore.memory({ maxEntries: 8 }),
        absoluteTimeoutMs: 5_000,
        clock: () => sessionClock,
    }));
    app.post("/login", (ctx) => Auth.signIn(ctx, { sub: "short-store" }, { maxAgeSeconds: 1 }));
    app.get("/me", (ctx) => Results.ok({ subject: ctx.user.sub })).requireAuth();

    const host = Testing.createHost(app);
    const login = await host.post("/login");
    const setCookie = login.headers.get("set-cookie");
    assert.equal(cookieMaxAgeSeconds(setCookie), 1);
    const session = cookieValue(setCookie);
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${session}` },
    })).response.status, 200);
    sessionClock += 1_001;
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${session}` },
    })).response.status, 401);
    await host.close();
}

{
    const app = Sloppy.create();
    app.use(Auth.cookieSession({
        secret: "logout-session-secret",
        store: Auth.sessionStore.memory(),
        clock: () => 1_700_000_000_000,
    }));
    app.post("/login", (ctx) => Auth.signIn(ctx, { sub: "logout-user" }));
    app.post("/logout", (ctx) => Auth.signOut(ctx));
    app.get("/me", (ctx) => Results.ok({ subject: ctx.user.sub })).requireAuth();

    const host = Testing.createHost(app);
    const login = await host.post("/login");
    const session = cookieValue(login.headers.get("set-cookie"));
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${session}` },
    })).response.status, 200);
    assert.equal((await requestJson(host, "POST", "/logout", {
        headers: { cookie: `sloppy.session=${session}` },
    })).response.status, 204);
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${session}` },
    })).response.status, 401);
    await host.close();
}

{
    const store = Auth.sessionStore.memory();
    const first = Sloppy.create();
    first.use(Auth.cookieSession({
        secret: "isolated-memory-secret",
        store,
    }));
    first.post("/login", (ctx) => Auth.signIn(ctx, { sub: "first-user" }));
    const second = Sloppy.create();
    second.use(Auth.cookieSession({
        secret: "isolated-memory-secret",
        store,
    }));
    second.get("/me", (ctx) => Results.ok({ subject: ctx.user.sub })).requireAuth();

    const firstHost = Testing.createHost(first);
    const secondHost = Testing.createHost(second);
    const login = await firstHost.post("/login");
    const session = cookieValue(login.headers.get("set-cookie"));
    assert.equal((await requestJson(secondHost, "GET", "/me", {
        headers: { cookie: `sloppy.session=${session}` },
    })).response.status, 401);
    await firstHost.close();
    await secondHost.close();
}

{
    const app = Sloppy.create();
    app.use(Auth.cookieSession({
        secret: "rotate-session-secret",
        store: Auth.sessionStore.memory(),
        rotation: true,
        clock: () => 1_700_000_000_000,
    }));
    app.post("/login", (ctx) => Auth.signIn(ctx, { sub: "rotate-user" }));
    app.get("/me", (ctx) => Results.ok({ id: ctx.session.id, subject: ctx.user.sub })).requireAuth();

    const host = Testing.createHost(app);
    const login = await host.post("/login");
    const first = cookieValue(login.headers.get("set-cookie"));
    const firstMe = await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${first}` },
    });
    assert.equal(firstMe.response.status, 200);
    const rotated = cookieValue(firstMe.response.headers.get("set-cookie"));
    assert.notEqual(rotated, first);
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${first}` },
    })).response.status, 401);
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${rotated}` },
    })).response.status, 200);
    await host.close();
}

{
    const startMs = 1_700_000_000_000;
    let sessionClock = startMs;
    const app = Sloppy.create();
    app.use(Auth.cookieSession({
        secret: "rotate-hard-expiry-secret",
        store: Auth.sessionStore.memory(),
        rotation: true,
        csrf: true,
        idleTimeoutMs: 10 * 60_000,
        absoluteTimeoutMs: 10 * 60_000,
        clock: () => sessionClock,
    }));
    app.post("/login", (ctx) => Auth.signIn(ctx, { sub: "hard-expiry-user" }));
    app.get("/me", (ctx) => Results.ok({
        id: ctx.session.id,
        subject: ctx.user.sub,
        issuedAt: ctx.session.issuedAt,
        expiresAt: ctx.session.expiresAt,
    })).requireAuth();
    app.post("/unsafe", () => Results.ok({ ok: true })).requireAuth();

    const host = Testing.createHost(app);
    const login = await host.post("/login");
    const loginCookies = setCookies(login);
    const firstSession = namedCookieValue(loginCookies, "sloppy.session");
    const firstCsrf = namedCookieValue(loginCookies, "__Host-sloppy_csrf");

    sessionClock = startMs + 9 * 60_000;
    const rotatedMe = await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${firstSession}` },
    });
    assert.equal(rotatedMe.response.status, 200);
    assert.equal(rotatedMe.body.issuedAt, Math.floor(startMs / 1000));
    assert.equal(rotatedMe.body.expiresAt, Math.floor((startMs + 10 * 60_000) / 1000));
    const rotatedCookies = setCookies(rotatedMe.response);
    const rotatedSessionCookie = namedSetCookie(rotatedCookies, "sloppy.session");
    const rotatedCsrfCookie = namedSetCookie(rotatedCookies, "__Host-sloppy_csrf");
    const rotatedSession = cookieValue(rotatedSessionCookie);
    const rotatedCsrf = cookieValue(rotatedCsrfCookie);
    assert.notEqual(rotatedSession, firstSession);
    assert.notEqual(rotatedCsrf, firstCsrf);
    assert.ok(cookieMaxAgeSeconds(rotatedSessionCookie) <= 60);
    assert.ok(cookieMaxAgeSeconds(rotatedCsrfCookie) <= 60);

    assert.equal((await requestJson(host, "POST", "/unsafe", {
        headers: {
            cookie: `sloppy.session=${rotatedSession}; __Host-sloppy_csrf=${firstCsrf}`,
            "x-csrf-token": firstCsrf,
        },
    })).response.status, 403);
    const csrfSuccess = await requestJson(host, "POST", "/unsafe", {
        headers: {
            cookie: `sloppy.session=${rotatedSession}; __Host-sloppy_csrf=${rotatedCsrf}`,
            "x-csrf-token": rotatedCsrf,
        },
    });
    assert.equal(csrfSuccess.response.status, 200);
    const secondRotatedSession = namedCookieValue(setCookies(csrfSuccess.response), "sloppy.session");

    sessionClock = startMs + 10 * 60_000 + 1;
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${secondRotatedSession}` },
    })).response.status, 401);
    await host.close();
}

{
    let createSql;
    const db = {
        __debug() {
            return { kind: "sqlserver-connection" };
        },
        async exec(sql) {
            if (createSql === undefined) {
                createSql = sql;
            }
        },
        async queryOne() {
            return undefined;
        },
    };
    const app = Sloppy.create();
    app.use(Auth.cookieSession({
        secret: "sqlserver-session-secret",
        store: Auth.sessionStore.dataProvider({ db }),
        clock: () => 1_700_000_000_000,
    }));
    app.post("/login", (ctx) => Auth.signIn(ctx, { sub: "sqlserver-user" }));

    const host = Testing.createHost(app);
    assert.equal((await requestJson(host, "POST", "/login")).response.status, 200);
    assert.match(createSql, /^IF OBJECT_ID\(N'dbo\.sloppy_auth_sessions', N'U'\) IS NULL/u);
    await host.close();
}

{
    let row;
    const db = {
        __debug() {
            return { kind: "sqlite-connection" };
        },
        async exec(sql, params) {
            if (sql.startsWith("INSERT INTO sloppy_auth_sessions")) {
                row = {
                    id: params[0],
                    subject: params[1],
                    claims_json: params[2],
                    created_at_ms: params[3],
                    last_seen_at_ms: params[4],
                    expires_at_ms: params[5],
                    idle_expires_at_ms: params[6],
                    revoked_at_ms: params[7],
                    csrf: params[8],
                    metadata_json: params[9],
                };
            } else if (sql.startsWith("UPDATE sloppy_auth_sessions SET last_seen_at_ms")) {
                row.last_seen_at_ms = params[0];
                row.idle_expires_at_ms = params[1];
            } else if (sql.startsWith("UPDATE sloppy_auth_sessions SET revoked_at_ms")) {
                row.revoked_at_ms = params[0];
            } else if (sql.startsWith("DELETE FROM sloppy_auth_sessions")) {
                if (row.revoked_at_ms !== null || row.expires_at_ms <= params[0] || row.idle_expires_at_ms <= params[0]) {
                    row = undefined;
                }
            }
        },
        async queryOne() {
            return row;
        },
    };
    const app = Sloppy.create();
    app.use(Auth.cookieSession({
        secret: "db-session-secret",
        store: Auth.sessionStore.dataProvider({ db }),
        idleTimeoutMs: 10_000,
        absoluteTimeoutMs: 60_000,
        clock: () => 1_700_000_000_000,
    }));
    app.post("/login", (ctx) => Auth.signIn(ctx, { sub: "db-user" }));
    app.get("/me", (ctx) => Results.ok({ id: ctx.session.id, subject: ctx.user.sub })).requireAuth();
    app.post("/logout", (ctx) => Auth.signOut(ctx));

    const host = Testing.createHost(app);
    const login = await host.post("/login");
    const session = cookieValue(login.headers.get("set-cookie"));
    const me = await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${session}` },
    });
    assert.equal(me.response.status, 200);
    assert.equal(me.body.subject, "db-user");
    assert.equal(row.last_seen_at_ms, 1_700_000_000_000);
    const validRow = { ...row };
    const invalidRows = [
        ["malformed claims JSON", (candidate) => { candidate.claims_json = "{"; }],
        ["null claims JSON", (candidate) => { candidate.claims_json = "null"; }],
        ["string claims JSON", (candidate) => { candidate.claims_json = "\"bad\""; }],
        ["invalid created_at_ms", (candidate) => { candidate.created_at_ms = "bad"; }],
        ["invalid expires_at_ms", (candidate) => { candidate.expires_at_ms = "bad"; }],
        ["invalid metadata_json shape", (candidate) => { candidate.metadata_json = "[]"; }],
        ["malformed metadata JSON", (candidate) => { candidate.metadata_json = "{"; }],
    ];
    for (const [name, mutate] of invalidRows) {
        row = { ...validRow };
        mutate(row);
        assert.equal((await requestJson(host, "GET", "/me", {
            headers: { cookie: `sloppy.session=${session}` },
        })).response.status, 401, name);
    }
    row = validRow;
    assert.equal((await requestJson(host, "POST", "/logout", {
        headers: { cookie: `sloppy.session=${session}` },
    })).response.status, 204);
    assert.notEqual(row.revoked_at_ms, null);
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${session}` },
    })).response.status, 401);
    await host.close();
}

{
    const app = Sloppy.create();
    app.use(Auth.apiKey({
        authorizationScheme: "SloppyKey",
        keys: [
            { id: "service-client", key: "authorization-secret", scopes: ["jobs:read"] },
        ],
    }));
    app.get("/authorization-key", (ctx) => Results.ok({
        subject: ctx.user.sub,
        scope: ctx.user.hasScope("jobs:read"),
    })).requireAuth();

    const host = Testing.createHost(app);
    assert.equal((await requestJson(host, "GET", "/authorization-key", {
        headers: { Authorization: "Bearer authorization-secret" },
    })).response.status, 401);
    assert.equal((await requestJson(host, "GET", "/authorization-key", {
        headers: { Authorization: "SloppyKey authorization-secret" },
    })).response.status, 200);
    assert.equal((await requestJson(host, "GET", "/authorization-key", {
        headers: { Authorization: "sloppykey authorization-secret" },
    })).response.status, 200);
    assert.equal((await requestJson(host, "GET", "/authorization-key", {
        headers: { Authorization: `SloppyKey ${"x".repeat(4097)}` },
    })).response.status, 401);
    await host.close();
}

{
    let record;
    const store = {
        async create(value) {
            record = { ...value };
        },
        async load(id) {
            return record?.id === id ? { ...record } : undefined;
        },
        async touch(id, lastSeenAt, idleExpiresAt) {
            if (record?.id === id) {
                record = { ...record, lastSeenAt, idleExpiresAt, revokedAt: lastSeenAt };
            }
            return undefined;
        },
        async revoke(id, revokedAt) {
            if (record?.id === id) {
                record = { ...record, revokedAt };
            }
        },
        async cleanup() {},
    };
    const app = Sloppy.create();
    app.use(Auth.cookieSession({
        name: "sloppy.session",
        secret: "touch-reload-secret",
        store,
    }));
    app.post("/login", (ctx) => Auth.signIn(ctx, { sub: "stale-user" }));
    app.get("/me", (ctx) => Results.ok({ subject: ctx.user.sub })).requireAuth();

    const host = Testing.createHost(app);
    const login = await host.post("/login");
    const session = cookieValue(login.headers.get("set-cookie"));
    assert.equal((await requestJson(host, "GET", "/me", {
        headers: { cookie: `sloppy.session=${session}` },
    })).response.status, 401);
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
    const builder = Sloppy.createBuilder();
    builder.config.addObject({
        Auth: {
            JwtSecret: "named-secret",
        },
    });
    const app = builder.build();
    app.use(Auth.configure({
        defaultScheme: "jwt",
        schemes: {
            jwt: Auth.jwtBearer({
                secret: Config.required("Auth:JwtSecret"),
                clock: () => 1_700_000_000_000,
            }),
        },
    }));
    const group = app.group("/secure").requiresAuth("jwt");
    group.get("/me", (ctx) => Results.ok({
        subject: ctx.requireUser().sub,
        scheme: ctx.auth.user.scheme,
        scope: ctx.hasScope("users:read"),
        claim: ctx.hasClaim("tenant", "alpha"),
        claimsSubject: ctx.claims.sub,
    })).requiresScope("users:read");
    group.get("/public", () => Results.ok({ ok: true })).allowAnonymous();

    const host = Testing.createHost(app);
    const token = jwt("named-secret", {
        sub: "named-user",
        scope: "users:read jobs:write",
        tenant: "alpha",
        exp: 1_700_000_600,
    });
    const me = await requestJson(host, "GET", "/secure/me", {
        headers: { Authorization: `Bearer ${token}` },
    });
    assert.equal(me.response.status, 200);
    assert.equal(me.body.subject, "named-user");
    assert.equal(me.body.scheme, "jwt");
    assert.equal(me.body.scope, true);
    assert.equal(me.body.claim, true);
    assert.equal(me.body.claimsSubject, "named-user");
    assert.equal((await requestJson(host, "GET", "/secure/public")).response.status, 200);

    const noScope = jwt("named-secret", { sub: "named-user", exp: 1_700_000_600 });
    assert.equal((await requestJson(host, "GET", "/secure/me", {
        headers: { Authorization: `Bearer ${noScope}` },
    })).response.status, 403);
    await host.close();
}

{
    const app = Sloppy.create();
    app.use(Auth.apiKey({
        keys: [
            {
                id: "policy-user",
                key: "policy-key",
                roles: ["admin"],
                scopes: ["users:read"],
                claims: { tenant: "alpha" },
            },
        ],
    }));
    app.auth.addPolicy("Users.Read", Auth.policy((policy) =>
        policy
            .requireAuthenticated()
            .requireScope("users:read")
            .requireRole("admin")
            .requireClaim("tenant", "alpha")));
    app.auth.addPolicy("Resource.Owner", (_user, _ctx, resource) => resource?.owner === "policy-user");
    app.get("/policy", () => Results.ok({ ok: true })).authorize("Users.Read");
    app.get("/resource", async (ctx) => Results.ok({
        allowed: await ctx.authorize("Resource.Owner", { owner: ctx.user.sub }),
        denied: await ctx.authorize("Resource.Owner", { owner: "other" }),
    })).requireAuth();

    const host = Testing.createHost(app);
    assert.equal((await requestJson(host, "GET", "/policy")).response.status, 401);
    assert.equal((await requestJson(host, "GET", "/policy", {
        headers: { "x-api-key": "policy-key" },
    })).response.status, 200);
    const resource = await requestJson(host, "GET", "/resource", {
        headers: { "x-api-key": "policy-key" },
    });
    assert.equal(resource.response.status, 200);
    assert.equal(resource.body.allowed, true);
    assert.equal(resource.body.denied, false);
    await host.close();
}

{
    const app = Sloppy.create();
    app.use(Auth.configure({
        defaultScheme: "jwt",
        schemes: {
            jwt: Auth.jwtBearer({
                secret: "default-jwt-secret",
                clock: () => 1_700_000_000_000,
            }),
            api: Auth.apiKey({
                keys: [
                    { id: "internal", key: "default-api-key", scopes: ["jobs:write"] },
                ],
            }),
        },
    }));
    app.get("/default", (ctx) => Results.ok({
        subject: ctx.user.sub,
        scheme: ctx.user.scheme,
        authScheme: ctx.user.authScheme,
    })).requiresAuth();
    app.get("/api", (ctx) => Results.ok({
        subject: ctx.user.sub,
        scheme: ctx.user.scheme,
        authScheme: ctx.user.authScheme,
    })).requiresAuth("api");

    const host = Testing.createHost(app);
    const jwtToken = jwt("default-jwt-secret", {
        sub: "jwt-user",
        exp: 1_700_000_600,
    });
    assert.equal((await requestJson(host, "GET", "/default", {
        headers: { "x-api-key": "default-api-key" },
    })).response.status, 401);
    const defaultOk = await requestJson(host, "GET", "/default", {
        headers: { Authorization: `Bearer ${jwtToken}` },
    });
    assert.equal(defaultOk.response.status, 200);
    assert.equal(defaultOk.body.scheme, "jwt");
    assert.equal(defaultOk.body.authScheme, "jwt");
    const apiOk = await requestJson(host, "GET", "/api", {
        headers: { "x-api-key": "default-api-key" },
    });
    assert.equal(apiOk.response.status, 200);
    assert.equal(apiOk.body.scheme, "api");
    assert.equal(apiOk.body.authScheme, "api");
    await host.close();
}

{
    const hash = `sha256:${createHash("sha256").update("hashed-secret").digest("hex")}`;
    const app = Sloppy.create();
    app.use(Auth.apiKey({
        keys: [
            { id: "internal", hash, scopes: ["jobs:write"], roles: ["worker"] },
        ],
    }));
    app.get("/jobs", (ctx) => Results.ok({
        subject: ctx.user.sub,
        scope: ctx.user.hasScope("jobs:write"),
        role: ctx.user.hasRole("worker"),
    })).requiresAuth("apiKey").requiresScope("jobs:write");

    const host = Testing.createHost(app);
    assert.equal((await requestJson(host, "GET", "/jobs", {
        headers: { "x-api-key": "wrong" },
    })).response.status, 401);
    const ok = await requestJson(host, "GET", "/jobs", {
        headers: { "x-api-key": "hashed-secret" },
    });
    assert.equal(ok.response.status, 200);
    assert.equal(ok.body.subject, "internal");
    assert.equal(ok.body.scope, true);
    assert.equal(ok.body.role, true);
    await host.close();
}

{
    const encoded = await Auth.password.hash("password");
    assert.equal(encoded.startsWith("$argon2id$"), true);
    assert.equal(await Auth.password.verify(encoded, "password"), true);
    assert.equal(await Auth.password.needsRehash(encoded, { opsLimit: 3 }), true);
}

{
    const builder = Sloppy.createBuilder();
    builder.config.addObject({
        Auth: {
            SessionSecret: "csrf-session-secret",
        },
    });
    const app = builder.build();
    app.use(Auth.cookieSession({
        secret: Config.required("Auth:SessionSecret"),
        csrf: true,
        clock: () => 1_700_000_000_000,
    }));
    app.post("/login", (ctx) => Auth.signIn(ctx, { sub: "csrf-user" }));
    app.post("/logout", (ctx) => Auth.signOut(ctx));
    app.post("/unsafe", (ctx) => Results.ok({ subject: ctx.user.sub })).requireAuth();

    const host = Testing.createHost(app);
    const login = await host.post("/login");
    const cookies = setCookies(login);
    const session = namedCookieValue(cookies, "sloppy.session");
    const csrf = namedCookieValue(cookies, "__Host-sloppy_csrf");
    assert.equal((await requestJson(host, "POST", "/unsafe", {
        headers: { cookie: `sloppy.session=${session}; __Host-sloppy_csrf=${csrf}` },
    })).response.status, 403);
    assert.equal((await requestJson(host, "POST", "/unsafe", {
        headers: {
            cookie: `sloppy.session=${session}; __Host-sloppy_csrf=${csrf}`,
            "x-csrf-token": csrf,
        },
    })).response.status, 200);
    const logout = await host.post("/logout", {
        headers: {
            cookie: `sloppy.session=${session}; __Host-sloppy_csrf=${csrf}`,
            "x-csrf-token": csrf,
        },
    });
    const logoutCookies = setCookies(logout);
    assert.ok(logoutCookies.some((cookie) => cookie.startsWith("sloppy.session=")));
    const logoutCsrf = logoutCookies.find((cookie) => cookie.startsWith("__Host-sloppy_csrf="));
    assert.ok(logoutCsrf);
    assertCookieDirective(logoutCsrf, "Max-Age=0");
    assertCookieDirective(logoutCsrf, "Expires=Thu, 01 Jan 1970 00:00:00 GMT");
    await host.close();
}

{
    const app = Sloppy.create();
    app.securityHeaders({ contentSecurityPolicy: "default-src 'none'" });
    app.get("/headers", () => Results.ok({ ok: true }));
    const host = Testing.createHost(app);
    const response = await host.get("/headers");
    assert.equal(response.headers.get("x-content-type-options"), "nosniff");
    assert.equal(response.headers.get("x-frame-options"), "DENY");
    assert.equal(response.headers.get("referrer-policy"), "no-referrer");
    assert.equal(response.headers.get("content-security-policy"), "default-src 'none'");
    assert.equal(response.headers.get("strict-transport-security"), undefined);
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
