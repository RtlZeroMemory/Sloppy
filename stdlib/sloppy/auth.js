import { Base64Url, Hex, Text } from "./codec.js";
import { Hash, Hmac, Password, Random } from "./crypto.js";
import {
    isPlainObject,
    optionalBoolean,
    optionalInteger,
    optionalPositiveInteger,
    requireHttpToken,
    requireNonEmptyString,
} from "./internal/validation.js";
import { Results } from "./results.js";

const AUTH_HEADER = "authorization";
const JWT_SCHEME = "bearerAuth";
const API_KEY_SCHEME = "apiKeyAuth";
const COOKIE_SESSION_SCHEME = "cookieSessionAuth";
const DEFAULT_SESSION_COOKIE = "sloppy.session";
const DEFAULT_CSRF_COOKIE = "__Host-sloppy_csrf";
const DEFAULT_CSRF_HEADER = "x-csrf-token";
const SAFE_CSRF_METHODS = new Set(["GET", "HEAD", "OPTIONS", "TRACE"]);
const STANDARD_JWT_CLAIMS = new Set(["iss", "sub", "aud", "exp", "nbf", "iat", "jti", "name", "role", "roles", "scope", "scp", "scopes"]);
const DEFAULT_SESSION_IDLE_TIMEOUT_MS = 30 * 60_000;
const DEFAULT_SESSION_ABSOLUTE_TIMEOUT_MS = 24 * 60 * 60_000;
const DEFAULT_SIGNED_SESSION_MAX_AGE_SECONDS = 24 * 60 * 60;

function validateHeaderName(name, subject) {
    requireHttpToken(name, `Sloppy ${subject} header must be an HTTP token string.`);
}

function isConfigReference(value) {
    return isPlainObject(value) && value.__sloppyConfigReference === true && typeof value.key === "string";
}

function secretString(value, config, subject) {
    let resolved = value;
    if (isConfigReference(value)) {
        resolved = config.require(value.key);
    }
    if (resolved !== null && typeof resolved === "object" && typeof resolved.value === "function") {
        resolved = resolved.value();
    }
    if (typeof resolved !== "string" || resolved.length === 0) {
        throw new TypeError(`Sloppy ${subject} secret must resolve to a non-empty string.`);
    }
    return resolved;
}

function stringOption(value, subject, required = true) {
    if (value === undefined && !required) {
        return undefined;
    }
    return requireNonEmptyString(value, `Sloppy ${subject} must be a non-empty string.`);
}

function stringArrayOption(value, subject, { lower = false } = {}) {
    if (value === undefined) {
        return Object.freeze([]);
    }
    const values = Array.isArray(value) ? value : [value];
    const output = values.map((entry) => {
        const normalized = stringOption(entry, subject);
        return lower ? normalized.toLowerCase() : normalized;
    });
    return Object.freeze([...new Set(output)]);
}

function booleanOption(value, subject, defaultValue) {
    return optionalBoolean(value, `Sloppy ${subject} must be a boolean.`, defaultValue);
}

function integerOption(value, subject, defaultValue = undefined) {
    return optionalInteger(value, `Sloppy ${subject} must be an integer.`, defaultValue);
}

function authProblem(status, title, code, headers = undefined) {
    return Results.problem(
        Object.freeze({
            status,
            title,
            code,
        }),
        Object.freeze({
            status,
            ...(headers === undefined ? {} : { headers }),
        }),
    );
}

function unauthorized(challenge = undefined) {
    return authProblem(
        401,
        "Unauthorized",
        "SLOPPY_E_AUTH_UNAUTHORIZED",
        challenge === undefined ? undefined : Object.freeze({ "WWW-Authenticate": challenge }),
    );
}

function forbidden() {
    return authProblem(403, "Forbidden", "SLOPPY_E_AUTH_FORBIDDEN");
}

function csrfForbidden() {
    return authProblem(403, "Forbidden", "SLOPPY_E_AUTH_CSRF_FAILED");
}

function constantTimeStringEquals(left, right) {
    left = String(left);
    right = String(right);
    if (left.length === 0 || right.length === 0) {
        return left.length === right.length;
    }
    let diff = left.length ^ right.length;
    const length = Math.max(left.length, right.length);
    for (let index = 0; index < length; index += 1) {
        diff |= left.charCodeAt(index % left.length) ^ right.charCodeAt(index % right.length);
    }
    return diff === 0;
}

function userFromClaims(claims, scheme, authScheme = scheme) {
    const roles = Array.isArray(claims.roles)
        ? claims.roles.filter((role) => typeof role === "string")
        : (typeof claims.role === "string" ? [claims.role] : []);
    const scopes = scopesFromClaims(claims);
    const allClaims = Object.freeze({ ...claims });
    const user = {
        authenticated: true,
        sub: typeof claims.sub === "string" ? claims.sub : "",
        name: typeof claims.name === "string" ? claims.name : "",
        roles: Object.freeze([...new Set(roles)]),
        scopes: Object.freeze([...new Set(scopes)]),
        claims: allClaims,
        scheme,
        authScheme,
        hasRole(role) {
            return typeof role === "string" && user.roles.includes(role);
        },
        hasScope(scope) {
            return typeof scope === "string" && user.scopes.includes(scope);
        },
        hasClaim(name, value = undefined) {
            if (typeof name !== "string" || !Object.prototype.hasOwnProperty.call(user.claims, name)) {
                return false;
            }
            return value === undefined ? true : Object.is(user.claims[name], value);
        },
    };
    return Object.freeze(user);
}

function scopesFromClaims(claims) {
    const scopes = [];
    if (typeof claims.scope === "string") {
        scopes.push(...claims.scope.split(/\s+/u).filter((scope) => scope.length !== 0));
    }
    if (typeof claims.scp === "string") {
        scopes.push(...claims.scp.split(/\s+/u).filter((scope) => scope.length !== 0));
    }
    for (const key of ["scopes", "scp"]) {
        if (Array.isArray(claims[key])) {
            scopes.push(...claims[key].filter((scope) => typeof scope === "string" && scope.length !== 0));
        }
    }
    return scopes;
}

export function anonymousUser() {
    const user = {
        authenticated: false,
        sub: "",
        name: "",
        roles: Object.freeze([]),
        scopes: Object.freeze([]),
        claims: Object.freeze({}),
        scheme: "",
        authScheme: "",
        hasRole() {
            return false;
        },
        hasScope() {
            return false;
        },
        hasClaim() {
            return false;
        },
    };
    return Object.freeze(user);
}

function headerValue(ctx, name) {
    const headers = ctx?.request?.headers;
    if (headers === undefined || headers === null || typeof headers.get !== "function") {
        return undefined;
    }
    return headers.get(name);
}

function bearerToken(ctx) {
    const value = headerValue(ctx, AUTH_HEADER);
    if (typeof value !== "string") {
        return undefined;
    }
    if (/[\r\n,]/u.test(value)) {
        return null;
    }
    const match = value.match(/^Bearer ([A-Za-z0-9_-]+(?:\.[A-Za-z0-9_-]+){2})$/iu);
    return match === null ? null : match[1];
}

function jsonFromBase64Url(value, subject) {
    try {
        return JSON.parse(Text.utf8.decode(Base64Url.decode(value, { padding: "optional" })));
    } catch {
        throw new Error(`SLOPPY_E_AUTH_INVALID_TOKEN: ${subject} is not valid JWT JSON.`);
    }
}

function nowSeconds(clock = Date.now) {
    return Math.floor(clock() / 1000);
}

function nowMilliseconds(clock = Date.now) {
    return clock();
}

function audienceMatches(expected, actual) {
    if (expected === undefined) {
        return true;
    }
    if (Array.isArray(actual)) {
        return actual.includes(expected);
    }
    return actual === expected;
}

function selectJwtKey(header, scheme) {
    if (scheme.keys.length === 0) {
        return Object.freeze({ algorithm: "HS256", secret: scheme.secret });
    }
    const kid = typeof header.kid === "string" ? header.kid : undefined;
    const key = scheme.keys.find((entry) => {
        if (kid !== undefined) {
            return entry.kid === kid;
        }
        return entry.default === true || entry.kid === undefined;
    });
    if (key === undefined) {
        throw new Error("SLOPPY_E_AUTH_INVALID_TOKEN: JWT kid is unknown.");
    }
    return key;
}

async function verifyJwtSignature(header, signingInput, signaturePart, scheme) {
    if (!scheme.algorithms.includes(header.alg)) {
        throw new Error("SLOPPY_E_AUTH_UNSUPPORTED_ALGORITHM: JWT algorithm is not allowed.");
    }
    const key = selectJwtKey(header, scheme);
    if (key.algorithm !== header.alg) {
        throw new Error("SLOPPY_E_AUTH_UNSUPPORTED_ALGORITHM: JWT key algorithm does not match token alg.");
    }
    if (header.alg === "HS256") {
        if (key.secret === undefined && scheme.secret === undefined) {
            throw new Error("SLOPPY_E_AUTH_INVALID_SIGNATURE: JWT HMAC key is not configured.");
        }
        const expected = await Hmac.sha256(key.secret ?? scheme.secret, signingInput);
        const actual = Base64Url.decode(signaturePart, { padding: "optional" });
        if (!constantTimeBytesEquals(expected, actual)) {
            throw new Error("SLOPPY_E_AUTH_INVALID_SIGNATURE: JWT signature is invalid.");
        }
        return;
    }
    if (header.alg === "RS256") {
        const subtle = globalThis.crypto?.subtle;
        if (subtle === undefined || key.jwk === undefined) {
            throw new Error("SLOPPY_E_AUTH_UNSUPPORTED_ALGORITHM: RS256 requires WebCrypto static JWK support.");
        }
        const cryptoKey = await subtle.importKey(
            "jwk",
            key.jwk,
            { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" },
            false,
            ["verify"],
        );
        const ok = await subtle.verify(
            "RSASSA-PKCS1-v1_5",
            cryptoKey,
            Base64Url.decode(signaturePart, { padding: "optional" }),
            Text.utf8.encode(signingInput),
        );
        if (!ok) {
            throw new Error("SLOPPY_E_AUTH_INVALID_SIGNATURE: JWT signature is invalid.");
        }
        return;
    }
    throw new Error("SLOPPY_E_AUTH_UNSUPPORTED_ALGORITHM: JWT algorithm is not supported.");
}

async function verifyJwt(token, scheme) {
    const parts = String(token).split(".");
    if (parts.length !== 3 || parts.some((part) => part.length === 0)) {
        throw new Error("SLOPPY_E_AUTH_INVALID_TOKEN: JWT must have header, payload, and signature.");
    }
    const header = jsonFromBase64Url(parts[0], "JWT header");
    if (!isPlainObject(header) || typeof header.alg !== "string") {
        throw new Error("SLOPPY_E_AUTH_INVALID_TOKEN: JWT header must include alg.");
    }
    if (header.alg === "none") {
        throw new Error("SLOPPY_E_AUTH_INVALID_TOKEN: JWT alg none is not supported.");
    }
    await verifyJwtSignature(header, `${parts[0]}.${parts[1]}`, parts[2], scheme);
    const claims = jsonFromBase64Url(parts[1], "JWT payload");
    if (!isPlainObject(claims)) {
        throw new Error("SLOPPY_E_AUTH_INVALID_TOKEN: JWT payload must be a JSON object.");
    }
    const current = nowSeconds(scheme.clock);
    if (scheme.issuer !== undefined && claims.iss !== scheme.issuer) {
        throw new Error("SLOPPY_E_AUTH_INVALID_TOKEN: JWT issuer is invalid.");
    }
    if (!audienceMatches(scheme.audience, claims.aud)) {
        throw new Error("SLOPPY_E_AUTH_INVALID_TOKEN: JWT audience is invalid.");
    }
    if (claims.exp !== undefined && (!Number.isFinite(claims.exp) ||
            claims.exp <= current - scheme.clockSkewSeconds)) {
        throw new Error("SLOPPY_E_AUTH_EXPIRED_TOKEN: JWT is expired.");
    }
    if (claims.nbf !== undefined && (!Number.isFinite(claims.nbf) ||
            claims.nbf > current + scheme.clockSkewSeconds)) {
        throw new Error("SLOPPY_E_AUTH_INVALID_TOKEN: JWT is not active yet.");
    }
    if (claims.iat !== undefined && (!Number.isFinite(claims.iat) ||
            claims.iat > current + scheme.clockSkewSeconds)) {
        throw new Error("SLOPPY_E_AUTH_INVALID_TOKEN: JWT iat must be numeric seconds not in the future.");
    }
    if (claims.sub !== undefined && typeof claims.sub !== "string") {
        throw new Error("SLOPPY_E_AUTH_INVALID_TOKEN: JWT sub must be a string when present.");
    }
    return userFromClaims(claims, scheme.principalScheme, scheme.name);
}

function constantTimeBytesEquals(left, right) {
    if (!(left instanceof Uint8Array) || !(right instanceof Uint8Array)) {
        return false;
    }
    if (left.byteLength === 0 || right.byteLength === 0) {
        return left.byteLength === right.byteLength;
    }
    let diff = left.byteLength ^ right.byteLength;
    const length = Math.max(left.byteLength, right.byteLength);
    for (let index = 0; index < length; index += 1) {
        diff |= left[index % left.byteLength] ^ right[index % right.byteLength];
    }
    return diff === 0;
}

async function jwtMiddleware(ctx, next, scheme) {
    const token = bearerToken(ctx);
    if (token === undefined) {
        return next();
    }
    if (token === null) {
        return unauthorized("Bearer");
    }
    try {
        ctx.user = await verifyJwt(token, scheme);
        return next();
    } catch {
        return unauthorized("Bearer error=\"invalid_token\"");
    }
}

function normalizeApiKeyUser(result, scheme) {
    if (result === true) {
        return userFromClaims({ sub: "api-key" }, scheme.principalScheme, scheme.name);
    }
    if (isPlainObject(result)) {
        return userFromClaims(result, scheme.principalScheme, scheme.name);
    }
    return undefined;
}

async function staticApiKeyUser(key, scheme) {
    let keySha256 = undefined;
    for (const entry of scheme.keys) {
        let matched = false;
        if (entry.hash !== undefined) {
            keySha256 ??= `sha256:${Hex.encode(await Hash.sha256(key))}`;
            matched = constantTimeStringEquals(keySha256, entry.hash);
        } else if (entry.key !== undefined) {
            matched = constantTimeStringEquals(key, entry.key);
        }
        if (matched) {
            return userFromClaims({
                sub: entry.id,
                roles: entry.roles,
                scopes: entry.scopes,
                ...(entry.claims ?? {}),
            }, scheme.principalScheme, scheme.name);
        }
    }
    return undefined;
}

async function apiKeyMiddleware(ctx, next, scheme) {
    let key = headerValue(ctx, scheme.header);
    if (key === undefined && scheme.authorizationScheme !== undefined) {
        const authorization = headerValue(ctx, AUTH_HEADER);
        if (typeof authorization === "string" && !/[\r\n,]/u.test(authorization)) {
            const prefix = `${scheme.authorizationScheme} `;
            key = authorization.toLowerCase().startsWith(prefix.toLowerCase())
                ? authorization.slice(prefix.length)
                : undefined;
        }
    }
    if (key === undefined) {
        return next();
    }
    if (typeof key !== "string" || key.length === 0 || key.length > scheme.maxLength || /[\r\n]/u.test(key)) {
        return unauthorized(`${scheme.header}`);
    }
    if (scheme.keys.length !== 0) {
        const user = await staticApiKeyUser(key, scheme);
        if (user === undefined) {
            return unauthorized(`${scheme.header}`);
        }
        ctx.user = user;
        return next();
    }
    if (scheme.expectedKey !== undefined) {
        if (!constantTimeStringEquals(key, scheme.expectedKey)) {
            return unauthorized(`${scheme.header}`);
        }
        ctx.user = userFromClaims({ sub: "api-key" }, scheme.principalScheme, scheme.name);
        return next();
    }
    let result;
    try {
        result = await scheme.validate(key, {
            constantTimeEquals: constantTimeStringEquals,
            expectedKey: scheme.configKey === undefined ? undefined : secretString(
                { __sloppyConfigReference: true, key: scheme.configKey },
                scheme.config,
                "API key",
            ),
        });
    } catch {
        result = false;
    }
    const user = normalizeApiKeyUser(result, scheme);
    if (user === undefined) {
        return unauthorized(`${scheme.header}`);
    }
    ctx.user = user;
    return next();
}

function sessionCookieValue(ctx, name) {
    const cookies = ctx?.cookies;
    if (cookies === undefined || cookies === null || typeof cookies.get !== "function") {
        return undefined;
    }
    return cookies.get(name) ?? undefined;
}

function normalizeSessionClaims(claims) {
    if (!isPlainObject(claims)) {
        throw new TypeError("Sloppy Auth.signIn claims must be a plain object.");
    }
    const copied = { ...claims };
    if (copied.sub !== undefined && typeof copied.sub !== "string") {
        throw new TypeError("Sloppy Auth.signIn sub must be a string when provided.");
    }
    if (copied.roles !== undefined && (!Array.isArray(copied.roles) ||
            !copied.roles.every((role) => typeof role === "string"))) {
        throw new TypeError("Sloppy Auth.signIn roles must be an array of strings when provided.");
    }
    if (copied.claims !== undefined) {
        if (!isPlainObject(copied.claims)) {
            throw new TypeError("Sloppy Auth.signIn claims.claims must be a plain object when provided.");
        }
        Object.assign(copied, copied.claims);
        delete copied.claims;
    }
    return copied;
}

function sessionCookieOptions(scheme, overrides = undefined) {
    if (overrides !== undefined && !isPlainObject(overrides)) {
        throw new TypeError("Sloppy Auth session cookie options must be a plain object.");
    }
    return Object.freeze({
        path: overrides?.path ?? scheme.path,
        secure: overrides?.secure ?? scheme.secure,
        httpOnly: overrides?.httpOnly ?? scheme.httpOnly,
        sameSite: overrides?.sameSite ?? scheme.sameSite,
        ...(overrides?.maxAgeSeconds !== undefined || scheme.maxAgeSeconds !== undefined
            ? { maxAgeSeconds: overrides?.maxAgeSeconds ?? scheme.maxAgeSeconds }
            : {}),
        ...(overrides?.expires !== undefined ? { expires: overrides.expires } : {}),
    });
}

function sessionStoreCookieOptions(scheme, overrides = undefined, remainingAbsoluteLifetimeMs = undefined) {
    const options = sessionCookieOptions(scheme, overrides);
    let maxAgeSeconds = options.maxAgeSeconds;
    if (overrides?.maxAgeSeconds === undefined && scheme.absoluteTimeoutMs !== undefined) {
        maxAgeSeconds = Math.ceil(scheme.absoluteTimeoutMs / 1000);
    }
    if (remainingAbsoluteLifetimeMs !== undefined) {
        const remainingSeconds = Math.max(0, Math.ceil(remainingAbsoluteLifetimeMs / 1000));
        maxAgeSeconds = maxAgeSeconds === undefined
            ? remainingSeconds
            : Math.min(maxAgeSeconds, remainingSeconds);
    }
    return Object.freeze({
        ...options,
        ...(maxAgeSeconds !== undefined ? { maxAgeSeconds } : {}),
    });
}

async function signSessionPayload(scheme, payload) {
    const body = Base64Url.encode(Text.utf8.encode(JSON.stringify(payload)));
    const signature = await Hmac.sha256(scheme.secret, body);
    return `${body}.${Base64Url.encode(signature)}`;
}

async function verifySessionCookie(value, scheme) {
    const parts = String(value).split(".");
    if (parts.length !== 2 || parts.some((part) => part.length === 0)) {
        return undefined;
    }
    const expected = await Hmac.sha256(scheme.secret, parts[0]);
    let actual;
    try {
        actual = Base64Url.decode(parts[1], { padding: "optional" });
    } catch {
        return undefined;
    }
    if (!constantTimeBytesEquals(expected, actual)) {
        return undefined;
    }
    let payload;
    try {
        payload = JSON.parse(Text.utf8.decode(Base64Url.decode(parts[0], { padding: "optional" })));
    } catch {
        return undefined;
    }
    if (!isPlainObject(payload) || !isPlainObject(payload.claims)) {
        return undefined;
    }
    const current = nowSeconds(scheme.clock);
    if (payload.exp !== undefined && (!Number.isFinite(payload.exp) || payload.exp <= current)) {
        return undefined;
    }
    const user = userFromClaims(payload.claims, scheme.principalScheme, scheme.name);
    return Object.freeze({ user, payload });
}

async function signSessionId(scheme, sessionId) {
    return signSessionPayload(scheme, { sid: sessionId });
}

async function verifySessionIdCookie(value, scheme) {
    const parts = String(value).split(".");
    if (parts.length !== 2 || parts.some((part) => part.length === 0)) {
        return undefined;
    }
    const expected = await Hmac.sha256(scheme.secret, parts[0]);
    let actual;
    try {
        actual = Base64Url.decode(parts[1], { padding: "optional" });
    } catch {
        return undefined;
    }
    if (!constantTimeBytesEquals(expected, actual)) {
        return undefined;
    }
    let payload;
    try {
        payload = JSON.parse(Text.utf8.decode(Base64Url.decode(parts[0], { padding: "optional" })));
    } catch {
        return undefined;
    }
    return typeof payload.sid === "string" && payload.sid.length !== 0 ? payload.sid : undefined;
}

function sessionRecordExpired(record, current) {
    return record.revokedAt !== undefined ||
        (record.expiresAt !== undefined && record.expiresAt <= current) ||
        (record.idleExpiresAt !== undefined && record.idleExpiresAt <= current);
}

async function verifyStoredSessionCookie(value, scheme) {
    const sessionId = await verifySessionIdCookie(value, scheme);
    if (sessionId === undefined) {
        return undefined;
    }
    const current = nowMilliseconds(scheme.clock);
    const record = await scheme.store.load(sessionId);
    if (record === undefined || sessionRecordExpired(record, current)) {
        if (record !== undefined) {
            await scheme.store.revoke(sessionId, current).catch(() => {});
        }
        return undefined;
    }
    const user = userFromClaims(record.claims, scheme.principalScheme, scheme.name);
    return Object.freeze({
        user,
        payload: Object.freeze({
            sid: sessionId,
            iat: Math.floor(record.createdAt / 1000),
            exp: record.expiresAt === undefined ? undefined : Math.floor(record.expiresAt / 1000),
            csrf: record.csrf,
        }),
        record,
    });
}

async function refreshStoredSession(verified, scheme) {
    const sessionId = verified.payload.sid;
    if (sessionId === undefined) {
        return verified;
    }
    const current = nowMilliseconds(scheme.clock);
    const nextIdleExpiresAt = scheme.idleTimeoutMs === undefined ? undefined : current + scheme.idleTimeoutMs;
    const touched = await scheme.store.touch(sessionId, current, nextIdleExpiresAt);
    const active = touched ?? await scheme.store.load(sessionId);
    if (active === undefined || sessionRecordExpired(active, current)) {
        return undefined;
    }
    const user = userFromClaims(active.claims, scheme.principalScheme, scheme.name);
    return Object.freeze({
        user,
        payload: Object.freeze({
            sid: sessionId,
            iat: Math.floor(active.createdAt / 1000),
            exp: active.expiresAt === undefined ? undefined : Math.floor(active.expiresAt / 1000),
            csrf: active.csrf,
        }),
        record: active,
    });
}

async function cookieSessionMiddleware(ctx, next, scheme) {
    const value = sessionCookieValue(ctx, scheme.cookieName);
    if (value === undefined) {
        return next();
    }
    let verified = scheme.store === undefined
        ? await verifySessionCookie(value, scheme)
        : await verifyStoredSessionCookie(value, scheme);
    if (verified === undefined) {
        return unauthorized();
    }
    ctx.user = verified.user;
    ctx.session = Object.freeze({
        scheme: scheme.name,
        id: verified.payload.sid,
        issuedAt: verified.payload.iat,
        expiresAt: verified.payload.exp,
        csrfToken: verified.payload.csrf,
        revoke: verified.payload.sid === undefined
            ? undefined
            : () => scheme.store.revoke(verified.payload.sid, nowMilliseconds(scheme.clock)),
    });
    if (scheme.csrf.enabled && !SAFE_CSRF_METHODS.has(String(ctx.request?.method ?? "").toUpperCase())) {
        const header = headerValue(ctx, scheme.csrf.header);
        const cookie = sessionCookieValue(ctx, scheme.csrf.cookieName);
        if (
            typeof verified.payload.csrf !== "string" ||
            typeof header !== "string" ||
            typeof cookie !== "string" ||
            !constantTimeStringEquals(header, verified.payload.csrf) ||
            !constantTimeStringEquals(cookie, verified.payload.csrf)
        ) {
            return csrfForbidden();
        }
    }
    if (scheme.store !== undefined && scheme.rotation !== true) {
        verified = await refreshStoredSession(verified, scheme);
        if (verified === undefined) {
            return unauthorized();
        }
        ctx.user = verified.user;
        ctx.session = Object.freeze({
            scheme: scheme.name,
            id: verified.payload.sid,
            issuedAt: verified.payload.iat,
            expiresAt: verified.payload.exp,
            csrfToken: verified.payload.csrf,
            revoke: () => scheme.store.revoke(verified.payload.sid, nowMilliseconds(scheme.clock)),
        });
    }
    if (scheme.store === undefined || scheme.rotation !== true || verified.payload.sid === undefined) {
        return next();
    }
    const result = await next();
    if (result === null || typeof result !== "object" || typeof result.cookie !== "function") {
        return result;
    }
    const currentMs = nowMilliseconds(scheme.clock);
    const remainingAbsoluteLifetimeMs = verified.record.expiresAt === undefined
        ? undefined
        : verified.record.expiresAt - currentMs;
    if (remainingAbsoluteLifetimeMs !== undefined && remainingAbsoluteLifetimeMs <= 0) {
        await scheme.store.revoke(verified.payload.sid, currentMs);
        return unauthorized();
    }
    const sessionId = Random.token(32);
    await scheme.store.revoke(verified.payload.sid, currentMs);
    const csrf = scheme.csrf.enabled ? Random.token(32) : verified.payload.csrf;
    const record = {
        id: sessionId,
        claims: verified.record.claims,
        createdAt: verified.record.createdAt,
        lastSeenAt: currentMs,
        expiresAt: verified.record.expiresAt,
        idleExpiresAt: scheme.idleTimeoutMs === undefined ? undefined : currentMs + scheme.idleTimeoutMs,
        csrf,
        metadata: verified.record.metadata,
    };
    await scheme.store.create(record);
    const rotatedValue = await signSessionId(scheme, sessionId);
    ctx.session = Object.freeze({
        scheme: scheme.name,
        id: sessionId,
        issuedAt: Math.floor(record.createdAt / 1000),
        expiresAt: Math.floor(record.expiresAt / 1000),
        csrfToken: csrf,
        revoke: () => scheme.store.revoke(sessionId, nowMilliseconds(scheme.clock)),
    });
    let rotatedResult = result.cookie(
        scheme.cookieName,
        rotatedValue,
        sessionStoreCookieOptions(scheme, undefined, remainingAbsoluteLifetimeMs),
    );
    if (scheme.csrf.enabled) {
        rotatedResult = rotatedResult.cookie(scheme.csrf.cookieName, csrf, {
            path: scheme.path,
            secure: scheme.secure,
            httpOnly: false,
            sameSite: scheme.sameSite,
            ...(remainingAbsoluteLifetimeMs !== undefined
                ? { maxAgeSeconds: Math.max(0, Math.ceil(remainingAbsoluteLifetimeMs / 1000)) }
                : {}),
        });
    }
    return rotatedResult;
}

export function createAuthState() {
    return {
        schemes: [],
        schemesByName: new Map(),
        policies: new Map(),
        defaultScheme: undefined,
    };
}

function snapshotScheme(scheme) {
    if (scheme.kind === "jwtBearer") {
        return Object.freeze({
            kind: scheme.kind,
            name: scheme.name,
            algorithms: Object.freeze([...scheme.algorithms]),
            issuer: scheme.issuer,
            audience: scheme.audience,
        });
    }
    if (scheme.kind === "cookieSession") {
        return Object.freeze({
            kind: scheme.kind,
            name: scheme.name,
            cookie: scheme.cookieName,
            csrf: scheme.csrf.enabled,
            store: scheme.store === undefined ? "signed-cookie" : scheme.store.kind,
            idleTimeoutMs: scheme.idleTimeoutMs,
            absoluteTimeoutMs: scheme.absoluteTimeoutMs,
            rotation: scheme.rotation,
        });
    }
    return Object.freeze({
        kind: scheme.kind,
        name: scheme.name,
        header: scheme.header,
    });
}

export function snapshotAuthState(state) {
    return Object.freeze({
        schemes: Object.freeze(state.schemes.map(snapshotScheme)),
        defaultScheme: state.defaultScheme,
        policies: Object.freeze([...state.policies.keys()]),
    });
}

export function isAuthProviderDescriptor(value) {
    return isPlainObject(value) && value.__sloppyAuth === true;
}

function registerScheme(state, scheme) {
    if (state.schemesByName.has(scheme.name)) {
        throw new TypeError(`Sloppy auth scheme '${scheme.name}' is already registered.`);
    }
    state.schemes.push(scheme);
    state.schemesByName.set(scheme.name, scheme);
    state.defaultScheme ??= scheme.name;
}

export function registerAuthProvider(state, provider, config) {
    if (provider.kind === "configure") {
        if (provider.defaultScheme !== undefined) {
            state.defaultScheme = provider.defaultScheme;
        }
        const middleware = provider.providers.map((entry) => registerAuthProvider(state, entry, config));
        for (const name of provider.schemeNames) {
            if (!state.schemesByName.has(name)) {
                throw new TypeError(`Sloppy auth default or route scheme '${name}' is not configured.`);
            }
        }
        return (ctx, next) => invokeAuthMiddlewareList(ctx, next, middleware);
    }
    if (provider.kind === "jwtBearer") {
        const scheme = Object.freeze({
            kind: "jwtBearer",
            name: provider.name,
            principalScheme: provider.principalScheme,
            algorithms: provider.algorithms,
            keys: provider.keys.map((key) => Object.freeze({
                ...key,
                secret: key.secret === undefined ? undefined : secretString(key.secret, config, "JWT bearer key"),
            })),
            issuer: provider.issuer,
            audience: provider.audience,
            secret: provider.secret === undefined ? undefined : secretString(provider.secret, config, "JWT bearer"),
            clock: provider.clock,
            clockSkewSeconds: provider.clockSkewSeconds,
        });
        registerScheme(state, scheme);
        return (ctx, next) => jwtMiddleware(ctx, next, scheme);
    }
    if (provider.kind === "cookieSession") {
        const store = materializeSessionStore(provider.store);
        const scheme = Object.freeze({
            kind: "cookieSession",
            name: provider.name,
            principalScheme: provider.principalScheme,
            cookieName: provider.cookieName,
            secret: secretString(provider.secret, config, "cookie session"),
            secure: provider.secure,
            httpOnly: provider.httpOnly,
            sameSite: provider.sameSite,
            path: provider.path,
            maxAgeSeconds: provider.maxAgeSeconds,
            idleTimeoutMs: provider.idleTimeoutMs,
            absoluteTimeoutMs: provider.absoluteTimeoutMs,
            rotation: provider.rotation,
            clock: provider.clock,
            csrf: provider.csrf,
            store,
        });
        registerScheme(state, scheme);
        state.defaultSession = scheme;
        return (ctx, next) => cookieSessionMiddleware(ctx, next, scheme);
    }
    if (provider.kind === "apiKey") {
        const scheme = Object.freeze({
            kind: "apiKey",
            name: provider.name,
            principalScheme: provider.principalScheme,
            header: provider.header,
            authorizationScheme: provider.authorizationScheme,
            maxLength: provider.maxLength,
            keys: provider.keys,
            validate: provider.validate,
            config: config,
            configKey: provider.configKey,
            expectedKey: provider.configKey === undefined || provider.usesStaticConfigEquality !== true ? undefined : secretString(
                { __sloppyConfigReference: true, key: provider.configKey },
                config,
                "API key",
            ),
        });
        registerScheme(state, scheme);
        return (ctx, next) => apiKeyMiddleware(ctx, next, scheme);
    }
    throw new TypeError(`Sloppy Auth provider kind '${provider.kind}' is not supported.`);
}

function invokeAuthMiddlewareList(ctx, next, middleware) {
    let index = -1;
    function dispatch(nextIndex) {
        if (nextIndex <= index) {
            throw new Error("Sloppy auth middleware next() must not be called more than once.");
        }
        index = nextIndex;
        const current = middleware[nextIndex];
        return current === undefined ? next() : current(ctx, () => dispatch(nextIndex + 1));
    }
    return dispatch(0);
}

export function normalizeAuthRequirement(options = undefined) {
    if (options === undefined) {
        return Object.freeze({ required: true });
    }
    if (typeof options === "string" || Array.isArray(options)) {
        return Object.freeze({ required: true, schemes: stringArrayOption(options, "auth scheme") });
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy requireAuth options must be a plain object when provided.");
    }
    if (options.allowAnonymous === true) {
        return Object.freeze({ required: false, allowAnonymous: true });
    }
    const requirement = { required: true };
    if (options.scheme !== undefined) {
        requirement.schemes = stringArrayOption(options.scheme, "auth scheme");
    }
    if (options.schemes !== undefined) {
        requirement.schemes = stringArrayOption(options.schemes, "auth scheme");
    }
    if (options.role !== undefined) {
        requirement.roles = Object.freeze([stringOption(options.role, "auth role")]);
    }
    if (options.roles !== undefined) {
        if (!Array.isArray(options.roles)) {
            throw new TypeError("Sloppy requireAuth roles must be an array when provided.");
        }
        requirement.roles = Object.freeze(options.roles.map((role) => stringOption(role, "auth role")));
    }
    if (options.policy !== undefined) {
        requirement.policy = stringOption(options.policy, "auth policy");
    }
    if (options.claim !== undefined) {
        requirement.claims = Object.freeze([stringOption(options.claim, "auth claim")]);
    }
    if (options.scope !== undefined) {
        requirement.scopes = stringArrayOption(options.scope, "auth scope");
    }
    if (options.scopes !== undefined) {
        requirement.scopes = stringArrayOption(options.scopes, "auth scope");
    }
    return Object.freeze(requirement);
}

export function snapshotAuthRequirement(requirement) {
    if (requirement === undefined || requirement === null) {
        return undefined;
    }
    return Object.freeze({
        required: requirement.required === true,
        ...(requirement.allowAnonymous === true ? { allowAnonymous: true } : {}),
        ...(requirement.schemes === undefined ? {} : { schemes: Object.freeze([...requirement.schemes]) }),
        ...(requirement.scopes === undefined ? {} : { scopes: Object.freeze([...requirement.scopes]) }),
        ...(requirement.roles === undefined ? {} : { roles: Object.freeze([...requirement.roles]) }),
        ...(requirement.policy === undefined ? {} : { policy: requirement.policy }),
        ...(requirement.claims === undefined ? {} : { claims: Object.freeze([...requirement.claims]) }),
        ...(requirement.scopes === undefined ? {} : { scopes: Object.freeze([...requirement.scopes]) }),
    });
}

function policyFailed(user, kind, values, ctx, resource) {
    if (kind === "authenticated") {
        return user?.authenticated !== true;
    }
    if (kind === "scope") {
        return !values.every((scope) => user?.hasScope(scope) === true);
    }
    if (kind === "role") {
        return !values.some((role) => user?.hasRole(role) === true);
    }
    if (kind === "claim") {
        return !user?.hasClaim(values.name, values.value);
    }
    if (kind === "custom") {
        return values(user, ctx, resource) !== true;
    }
    return true;
}

function authPolicy(configure) {
    if (typeof configure !== "function") {
        throw new TypeError("Sloppy Auth.policy requires a builder callback.");
    }
    const requirements = [];
    const builder = Object.freeze({
        requireAuthenticated() {
            requirements.push(Object.freeze({ kind: "authenticated" }));
            return builder;
        },
        requireScope(...scopes) {
            requirements.push(Object.freeze({ kind: "scope", values: stringArrayOption(scopes, "auth policy scope") }));
            return builder;
        },
        requireRole(...roles) {
            requirements.push(Object.freeze({ kind: "role", values: stringArrayOption(roles, "auth policy role") }));
            return builder;
        },
        requireClaim(name, value = undefined) {
            requirements.push(Object.freeze({
                kind: "claim",
                values: Object.freeze({ name: stringOption(name, "auth policy claim"), value }),
            }));
            return builder;
        },
        custom(predicate) {
            if (typeof predicate !== "function") {
                throw new TypeError("Sloppy Auth.policy custom predicate must be a function.");
            }
            requirements.push(Object.freeze({ kind: "custom", values: predicate }));
            return builder;
        },
    });
    configure(builder);
    return Object.freeze({
        __sloppyPolicy: true,
        requirements: Object.freeze([...requirements]),
        async evaluate(user, ctx, resource = undefined) {
            for (const requirement of requirements) {
                if (requirement.kind === "custom") {
                    if (await requirement.values(user, ctx, resource) !== true) {
                        return false;
                    }
                    continue;
                }
                if (policyFailed(user, requirement.kind, requirement.values, ctx, resource)) {
                    return false;
                }
            }
            return true;
        },
    });
}

export function normalizeAuthPolicy(policy) {
    if (typeof policy === "function") {
        return policy;
    }
    if (isPlainObject(policy) && policy.__sloppyPolicy === true && typeof policy.evaluate === "function") {
        return (user, ctx, resource = undefined) => policy.evaluate(user, ctx, resource);
    }
    throw new TypeError("Sloppy auth policy must be a function or Auth.policy descriptor.");
}

export function authorizePolicy(state, name, user, ctx, resource = undefined) {
    const policy = state?.policies?.get(name);
    if (typeof policy !== "function") {
        return forbidden();
    }
    const result = policy(user, ctx, resource);
    if (result !== null && typeof result === "object" && typeof result.then === "function") {
        return Promise.resolve(result).then((ok) => ok === true ? undefined : forbidden());
    }
    return result === true ? undefined : forbidden();
}

export function authorizeRoute(ctx, requirement, state) {
    if (ctx.user === undefined || ctx.user === null) {
        ctx.user = anonymousUser();
    }
    if (requirement === undefined || requirement === null || requirement.required !== true) {
        return undefined;
    }
    if (ctx.user.authenticated !== true) {
        return unauthorized();
    }
    if (Array.isArray(requirement.schemes) && requirement.schemes.length > 0 &&
        !requirement.schemes.includes(ctx.user.scheme) &&
        !requirement.schemes.includes(ctx.user.authScheme))
    {
        return unauthorized();
    }
    if ((!Array.isArray(requirement.schemes) || requirement.schemes.length === 0) &&
        typeof state?.defaultScheme === "string" &&
        ctx.user.scheme !== state.defaultScheme &&
        ctx.user.authScheme !== state.defaultScheme)
    {
        return unauthorized();
    }
    if (Array.isArray(requirement.scopes) && requirement.scopes.length > 0) {
        const matched = requirement.scopes.every((scope) => ctx.user.hasScope(scope));
        if (!matched) {
            return forbidden();
        }
    }
    if (Array.isArray(requirement.roles) && requirement.roles.length > 0) {
        const matched = requirement.roles.some((role) => ctx.user.hasRole(role));
        if (!matched) {
            return forbidden();
        }
    }
    if (Array.isArray(requirement.claims) && requirement.claims.length > 0) {
        const matched = requirement.claims.every((claim) => ctx.user.hasClaim(claim));
        if (!matched) {
            return forbidden();
        }
    }
    if (Array.isArray(requirement.scopes) && requirement.scopes.length > 0) {
        const matched = requirement.scopes.every((scope) => ctx.user.hasScope(scope));
        if (!matched) {
            return forbidden();
        }
    }
    if (requirement.policy !== undefined) {
        return authorizePolicy(state, requirement.policy, ctx.user, ctx);
    }
    return undefined;
}

function jwtBearer(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Auth.jwtBearer options must be a plain object.");
    }
    if (options.jwksUri !== undefined || options.jwksUrl !== undefined || options.authority !== undefined) {
        throw new TypeError(
            "Sloppy Auth.jwtBearer remote JWKS discovery requires runtime HTTP client integration; use static jwks.keys or keys.",
        );
    }
    const algorithms = stringArrayOption(options.algorithms ?? options.algorithm ?? "HS256", "JWT algorithm");
    const keys = normalizeJwtKeys(options.keys ?? options.jwks?.keys);
    if (options.secret === undefined && keys.length === 0) {
        throw new TypeError("Sloppy Auth.jwtBearer requires secret or static keys.");
    }
    return Object.freeze({
        __sloppyAuth: true,
        kind: "jwtBearer",
        name: stringOption(options.name ?? JWT_SCHEME, "JWT auth scheme name"),
        principalScheme: stringOption(options.name ?? "jwtBearer", "JWT auth principal scheme"),
        algorithms,
        keys,
        issuer: stringOption(options.issuer, "JWT issuer", false),
        audience: stringOption(options.audience, "JWT audience", false),
        secret: options.secret,
        clock: options.clock,
        clockSkewSeconds: integerOption(options.clockSkewSeconds ?? options.clockSkew, "JWT clockSkewSeconds", 0),
    });
}

function normalizeJwtKeys(keys) {
    if (keys === undefined) {
        return Object.freeze([]);
    }
    if (!Array.isArray(keys)) {
        throw new TypeError("Sloppy Auth.jwtBearer keys must be an array when provided.");
    }
    return Object.freeze(keys.map((key) => {
        if (!isPlainObject(key)) {
            throw new TypeError("Sloppy Auth.jwtBearer key entries must be plain objects.");
        }
        const algorithm = stringOption(key.alg ?? key.algorithm ?? "HS256", "JWT key algorithm");
        return Object.freeze({
            kid: stringOption(key.kid, "JWT key kid", false),
            algorithm,
            secret: key.secret,
            jwk: key.kty === undefined ? key.jwk : { ...key },
            default: key.default === true,
        });
    }));
}

function normalizeSameSiteOption(value) {
    if (value === undefined) {
        return "lax";
    }
    if (typeof value !== "string") {
        throw new TypeError("Sloppy Auth.cookieSession sameSite must be lax, strict, or none.");
    }
    const lowered = value.toLowerCase();
    if (lowered !== "lax" && lowered !== "strict" && lowered !== "none") {
        throw new TypeError("Sloppy Auth.cookieSession sameSite must be lax, strict, or none.");
    }
    return lowered;
}

function normalizeTimeoutMs(value, subject, defaultValue = undefined) {
    return optionalPositiveInteger(value, `Sloppy ${subject} must be a positive integer number of milliseconds.`, defaultValue);
}

function normalizeSessionStoreDescriptor(value) {
    if (value === undefined) {
        return undefined;
    }
    if (isPlainObject(value) && value.__sloppySessionStore === true) {
        return value;
    }
    if (typeof value === "object" && value !== null) {
        for (const method of ["create", "load", "touch", "revoke", "cleanup"]) {
            if (typeof value[method] !== "function") {
                throw new TypeError(`Sloppy Auth session store object must provide ${method}().`);
            }
        }
        return Object.freeze({
            __sloppySessionStore: true,
            kind: "custom",
            store: value,
        });
    }
    throw new TypeError("Sloppy Auth.cookieSession store must be a session store descriptor or store object.");
}

function cloneSessionRecord(record) {
    if (record === undefined) {
        return undefined;
    }
    return Object.freeze({
        id: record.id,
        claims: Object.freeze({ ...record.claims }),
        createdAt: record.createdAt,
        lastSeenAt: record.lastSeenAt,
        expiresAt: record.expiresAt,
        idleExpiresAt: record.idleExpiresAt,
        revokedAt: record.revokedAt,
        csrf: record.csrf,
        metadata: record.metadata === undefined ? undefined : Object.freeze({ ...record.metadata }),
    });
}

function createMemorySessionStore(options = undefined) {
    if (options !== undefined && !isPlainObject(options)) {
        throw new TypeError("Sloppy Auth.sessionStore.memory options must be a plain object.");
    }
    const maxEntries = integerOption(options?.maxEntries, "memory session store maxEntries", 4096);
    if (maxEntries <= 0) {
        throw new TypeError("Sloppy memory session store maxEntries must be positive.");
    }
    const sessions = new Map();
    function trim() {
        while (sessions.size > maxEntries) {
            const oldest = sessions.keys().next().value;
            sessions.delete(oldest);
        }
    }
    return Object.freeze({
        __sloppySessionStore: true,
        kind: "memory",
        async create(record) {
            sessions.set(record.id, cloneSessionRecord(record));
            trim();
            return cloneSessionRecord(sessions.get(record.id));
        },
        async load(id) {
            return cloneSessionRecord(sessions.get(id));
        },
        async touch(id, lastSeenAt, idleExpiresAt) {
            const record = sessions.get(id);
            if (record === undefined || record.revokedAt !== undefined) {
                return undefined;
            }
            const next = cloneSessionRecord({ ...record, lastSeenAt, idleExpiresAt });
            sessions.set(id, next);
            return next;
        },
        async revoke(id, revokedAt = Date.now()) {
            const record = sessions.get(id);
            if (record === undefined) {
                return false;
            }
            sessions.set(id, cloneSessionRecord({ ...record, revokedAt }));
            return true;
        },
        async cleanup(now = Date.now()) {
            let count = 0;
            for (const [id, record] of sessions) {
                if (sessionRecordExpired(record, now)) {
                    sessions.delete(id);
                    count += 1;
                }
            }
            return count;
        },
        __debug() {
            return Object.freeze({ kind: "memory", count: sessions.size, maxEntries });
        },
    });
}

function providerKindFromDb(db, fallback) {
    const kind = typeof db?.__debug === "function" ? db.__debug().kind : undefined;
    if (kind === "sqlite-connection") {
        return "sqlite";
    }
    if (kind === "postgres-connection") {
        return "postgres";
    }
    if (kind === "sqlserver-connection") {
        return "sqlserver";
    }
    return fallback;
}

function placeholders(kind, count) {
    return Array.from({ length: count }, (_entry, index) => {
        if (kind === "postgres") {
            return `$${index + 1}`;
        }
        if (kind === "sqlserver") {
            return `@p${index + 1}`;
        }
        return "?";
    });
}

function normalizeSessionRow(row) {
    if (row === undefined || row === null) {
        return undefined;
    }
    function finiteNumber(value) {
        if (typeof value === "string" && value.trim().length === 0) {
            return undefined;
        }
        const number = Number(value);
        return Number.isFinite(number) ? number : undefined;
    }
    function optionalFiniteNumber(...values) {
        const value = values.find((entry) => entry !== undefined && entry !== null);
        if (value === undefined) {
            return { valid: true, value: undefined };
        }
        const number = finiteNumber(value);
        return number === undefined
            ? { valid: false, value: undefined }
            : { valid: true, value: number };
    }
    const claimsText = row.claims_json ?? row.claimsJson ?? row.claims;
    const metadataText = row.metadata_json ?? row.metadataJson ?? row.metadata;
    let claimsValue = claimsText;
    let metadataValue = metadataText;
    try {
        if (typeof claimsText === "string") {
            claimsValue = JSON.parse(claimsText);
        }
        if (typeof metadataText === "string") {
            metadataValue = JSON.parse(metadataText);
        }
    } catch {
        return undefined;
    }
    const createdAt = finiteNumber(row.created_at_ms ?? row.createdAt ?? row.created_at);
    const lastSeenAt = finiteNumber(row.last_seen_at_ms ?? row.lastSeenAt ?? row.last_seen_at);
    const expiresAt = optionalFiniteNumber(row.expires_at_ms, row.expiresAt, row.expires_at);
    const idleExpiresAt = optionalFiniteNumber(row.idle_expires_at_ms, row.idleExpiresAt, row.idle_expires_at);
    const revokedAt = optionalFiniteNumber(row.revoked_at_ms, row.revokedAt, row.revoked_at);
    const csrf = row.csrf === null || row.csrf === undefined ? undefined : row.csrf;
    if (
        typeof row.id !== "string" ||
        row.id.length === 0 ||
        !isPlainObject(claimsValue) ||
        createdAt === undefined ||
        lastSeenAt === undefined ||
        expiresAt.valid !== true ||
        idleExpiresAt.valid !== true ||
        revokedAt.valid !== true ||
        (csrf !== undefined && typeof csrf !== "string") ||
        (metadataValue !== null && metadataValue !== undefined && !isPlainObject(metadataValue))
    ) {
        return undefined;
    }
    return cloneSessionRecord({
        id: row.id,
        claims: claimsValue,
        createdAt,
        lastSeenAt,
        expiresAt: expiresAt.value,
        idleExpiresAt: idleExpiresAt.value,
        revokedAt: revokedAt.value,
        csrf,
        metadata: metadataValue === null || metadataValue === undefined
            ? undefined
            : metadataValue,
    });
}

function createDataProviderSessionStore(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Auth.sessionStore.dataProvider options must be a plain object.");
    }
    const db = options.db ?? options.connection;
    if (db === undefined || typeof db.exec !== "function" || typeof db.queryOne !== "function") {
        throw new TypeError("Sloppy Auth.sessionStore.dataProvider requires an opened data connection with exec() and queryOne().");
    }
    const kind = providerKindFromDb(db, stringOption(options.provider, "session store provider", false) ?? "sqlite");
    if (!["sqlite", "postgres", "sqlserver"].includes(kind)) {
        throw new TypeError("Sloppy Auth.sessionStore.dataProvider provider must be sqlite, postgres, or sqlserver.");
    }
    let ensured = false;
    async function ensure() {
        if (ensured) {
            return;
        }
        if (kind === "sqlserver") {
            await db.exec(`IF OBJECT_ID(N'dbo.sloppy_auth_sessions', N'U') IS NULL
BEGIN
CREATE TABLE sloppy_auth_sessions (
id NVARCHAR(255) NOT NULL PRIMARY KEY,
subject NVARCHAR(255) NOT NULL,
claims_json NVARCHAR(MAX) NOT NULL,
created_at_ms BIGINT NOT NULL,
last_seen_at_ms BIGINT NOT NULL,
expires_at_ms BIGINT NULL,
idle_expires_at_ms BIGINT NULL,
revoked_at_ms BIGINT NULL,
csrf NVARCHAR(255) NULL,
metadata_json NVARCHAR(MAX) NULL
)
END`, []);
        } else {
            await db.exec(`CREATE TABLE IF NOT EXISTS sloppy_auth_sessions (
id TEXT PRIMARY KEY,
subject TEXT NOT NULL,
claims_json TEXT NOT NULL,
created_at_ms INTEGER NOT NULL,
last_seen_at_ms INTEGER NOT NULL,
expires_at_ms INTEGER NULL,
idle_expires_at_ms INTEGER NULL,
revoked_at_ms INTEGER NULL,
csrf TEXT NULL,
metadata_json TEXT NULL
)`, []);
        }
        ensured = true;
    }
    const store = Object.freeze({
        __sloppySessionStore: true,
        kind: `dataProvider:${kind}`,
        async create(record) {
            await ensure();
            const p = placeholders(kind, 10);
            await db.exec(
                `INSERT INTO sloppy_auth_sessions (id, subject, claims_json, created_at_ms, last_seen_at_ms, expires_at_ms, idle_expires_at_ms, revoked_at_ms, csrf, metadata_json) VALUES (${p.join(", ")})`,
                [
                    record.id,
                    record.claims.sub ?? "",
                    JSON.stringify(record.claims),
                    record.createdAt,
                    record.lastSeenAt,
                    record.expiresAt ?? null,
                    record.idleExpiresAt ?? null,
                    record.revokedAt ?? null,
                    record.csrf ?? null,
                    record.metadata === undefined ? null : JSON.stringify(record.metadata),
                ],
            );
            return cloneSessionRecord(record);
        },
        async load(id) {
            await ensure();
            const p = placeholders(kind, 1);
            return normalizeSessionRow(await db.queryOne(`SELECT * FROM sloppy_auth_sessions WHERE id = ${p[0]}`, [id]));
        },
        async touch(id, lastSeenAt, idleExpiresAt) {
            await ensure();
            const p = placeholders(kind, 3);
            await db.exec(
                `UPDATE sloppy_auth_sessions SET last_seen_at_ms = ${p[0]}, idle_expires_at_ms = ${p[1]} WHERE id = ${p[2]} AND revoked_at_ms IS NULL`,
                [lastSeenAt, idleExpiresAt ?? null, id],
            );
            return store.load(id);
        },
        async revoke(id, revokedAt = Date.now()) {
            await ensure();
            const p = placeholders(kind, 2);
            await db.exec(`UPDATE sloppy_auth_sessions SET revoked_at_ms = ${p[0]} WHERE id = ${p[1]}`, [revokedAt, id]);
            return true;
        },
        async cleanup(now = Date.now()) {
            await ensure();
            const p = placeholders(kind, 1);
            await db.exec(
                `DELETE FROM sloppy_auth_sessions WHERE revoked_at_ms IS NOT NULL OR expires_at_ms <= ${p[0]} OR idle_expires_at_ms <= ${p[0]}`,
                [now],
            );
            return undefined;
        },
    });
    return store;
}

function materializeSessionStore(descriptor) {
    if (descriptor === undefined) {
        return undefined;
    }
    if (descriptor.kind === "memory") {
        return createMemorySessionStore(descriptor.options);
    }
    if (descriptor.kind === "dataProvider") {
        return createDataProviderSessionStore(descriptor.options);
    }
    if (descriptor.kind === "custom") {
        return descriptor.store;
    }
    throw new TypeError(`Sloppy Auth session store kind '${descriptor.kind}' is not supported.`);
}

function cookieSession(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Auth.cookieSession options must be a plain object.");
    }
    const store = normalizeSessionStoreDescriptor(options.store);
    const idleTimeoutMs = normalizeTimeoutMs(
        options.idleTimeoutMs ?? options.idleTimeout,
        "Auth.cookieSession idleTimeoutMs",
        store === undefined ? undefined : DEFAULT_SESSION_IDLE_TIMEOUT_MS,
    );
    const absoluteTimeoutMs = normalizeTimeoutMs(
        options.absoluteTimeoutMs ?? options.absoluteTimeout,
        "Auth.cookieSession absoluteTimeoutMs",
        store === undefined ? undefined : DEFAULT_SESSION_ABSOLUTE_TIMEOUT_MS,
    );
    const name = options.name ?? DEFAULT_SESSION_COOKIE;
    requireHttpToken(name, "Sloppy Auth.cookieSession name must be a safe HTTP token.");
    const secure = booleanOption(options.secure, "Auth.cookieSession secure", true);
    const sameSite = normalizeSameSiteOption(options.sameSite);
    if (sameSite === "none" && secure !== true) {
        throw new TypeError("Sloppy Auth.cookieSession sameSite none requires secure cookies.");
    }
    const path = stringOption(options.path ?? "/", "Auth.cookieSession path");
    const maxAgeSeconds = integerOption(
        options.maxAgeSeconds ?? options.maxAge ?? (store === undefined ? DEFAULT_SIGNED_SESSION_MAX_AGE_SECONDS : undefined),
        "Auth.cookieSession maxAgeSeconds",
    );
    const csrf = normalizeCsrfOptions(options.csrf);
    if (csrf.enabled && csrf.cookieName.startsWith("__Host-") && (secure !== true || path !== "/")) {
        throw new TypeError("Sloppy Auth.cookieSession __Host- CSRF cookies require secure true and path '/'.");
    }
    return Object.freeze({
        __sloppyAuth: true,
        kind: "cookieSession",
        name: stringOption(options.scheme ?? options.schemeName ?? COOKIE_SESSION_SCHEME, "cookie session auth scheme name"),
        principalScheme: stringOption(options.scheme ?? options.schemeName ?? "cookieSession", "cookie session auth principal scheme"),
        cookieName: name,
        secret: options.secret,
        secure,
        httpOnly: booleanOption(options.httpOnly, "Auth.cookieSession httpOnly", true),
        sameSite,
        path,
        maxAgeSeconds,
        idleTimeoutMs,
        absoluteTimeoutMs,
        rotation: store === undefined ? false : booleanOption(options.rotation ?? options.rotate, "Auth.cookieSession rotation", false),
        clock: options.clock,
        csrf,
        store,
    });
}

function normalizeCsrfOptions(options) {
    if (options === undefined || options === false) {
        return Object.freeze({ enabled: false, header: DEFAULT_CSRF_HEADER, cookieName: DEFAULT_CSRF_COOKIE });
    }
    if (options === true) {
        return Object.freeze({ enabled: true, header: DEFAULT_CSRF_HEADER, cookieName: DEFAULT_CSRF_COOKIE });
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Auth CSRF options must be a plain object, true, or false.");
    }
    const header = (options.header ?? DEFAULT_CSRF_HEADER).toLowerCase();
    validateHeaderName(header, "CSRF");
    const cookieName = stringOption(options.cookieName ?? DEFAULT_CSRF_COOKIE, "CSRF cookie name");
    requireHttpToken(cookieName, "Sloppy Auth CSRF cookie name must be a safe HTTP token.");
    return Object.freeze({
        enabled: true,
        header,
        cookieName,
    });
}

function findSessionScheme(ctx) {
    const auth = ctx?.__sloppyHost?.auth;
    const scheme = auth?.state?.defaultSession ?? auth?.defaultSession ??
        (Array.isArray(auth?.schemes)
            ? auth.schemes.find((entry) => entry.kind === "cookieSession")
            : undefined);
    if (scheme === undefined) {
        throw new TypeError("Sloppy Auth.signIn/signOut requires Auth.cookieSession middleware.");
    }
    return scheme;
}

async function signIn(ctx, claims, options = undefined) {
    const scheme = findSessionScheme(ctx);
    const sessionClaims = normalizeSessionClaims(claims);
    const currentMs = nowMilliseconds(scheme.clock);
    const current = Math.floor(currentMs / 1000);
    const csrf = scheme.csrf.enabled ? Random.token(32) : undefined;
    if (scheme.store !== undefined) {
        const cookieOptions = sessionStoreCookieOptions(scheme, options, scheme.absoluteTimeoutMs);
        const absoluteLifetimeMs = cookieOptions.maxAgeSeconds === undefined
            ? scheme.absoluteTimeoutMs
            : Math.min(scheme.absoluteTimeoutMs, Math.max(0, cookieOptions.maxAgeSeconds * 1000));
        const sessionId = Random.token(32);
        const record = {
            id: sessionId,
            claims: sessionClaims,
            createdAt: currentMs,
            lastSeenAt: currentMs,
            expiresAt: currentMs + absoluteLifetimeMs,
            idleExpiresAt: scheme.idleTimeoutMs === undefined ? undefined : currentMs + scheme.idleTimeoutMs,
            csrf,
            metadata: isPlainObject(options?.metadata) ? { ...options.metadata } : undefined,
        };
        await scheme.store.create(record);
        const value = await signSessionId(scheme, sessionId);
        ctx.user = userFromClaims(sessionClaims, scheme.principalScheme, scheme.name);
        ctx.session = Object.freeze({
            scheme: scheme.name,
            id: sessionId,
            issuedAt: current,
            expiresAt: Math.floor(record.expiresAt / 1000),
            csrfToken: csrf,
            revoke: () => scheme.store.revoke(sessionId, nowMilliseconds(scheme.clock)),
        });
        let result = Results.ok({ ok: true }).cookie(
            scheme.cookieName,
            value,
            cookieOptions,
        );
        if (scheme.csrf.enabled) {
            result = result.cookie(scheme.csrf.cookieName, csrf, {
                path: scheme.path,
                secure: scheme.secure,
                sameSite: scheme.sameSite,
            });
        }
        return result;
    }
    const cookieOptions = sessionCookieOptions(scheme, options);
    const payload = {
        iat: current,
        ...(cookieOptions.maxAgeSeconds === undefined ? {} : { exp: current + cookieOptions.maxAgeSeconds }),
        claims: sessionClaims,
    };
    if (csrf !== undefined) {
        payload.csrf = csrf;
    }
    const value = await signSessionPayload(scheme, payload);
    ctx.user = userFromClaims(sessionClaims, scheme.principalScheme, scheme.name);
    let result = Results.ok({ ok: true }).cookie(scheme.cookieName, value, cookieOptions);
    if (scheme.csrf.enabled) {
        result = result.cookie(scheme.csrf.cookieName, payload.csrf, {
            path: scheme.path,
            secure: scheme.secure,
            sameSite: scheme.sameSite,
        });
    }
    return result;
}

async function signOut(ctx, options = undefined) {
    const scheme = findSessionScheme(ctx);
    const sessionId = typeof ctx?.session?.id === "string" ? ctx.session.id : undefined;
    if (sessionId !== undefined && scheme.store !== undefined) {
        await scheme.store.revoke(sessionId, nowMilliseconds(scheme.clock));
    }
    ctx.user = anonymousUser();
    let result = Results.status(204).cookie(
        scheme.cookieName,
        "",
        sessionStoreCookieOptions(scheme, { ...options, maxAgeSeconds: 0, expires: new Date(0) }),
    );
    if (scheme.csrf.enabled) {
        result = result.cookie(scheme.csrf.cookieName, "", {
            path: scheme.path,
            secure: scheme.secure,
            sameSite: scheme.sameSite,
            maxAgeSeconds: 0,
            expires: new Date(0),
        });
    }
    return result;
}

function apiKey(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Auth.apiKey options must be a plain object.");
    }
    const header = options.header ?? "x-api-key";
    validateHeaderName(header, "API key");
    const keys = normalizeApiKeys(options.keys);
    const configKey = stringOption(options.configKey, "API key configKey", false)
        ?? configRequiredKeysFromFunction(options.validate)?.[0];
    if (typeof options.validate !== "function" && configKey === undefined && keys.length === 0) {
        throw new TypeError("Sloppy Auth.apiKey requires validate, configKey, or keys.");
    }
    if (options.validate !== undefined && typeof options.validate !== "function") {
        throw new TypeError("Sloppy Auth.apiKey validate must be a function.");
    }
    return Object.freeze({
        __sloppyAuth: true,
        kind: "apiKey",
        name: stringOption(options.name ?? API_KEY_SCHEME, "API key auth scheme name"),
        principalScheme: stringOption(options.name ?? "apiKey", "API key auth principal scheme"),
        header: header.toLowerCase(),
        authorizationScheme: stringOption(options.authorizationScheme, "API key Authorization scheme", false),
        maxLength: integerOption(options.maxLength, "API key maxLength", 4096),
        keys,
        validate: options.validate,
        configKey,
        usesStaticConfigEquality: typeof options.validate !== "function" ||
            apiKeyValidatorUsesOnlyConfigKey(options.validate, configKey),
    });
}

function normalizeApiKeys(keys) {
    if (keys === undefined) {
        return Object.freeze([]);
    }
    if (!Array.isArray(keys)) {
        throw new TypeError("Sloppy Auth.apiKey keys must be an array.");
    }
    return Object.freeze(keys.map((entry) => {
        if (!isPlainObject(entry)) {
            throw new TypeError("Sloppy Auth.apiKey key entries must be plain objects.");
        }
        const id = stringOption(entry.id, "API key id");
        const key = stringOption(entry.key, "API key value", false);
        const hash = stringOption(entry.hash, "API key hash", false);
        if ((key === undefined) === (hash === undefined)) {
            throw new TypeError("Sloppy Auth.apiKey key entries require exactly one of key or hash.");
        }
        return Object.freeze({
            id,
            key,
            hash,
            scopes: stringArrayOption(entry.scopes, "API key scope"),
            roles: stringArrayOption(entry.roles, "API key role"),
            claims: entry.claims === undefined ? undefined : Object.freeze({ ...entry.claims }),
        });
    }));
}

function configure(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Auth.configure options must be a plain object.");
    }
    if (!isPlainObject(options.schemes)) {
        throw new TypeError("Sloppy Auth.configure schemes must be an object.");
    }
    const providers = [];
    const schemeNames = [];
    for (const [name, provider] of Object.entries(options.schemes)) {
        if (!isAuthProviderDescriptor(provider) || provider.kind === "configure") {
            throw new TypeError("Sloppy Auth.configure schemes must contain Auth provider descriptors.");
        }
        providers.push(Object.freeze({ ...provider, name, principalScheme: name }));
        schemeNames.push(name);
    }
    const defaultScheme = stringOption(options.defaultScheme, "default auth scheme", false);
    if (defaultScheme !== undefined && !schemeNames.includes(defaultScheme)) {
        throw new TypeError(`Sloppy Auth.configure default scheme '${defaultScheme}' is not configured.`);
    }
    return Object.freeze({
        __sloppyAuth: true,
        kind: "configure",
        defaultScheme,
        schemeNames: Object.freeze(schemeNames),
        providers: Object.freeze(providers),
    });
}

const password = Object.freeze({
    hash(passwordValue, options = undefined) {
        return Password.hash(passwordValue, options);
    },
    verify(encodedHash, passwordValue) {
        return Password.verify(passwordValue, encodedHash);
    },
    needsRehash(encodedHash, options = undefined) {
        return Password.needsRehash(encodedHash, options);
    },
});

const sessionStore = Object.freeze({
    memory(options = undefined) {
        if (options !== undefined && !isPlainObject(options)) {
            throw new TypeError("Sloppy Auth.sessionStore.memory options must be a plain object.");
        }
        return Object.freeze({
            __sloppySessionStore: true,
            kind: "memory",
            options: options === undefined ? undefined : Object.freeze({ ...options }),
        });
    },
    dataProvider(options) {
        return Object.freeze({
            __sloppySessionStore: true,
            kind: "dataProvider",
            options: Object.freeze({ ...options }),
        });
    },
});

function configRequiredKeysFromFunction(fn) {
    if (typeof fn !== "function") {
        return undefined;
    }
    // This only recognizes direct literal Config.required("...") calls. When the
    // function source cannot be parsed confidently, leave metadata undefined.
    const keys = [];
    let source = Function.prototype.toString.call(fn);
    while (source.length !== 0) {
        const index = source.indexOf("Config.required");
        if (index < 0) {
            break;
        }
        source = source.slice(index + "Config.required".length);
        const open = source.indexOf("(");
        if (open < 0) {
            return undefined;
        }
        source = source.slice(open + 1).trimStart();
        const quote = source[0];
        if (quote !== "\"" && quote !== "'") {
            return undefined;
        }
        const end = source.slice(1).indexOf(quote);
        if (end < 0) {
            return undefined;
        }
        keys.push(source.slice(1, end + 1));
        source = source.slice(end + 2);
    }
    return keys;
}

function apiKeyValidatorUsesOnlyConfigKey(validate, configKey) {
    if (configKey === undefined || typeof validate !== "function") {
        return false;
    }
    const source = Function.prototype.toString.call(validate).trim();
    const escaped = configKey.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
    const parameter = "([A-Za-z_$][0-9A-Za-z_$]*)";
    return new RegExp(`^\\(?\\s*${parameter}\\s*\\)?\\s*=>\\s*\\1\\s*===\\s*Config\\.required\\(["']${escaped}["']\\)\\s*$`, "u").test(source) ||
        new RegExp(`^\\(?\\s*${parameter}\\s*\\)?\\s*=>\\s*Config\\.required\\(["']${escaped}["']\\)\\s*===\\s*\\1\\s*$`, "u").test(source);
}

export const Auth = Object.freeze({
    configure,
    policy: authPolicy,
    jwtBearer,
    apiKey,
    cookieSession,
    signIn,
    signOut,
    sessionStore,
    password,
    constantTimeEquals: constantTimeStringEquals,
});
