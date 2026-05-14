import { createHmac } from "node:crypto";

import { ContractAssertionCollector, errorInvariants } from "../runner/assertions.mjs";
import { createFinding, createReport } from "../runner/contract-report.mjs";
import { Auth, Base64Url, Results, Secret, Sloppy, Testing, Text } from "../../../stdlib/sloppy/index.js";

const SUBSYSTEM = "auth";
const SECRET_MARKER = "contract-raw-secret-do-not-leak";

function encodeJwt(secret, claims, header = { alg: "HS256", typ: "JWT" }) {
    const encodedHeader = Base64Url.encode(Text.utf8.encode(JSON.stringify(header)));
    const encodedClaims = Base64Url.encode(Text.utf8.encode(JSON.stringify(claims)));
    const signingInput = `${encodedHeader}.${encodedClaims}`;
    const signature = createHmac("sha256", secret).update(signingInput).digest();
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
    if (cookie === undefined) {
        throw new Error(`missing expected cookie ${name}`);
    }
    return cookieValue(cookie);
}

function assertCookieDirective(setCookie, directive) {
    if (!new RegExp(`(?:^|;\\s*)${directive}(?:;|$)`, "u").test(setCookie)) {
        throw new Error(`cookie is missing ${directive}`);
    }
}

async function requestJson(host, method, target, options = undefined) {
    const response = await host.request(method, target, options);
    const text = response.text();
    return { response, body: text.length === 0 ? null : JSON.parse(text) };
}

function routeByPattern(app, pattern) {
    const route = app.__getRoutes().find((entry) => entry.pattern === pattern);
    if (route === undefined) {
        throw new Error(`missing route ${pattern}`);
    }
    return route;
}

async function record(collector, invariant, callback) {
    try {
        await callback();
        collector.pass(invariant, `${invariant} holds`);
    } catch (error) {
        collector.fail(invariant, `${invariant} failed`, {
            error: error?.name ?? "Error",
            message: error?.message?.replaceAll(SECRET_MARKER, "[redacted]"),
        });
    }
}

function assertStatus(response, expected, invariant) {
    if (response.status !== expected) {
        throw new Error(`${invariant} expected status ${expected}, got ${response.status}`);
    }
}

function assertProblem(response, expectedStatus, expectedCode, invariant) {
    assertStatus(response, expectedStatus, invariant);
    const body = response.problem();
    if (body.status !== expectedStatus || body.code !== expectedCode || typeof body.title !== "string") {
        throw new Error(`${invariant} returned unexpected ProblemDetails body`);
    }
}

function assertNoSecretText(value, invariant) {
    if (JSON.stringify(value).includes(SECRET_MARKER)) {
        throw new Error(`${invariant} leaked raw secret text`);
    }
}

async function withAuthApp(callback) {
    const app = Sloppy.create();
    app.use(Auth.apiKey({
        keys: [
            {
                id: "admin-user",
                key: "admin-key",
                roles: ["admin", "operator"],
                scopes: ["users:read", "users:write"],
                claims: { tenant: "alpha" },
            },
            {
                id: "reader-user",
                key: "reader-key",
                roles: ["reader"],
                scopes: ["users:read"],
                claims: { tenant: "alpha" },
            },
        ],
    }));
    app.auth.addPolicy("Tenant.Alpha", Auth.policy((policy) =>
        policy.requireAuthenticated().requireClaim("tenant", "alpha")));

    let protectedHits = 0;
    let groupHits = 0;
    app.get("/public", () => Results.ok({ public: true }));
    app.get("/protected", (ctx) => {
        protectedHits += 1;
        return Results.ok({ subject: ctx.requireUser().sub, authUser: ctx.auth.user.sub });
    }).requireAuth();
    app.get("/admin", (ctx) => Results.ok({ subject: ctx.user.sub })).requireAuth({ role: "admin" });
    app.get("/write", (ctx) => Results.ok({ subject: ctx.user.sub })).requiresScope("users:write");
    app.get("/tenant", () => Results.ok({ ok: true })).authorize("Tenant.Alpha");

    const group = app.group("/group").requireAuth();
    group.get("/child", () => {
        groupHits += 1;
        return Results.ok({ ok: true });
    });
    const nested = group.group("/nested").requireAuth({ role: "admin" });
    nested.get("/admin", () => Results.ok({ ok: true }));
    nested.get("/public", () => Results.ok({ ok: true })).allowAnonymous();

    const host = Testing.createHost(app);
    try {
        await callback({ app, host, hits: () => ({ protectedHits, groupHits }) });
    } finally {
        await host.close();
    }
}

async function validateRouteProtection(collector) {
    await withAuthApp(async ({ app, host, hits }) => {
        const missing = await host.get("/protected");
        await record(collector, "auth.route.protected", () => {
            assertProblem(missing, 401, "SLOPPY_E_AUTH_UNAUTHORIZED", "auth.route.protected");
        });

        await record(collector, "auth.handler.not-invoked-on-failure", () => {
            if (hits().protectedHits !== 0) {
                throw new Error("protected handler ran after failed auth");
            }
        });

        await record(collector, "auth.route.public", async () => {
            assertStatus(await host.get("/public"), 200, "auth.route.public");
            assertStatus(await host.get("/group/nested/public"), 200, "auth.route.public");
        });

        await record(collector, "auth.group.inheritance", async () => {
            assertStatus(await host.get("/group/child"), 401, "auth.group.inheritance");
            assertStatus(await host.get("/group/child", { headers: { "x-api-key": "reader-key" } }), 200, "auth.group.inheritance");
            assertStatus(await host.get("/group/nested/admin", { headers: { "x-api-key": "reader-key" } }), 403, "auth.group.inheritance");
            assertStatus(await host.get("/group/nested/admin", { headers: { "x-api-key": "admin-key" } }), 200, "auth.group.inheritance");
            if (hits().groupHits !== 1) {
                throw new Error("group child handler invocation count did not match authorized requests");
            }
        });

        await record(collector, "auth.status.unauthorized-forbidden", async () => {
            assertProblem(await host.get("/admin"), 401, "SLOPPY_E_AUTH_UNAUTHORIZED", "auth.status.unauthorized-forbidden");
            assertProblem(
                await host.get("/admin", { headers: { "x-api-key": "reader-key" } }),
                403,
                "SLOPPY_E_AUTH_FORBIDDEN",
                "auth.status.unauthorized-forbidden",
            );
        });

        await record(collector, "auth.roles.enforced", async () => {
            assertStatus(await host.get("/admin", { headers: { "x-api-key": "reader-key" } }), 403, "auth.roles.enforced");
            assertStatus(await host.get("/admin", { headers: { "x-api-key": "admin-key" } }), 200, "auth.roles.enforced");
        });

        await record(collector, "auth.scopes.enforced", async () => {
            assertStatus(await host.get("/write", { headers: { "x-api-key": "reader-key" } }), 403, "auth.scopes.enforced");
            assertStatus(await host.get("/write", { headers: { "x-api-key": "admin-key" } }), 200, "auth.scopes.enforced");
            assertStatus(await host.get("/tenant", { headers: { "x-api-key": "admin-key" } }), 200, "auth.scopes.enforced");
        });

        await record(collector, "auth.metadata.runtime-agreement", () => {
            const protectedAuth = routeByPattern(app, "/protected").metadata.auth;
            const groupAuth = routeByPattern(app, "/group/child").metadata.auth;
            const nestedAuth = routeByPattern(app, "/group/nested/admin").metadata.auth;
            const publicAuth = routeByPattern(app, "/group/nested/public").metadata.auth;
            if (protectedAuth?.required !== true || groupAuth?.required !== true || nestedAuth?.roles?.[0] !== "admin") {
                throw new Error("auth route metadata does not describe protected runtime behavior");
            }
            if (publicAuth?.allowAnonymous !== true || publicAuth.required !== false) {
                throw new Error("allowAnonymous metadata does not describe public override behavior");
            }
            const authContributions = app.__getPlanContributions().auth;
            if (authContributions?.schemes?.some((scheme) => scheme.kind === "apiKey") !== true) {
                throw new Error("Plan contribution auth metadata did not include API key scheme");
            }
        });

        await record(collector, "auth.context.user-after-success", async () => {
            const ok = await requestJson(host, "GET", "/protected", { headers: { "x-api-key": "admin-key" } });
            assertStatus(ok.response, 200, "auth.context.user-after-success");
            if (ok.body.subject !== "admin-user" || ok.body.authUser !== "admin-user") {
                throw new Error("ctx.user/ctx.requireUser did not expose the authenticated principal");
            }
        });
    });
}

async function validateSessionsAndCsrf(collector) {
    let clock = 1_700_000_000_000;
    let touched = 0;
    const store = {
        record: undefined,
        async create(record) {
            this.record = { ...record };
        },
        async load(id) {
            return this.record?.id === id ? { ...this.record } : undefined;
        },
        async touch(id, lastSeenAt, idleExpiresAt) {
            touched += 1;
            if (this.record?.id === id) {
                this.record = { ...this.record, lastSeenAt, idleExpiresAt };
                return { ...this.record };
            }
            return undefined;
        },
        async revoke(id, revokedAt) {
            if (this.record?.id === id) {
                this.record = { ...this.record, revokedAt };
            }
        },
        async cleanup() {},
    };

    const app = Sloppy.create();
    app.use(Auth.cookieSession({
        secret: "session-contract-secret",
        store,
        idleTimeoutMs: 1_000,
        absoluteTimeoutMs: 5_000,
        csrf: { header: "x-csrf-token", cookieName: "__Host-sloppy_csrf" },
        clock: () => clock,
    }));
    app.post("/login", (ctx) => Auth.signIn(ctx, { sub: "session-user", roles: ["user"] }));
    app.get("/me", (ctx) => Results.ok({ subject: ctx.requireUser().sub })).requireAuth();
    app.post("/unsafe", () => Results.ok({ ok: true })).requireAuth();
    app.post("/logout", (ctx) => Auth.signOut(ctx));

    const host = Testing.createHost(app);
    try {
        const login = await host.post("/login");
        const cookies = setCookies(login);
        const session = namedCookieValue(cookies, "sloppy.session");
        const csrf = namedCookieValue(cookies, "__Host-sloppy_csrf");
        const sessionCookie = cookies.find((value) => value.startsWith("sloppy.session="));

        await record(collector, "session.cookie.flags", () => {
            assertCookieDirective(sessionCookie, "Path=/");
            assertCookieDirective(sessionCookie, "SameSite=Lax");
            assertCookieDirective(sessionCookie, "HttpOnly");
            assertCookieDirective(sessionCookie, "Secure");
        });

        await record(collector, "csrf.safe-not-required", async () => {
            assertStatus(await host.get("/me", { headers: { cookie: `sloppy.session=${session}` } }), 200, "csrf.safe-not-required");
        });

        const touchedAfterSafe = touched;
        let touchedAfterFailedCsrf = touchedAfterSafe;
        await record(collector, "csrf.unsafe-required", async () => {
            const failed = await host.post("/unsafe", {
                headers: { cookie: `sloppy.session=${session}; __Host-sloppy_csrf=${csrf}` },
            });
            assertProblem(failed, 403, "SLOPPY_E_AUTH_CSRF_FAILED", "csrf.unsafe-required");
            touchedAfterFailedCsrf = touched;
            assertStatus(await host.post("/unsafe", {
                headers: {
                    cookie: `sloppy.session=${session}; __Host-sloppy_csrf=${csrf}`,
                    "x-csrf-token": csrf,
                },
            }), 200, "csrf.unsafe-required");
        });

        await record(collector, "csrf.failure-no-idle-refresh", () => {
            if (touchedAfterFailedCsrf !== touchedAfterSafe) {
                throw new Error("CSRF failure changed idle refresh count");
            }
        });

        await record(collector, "session.expiry.enforced", async () => {
            clock = 1_700_000_001_001;
            assertStatus(await host.get("/me", { headers: { cookie: `sloppy.session=${session}` } }), 401, "session.expiry.enforced");
            assertNoSecretText(host.diagnostics.snapshot(), "session.expiry.enforced");
        });

        await record(collector, "session.logout.clear", async () => {
            clock = 1_700_000_000_000;
            const secondLogin = await host.post("/login");
            const secondCookies = setCookies(secondLogin);
            const secondSession = namedCookieValue(secondCookies, "sloppy.session");
            const secondCsrf = namedCookieValue(secondCookies, "__Host-sloppy_csrf");
            const logout = await host.post("/logout", {
                headers: {
                    cookie: `sloppy.session=${secondSession}; __Host-sloppy_csrf=${secondCsrf}`,
                    "x-csrf-token": secondCsrf,
                },
            });
            assertStatus(logout, 204, "session.logout.clear");
            const logoutCookies = setCookies(logout).join("\n");
            if (!/sloppy\.session=;.*Max-Age=0/us.test(logoutCookies) ||
                !/__Host-sloppy_csrf=;.*Max-Age=0/us.test(logoutCookies))
            {
                throw new Error("logout did not clear session and CSRF cookies");
            }
            assertStatus(await host.get("/me", { headers: { cookie: `sloppy.session=${secondSession}` } }), 401, "session.logout.clear");
        });
    } finally {
        await host.close();
    }

    await record(collector, "session.rotation-refreshes-plain-success", async () => {
        let rotationClock = 1_700_000_000_000;
        const rotationApp = Sloppy.create();
        rotationApp.use(Auth.cookieSession({
            secret: "rotation-plain-secret",
            store: Auth.sessionStore.memory(),
            rotation: true,
            idleTimeoutMs: 1_000,
            absoluteTimeoutMs: 5_000,
            clock: () => rotationClock,
        }));
        rotationApp.post("/login", (ctx) => Auth.signIn(ctx, { sub: "rotation-plain-user" }));
        rotationApp.get("/plain", () => "ok").requireAuth();
        const rotationHost = Testing.createHost(rotationApp);
        try {
            const login = await rotationHost.post("/login");
            const session = cookieValue(login.headers.get("set-cookie"));
            rotationClock += 500;
            assertStatus(await rotationHost.get("/plain", {
                headers: { cookie: `sloppy.session=${session}` },
            }), 200, "session.rotation-refreshes-plain-success");
            rotationClock += 700;
            assertStatus(await rotationHost.get("/plain", {
                headers: { cookie: `sloppy.session=${session}` },
            }), 200, "session.rotation-refreshes-plain-success");
        } finally {
            await rotationHost.close();
        }
    });
}

async function validateJwtAndCsrfShape(collector) {
    const app = Sloppy.create();
    app.use(Auth.jwtBearer({
        secret: "jwt-contract-secret",
        clock: () => 1_700_000_000_000,
    }));
    app.get("/me", (ctx) => Results.ok({
        subject: ctx.requireUser().sub,
        scope: ctx.hasScope("users:read"),
        role: ctx.hasRole("operator"),
    })).requireAuth({ scopes: ["users:read", "users:write"], roles: ["admin", "operator"] });
    const host = Testing.createHost(app);
    try {
        await record(collector, "auth.scopes.multiple-all-required", async () => {
            const oneScope = encodeJwt("jwt-contract-secret", {
                sub: "jwt-user",
                scope: "users:read",
                roles: ["admin"],
                exp: 1_700_000_600,
            });
            assertStatus(await host.get("/me", { headers: { Authorization: `Bearer ${oneScope}` } }), 403, "auth.scopes.multiple-all-required");
            const bothScopes = encodeJwt("jwt-contract-secret", {
                sub: "jwt-user",
                scope: "users:read users:write",
                roles: ["operator"],
                exp: 1_700_000_600,
            });
            const ok = await requestJson(host, "GET", "/me", { headers: { Authorization: `Bearer ${bothScopes}` } });
            assertStatus(ok.response, 200, "auth.scopes.multiple-all-required");
            if (ok.body.subject !== "jwt-user" || ok.body.scope !== true || ok.body.role !== true) {
                throw new Error("JWT user helpers did not reflect authenticated claims");
            }
        });
    } finally {
        await host.close();
    }

    await record(collector, "csrf.object-config.runtime-valid", async () => {
        const csrfApp = Sloppy.create();
        csrfApp.use(Auth.cookieSession({
            secret: "csrf-object-secret",
            store: Auth.sessionStore.memory(),
            csrf: { header: "x-sloppy-csrf", cookieName: "__Host-sloppy_csrf" },
        }));
        csrfApp.post("/login", (ctx) => Auth.signIn(ctx, { sub: "csrf-object" }));
        csrfApp.post("/unsafe", () => Results.ok({ ok: true })).requireAuth();
        const csrfHost = Testing.createHost(csrfApp);
        try {
            const login = await csrfHost.post("/login");
            const cookies = setCookies(login);
            const session = namedCookieValue(cookies, "sloppy.session");
            const csrf = namedCookieValue(cookies, "__Host-sloppy_csrf");
            assertStatus(await csrfHost.post("/unsafe", {
                headers: {
                    cookie: `sloppy.session=${session}; __Host-sloppy_csrf=${csrf}`,
                    "x-sloppy-csrf": csrf,
                },
            }), 200, "csrf.object-config.runtime-valid");
            const planAuth = csrfApp.__getPlanContributions().auth;
            if (planAuth?.schemes?.some((scheme) => scheme.kind === "cookieSession" && scheme.csrf === true) !== true) {
                throw new Error("runtime-valid object-form CSRF config did not contribute CSRF auth metadata");
            }
        } finally {
            await csrfHost.close();
        }
    });
}

async function validateSecretsAndDiagnostics(collector) {
    await record(collector, "secret.non-enumerable", () => {
        const secret = Secret.fromUtf8(SECRET_MARKER);
        if (Object.keys(secret).length !== 0 || Object.keys({ ...secret }).length !== 0) {
            throw new Error("SecretValue exposed enumerable bytes");
        }
        secret.dispose();
        try {
            secret.bytes();
            throw new Error("disposed secret bytes remained readable");
        } catch (error) {
            if (!/SLOPPY_E_CRYPTO_SECRET_DISPOSED/u.test(error.message)) {
                throw error;
            }
        }
    });

    await record(collector, "secret.json-redacted", () => {
        const secret = Secret.fromUtf8(SECRET_MARKER);
        if (JSON.stringify(secret).includes(SECRET_MARKER) || JSON.stringify({ secret }).includes(SECRET_MARKER)) {
            throw new Error("SecretValue JSON exposed bytes");
        }
        secret.dispose();
    });

    await record(collector, "diagnostics.auth-redaction", async () => {
        const app = Sloppy.create();
        app.get("/diagnostics", (ctx) => {
            ctx.diagnostics.record({
                code: "SLOPPY_CONTRACT_DIAGNOSTIC_REDACTION",
                subsystem: "auth",
                severity: "warn",
                message: `Authorization: Bearer ${SECRET_MARKER}; Cookie: sid=${SECRET_MARKER}; Set-Cookie: sid=${SECRET_MARKER}; token=${SECRET_MARKER}; api_key=${SECRET_MARKER}; apiKey=${SECRET_MARKER}; api-key=${SECRET_MARKER}; password=${SECRET_MARKER}; connectionString=postgres://user:${SECRET_MARKER}@db/app`,
                fields: {
                    Authorization: `Bearer ${SECRET_MARKER}`,
                    Cookie: `sid=${SECRET_MARKER}`,
                    "Set-Cookie": `sid=${SECRET_MARKER}`,
                    token: SECRET_MARKER,
                    api_key: SECRET_MARKER,
                    apiKey: SECRET_MARKER,
                    "api-key": SECRET_MARKER,
                    password: SECRET_MARKER,
                    connectionString: `postgres://user:${SECRET_MARKER}@db/app`,
                    nested: {
                        safe: "visible",
                        token: SECRET_MARKER,
                    },
                },
            });
            return Results.ok({ ok: true });
        });
        const host = Testing.createHost(app, { secrets: { CONTRACT_SECRET: SECRET_MARKER } });
        try {
            assertStatus(await host.get("/diagnostics"), 200, "diagnostics.auth-redaction");
            const diagnostics = host.diagnostics.snapshot();
            host.diagnostics.expectNoSecretLeaks();
            assertNoSecretText(diagnostics, "diagnostics.auth-redaction");
            const diagnosticText = JSON.stringify(diagnostics);
            for (const label of ["Authorization", "Cookie", "Set-Cookie", "token", "api_key", "apiKey", "api-key", "password", "connectionString"]) {
                if (!diagnosticText.includes(label)) {
                    throw new Error(`diagnostic redaction did not exercise ${label}`);
                }
            }
        } finally {
            await host.close();
        }
    });
}

function negativeFinding(fixture, invariant, detected, rawFindings) {
    return createFinding({
        id: `${SUBSYSTEM}.${fixture}.negative.${invariant}`,
        status: "pass",
        severity: "info",
        subsystem: SUBSYSTEM,
        invariant: `negative.${invariant}`,
        fixture,
        message: `broken fixture produced expected ${invariant} finding`,
        details: {
            detected,
            detectedFindings: rawFindings
                .filter((finding) => finding.status === "fail" && finding.severity === "error")
                .map((finding) => ({
                    invariant: finding.invariant,
                    message: finding.message,
                })),
        },
    });
}

function missingNegativeFinding(fixture, invariant, detected) {
    return createFinding({
        id: `${SUBSYSTEM}.${fixture}.negative.${invariant}.missing`,
        status: "fail",
        severity: "error",
        subsystem: SUBSYSTEM,
        invariant: `negative.${invariant}`,
        fixture,
        message: `broken fixture did not produce expected ${invariant} finding`,
        details: { detected },
    });
}

function validateSecurityTranscript(fixture, transcript) {
    const collector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture });
    if (transcript.protectedMissingStatus !== 401) {
        collector.fail("auth.route.protected", "protected route must reject missing credentials");
    }
    if (transcript.failedAuthInvokedHandler === true) {
        collector.fail("auth.handler.not-invoked-on-failure", "failed auth must not invoke the handler");
    }
    if (transcript.wrongRoleStatus !== 403) {
        collector.fail("auth.roles.enforced", "wrong role must be forbidden");
    }
    if (transcript.csrfFailureRefreshedIdle === true) {
        collector.fail("csrf.failure-no-idle-refresh", "failed CSRF must not refresh idle timeout");
    }
    if (JSON.stringify(transcript.secretJson ?? "").includes(SECRET_MARKER)) {
        collector.fail("secret.json-redacted", "JSON serialization must not contain raw secret bytes");
    }
    if (JSON.stringify(transcript.diagnostics ?? "").includes(SECRET_MARKER)) {
        collector.fail("diagnostics.auth-redaction", "diagnostics must redact auth and cookie secrets");
    }
    return collector.findings;
}

function validateNegativeFixtures() {
    const fixtures = [
        {
            name: "broken-protected-route-public",
            invariant: "auth.route.protected",
            transcript: { protectedMissingStatus: 200 },
        },
        {
            name: "broken-handler-invoked",
            invariant: "auth.handler.not-invoked-on-failure",
            transcript: { protectedMissingStatus: 401, failedAuthInvokedHandler: true },
        },
        {
            name: "broken-wrong-role-accepted",
            invariant: "auth.roles.enforced",
            transcript: { protectedMissingStatus: 401, wrongRoleStatus: 200 },
        },
        {
            name: "broken-csrf-refresh",
            invariant: "csrf.failure-no-idle-refresh",
            transcript: { protectedMissingStatus: 401, wrongRoleStatus: 403, csrfFailureRefreshedIdle: true },
        },
        {
            name: "broken-secret-json",
            invariant: "secret.json-redacted",
            transcript: { protectedMissingStatus: 401, wrongRoleStatus: 403, secretJson: SECRET_MARKER },
        },
        {
            name: "broken-authorization-diagnostic",
            invariant: "diagnostics.auth-redaction",
            transcript: {
                protectedMissingStatus: 401,
                wrongRoleStatus: 403,
                diagnostics: { Authorization: `Bearer ${SECRET_MARKER}`, Cookie: SECRET_MARKER, "Set-Cookie": SECRET_MARKER },
            },
        },
    ];
    const findings = [];
    for (const fixture of fixtures) {
        const rawFindings = validateSecurityTranscript(fixture.name, fixture.transcript);
        const detected = errorInvariants(rawFindings);
        findings.push(
            detected.includes(fixture.invariant)
                ? negativeFinding(fixture.name, fixture.invariant, detected, rawFindings)
                : missingNegativeFinding(fixture.name, fixture.invariant, detected),
        );
    }
    return findings;
}

export async function validateAuthContracts() {
    const collector = new ContractAssertionCollector({ subsystem: SUBSYSTEM, fixture: "runtime" });
    const previousSloppy = globalThis.__sloppy;
    let randomCounter = 0;
    globalThis.__sloppy = {
        ...(previousSloppy ?? {}),
        crypto: {
            ...(previousSloppy?.crypto ?? {}),
            hmac(algorithm, key, bytes) {
                if (algorithm !== "sha256") {
                    throw new Error("unsupported test hmac algorithm");
                }
                return new Uint8Array(createHmac("sha256", Buffer.from(key)).update(Buffer.from(bytes)).digest());
            },
            randomToken(length) {
                randomCounter += 1;
                return `contract-token-${length}-${randomCounter}`;
            },
        },
    };
    try {
        await validateRouteProtection(collector);
        await validateSessionsAndCsrf(collector);
        await validateJwtAndCsrfShape(collector);
        await validateSecretsAndDiagnostics(collector);
    } finally {
        if (previousSloppy === undefined) {
            delete globalThis.__sloppy;
        } else {
            globalThis.__sloppy = previousSloppy;
        }
    }
    return [...collector.findings, ...validateNegativeFixtures()];
}

export async function runAuthContract({ tier }) {
    const startedAt = new Date().toISOString();
    const findings = await validateAuthContracts();
    const report = createReport({
        subsystem: SUBSYSTEM,
        tier,
        startedAt,
        finishedAt: new Date().toISOString(),
        findings,
    });
    if (JSON.stringify(report).includes(SECRET_MARKER)) {
        throw new Error("auth contract report leaked raw secret text");
    }
    return report;
}
