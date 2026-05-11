import { Base64Url, Text } from "./codec.js";
import { Hmac } from "./crypto.js";
import { Results } from "./results.js";

const AUTH_HEADER = "authorization";
const JWT_SCHEME = "bearerAuth";
const API_KEY_SCHEME = "apiKeyAuth";
const COOKIE_SESSION_SCHEME = "cookieSessionAuth";
const DEFAULT_SESSION_COOKIE = "sloppy.session";
const HEADER_TOKEN_PATTERN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/u;
const STANDARD_JWT_CLAIMS = new Set(["iss", "sub", "aud", "exp", "nbf", "iat", "jti", "name", "role", "roles"]);

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }
    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function validateHeaderName(name, subject) {
    if (typeof name !== "string" || !HEADER_TOKEN_PATTERN.test(name)) {
        throw new TypeError(`Sloppy ${subject} header must be an HTTP token string.`);
    }
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
    if (typeof value !== "string" || value.length === 0) {
        throw new TypeError(`Sloppy ${subject} must be a non-empty string.`);
    }
    return value;
}

function booleanOption(value, subject, defaultValue) {
    if (value === undefined) {
        return defaultValue;
    }
    if (typeof value !== "boolean") {
        throw new TypeError(`Sloppy ${subject} must be a boolean.`);
    }
    return value;
}

function integerOption(value, subject, defaultValue = undefined) {
    if (value === undefined) {
        return defaultValue;
    }
    if (!Number.isInteger(value)) {
        throw new TypeError(`Sloppy ${subject} must be an integer.`);
    }
    return value;
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

function userFromClaims(claims, scheme) {
    const roles = Array.isArray(claims.roles)
        ? claims.roles.filter((role) => typeof role === "string")
        : (typeof claims.role === "string" ? [claims.role] : []);
    const allClaims = Object.freeze({ ...claims });
    const user = {
        authenticated: true,
        sub: typeof claims.sub === "string" ? claims.sub : "",
        name: typeof claims.name === "string" ? claims.name : "",
        roles: Object.freeze([...new Set(roles)]),
        claims: allClaims,
        scheme,
        hasRole(role) {
            return typeof role === "string" && user.roles.includes(role);
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

export function anonymousUser() {
    const user = {
        authenticated: false,
        sub: "",
        name: "",
        roles: Object.freeze([]),
        claims: Object.freeze({}),
        scheme: "",
        hasRole() {
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
    const match = value.match(/^Bearer\s+(.+)$/iu);
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

function audienceMatches(expected, actual) {
    if (expected === undefined) {
        return true;
    }
    if (Array.isArray(actual)) {
        return actual.includes(expected);
    }
    return actual === expected;
}

async function verifyJwt(token, scheme) {
    const parts = String(token).split(".");
    if (parts.length !== 3 || parts.some((part) => part.length === 0)) {
        throw new Error("SLOPPY_E_AUTH_INVALID_TOKEN: JWT must have header, payload, and signature.");
    }
    const header = jsonFromBase64Url(parts[0], "JWT header");
    if (header.alg === "none") {
        throw new Error("SLOPPY_E_AUTH_INVALID_TOKEN: JWT alg none is not supported.");
    }
    if (header.alg !== "HS256") {
        throw new Error("SLOPPY_E_AUTH_INVALID_TOKEN: only HS256 JWT signatures are supported.");
    }
    const expected = await Hmac.sha256(scheme.secret, `${parts[0]}.${parts[1]}`);
    const actual = Base64Url.decode(parts[2], { padding: "optional" });
    if (!constantTimeBytesEquals(expected, actual)) {
        throw new Error("SLOPPY_E_AUTH_INVALID_TOKEN: JWT signature is invalid.");
    }
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
    return userFromClaims(claims, "jwtBearer");
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
        return userFromClaims({ sub: "api-key" }, scheme.name);
    }
    if (isPlainObject(result)) {
        return userFromClaims(result, scheme.name);
    }
    return undefined;
}

async function apiKeyMiddleware(ctx, next, scheme) {
    const key = headerValue(ctx, scheme.header);
    if (key === undefined) {
        return next();
    }
    if (scheme.expectedKey !== undefined) {
        if (!constantTimeStringEquals(key, scheme.expectedKey)) {
            return unauthorized(`${scheme.header}`);
        }
        ctx.user = userFromClaims({ sub: "api-key" }, scheme.name);
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
    return userFromClaims(payload.claims, "cookieSession");
}

async function cookieSessionMiddleware(ctx, next, scheme) {
    const value = sessionCookieValue(ctx, scheme.name);
    if (value === undefined) {
        return next();
    }
    const user = await verifySessionCookie(value, scheme);
    if (user === undefined) {
        return unauthorized();
    }
    ctx.user = user;
    return next();
}

export function createAuthState() {
    return {
        schemes: [],
        policies: new Map(),
    };
}

function snapshotScheme(scheme) {
    if (scheme.kind === "jwtBearer") {
        return Object.freeze({
            kind: scheme.kind,
            name: JWT_SCHEME,
            issuer: scheme.issuer,
            audience: scheme.audience,
        });
    }
    if (scheme.kind === "cookieSession") {
        return Object.freeze({
            kind: scheme.kind,
            name: COOKIE_SESSION_SCHEME,
            cookie: scheme.name,
        });
    }
    return Object.freeze({
        kind: scheme.kind,
        name: API_KEY_SCHEME,
        header: scheme.header,
    });
}

export function snapshotAuthState(state) {
    return Object.freeze({
        schemes: Object.freeze(state.schemes.map(snapshotScheme)),
        policies: Object.freeze([...state.policies.keys()]),
    });
}

export function isAuthProviderDescriptor(value) {
    return isPlainObject(value) && value.__sloppyAuth === true;
}

export function registerAuthProvider(state, provider, config) {
    if (provider.kind === "jwtBearer") {
        const scheme = Object.freeze({
            kind: "jwtBearer",
            issuer: provider.issuer,
            audience: provider.audience,
            secret: secretString(provider.secret, config, "JWT bearer"),
            clock: provider.clock,
            clockSkewSeconds: provider.clockSkewSeconds,
        });
        state.schemes.push(scheme);
        return (ctx, next) => jwtMiddleware(ctx, next, scheme);
    }
    if (provider.kind === "cookieSession") {
        const scheme = Object.freeze({
            kind: "cookieSession",
            name: provider.name,
            secret: secretString(provider.secret, config, "cookie session"),
            secure: provider.secure,
            httpOnly: provider.httpOnly,
            sameSite: provider.sameSite,
            path: provider.path,
            maxAgeSeconds: provider.maxAgeSeconds,
            clock: provider.clock,
        });
        state.schemes.push(scheme);
        state.defaultSession = scheme;
        return (ctx, next) => cookieSessionMiddleware(ctx, next, scheme);
    }
    if (provider.kind === "apiKey") {
        const scheme = Object.freeze({
            kind: "apiKey",
            name: "apiKey",
            header: provider.header,
            validate: provider.validate,
            config: config,
            configKey: provider.configKey,
            expectedKey: provider.configKey === undefined || provider.usesStaticConfigEquality !== true ? undefined : secretString(
                { __sloppyConfigReference: true, key: provider.configKey },
                config,
                "API key",
            ),
        });
        state.schemes.push(scheme);
        return (ctx, next) => apiKeyMiddleware(ctx, next, scheme);
    }
    throw new TypeError(`Sloppy Auth provider kind '${provider.kind}' is not supported.`);
}

export function normalizeAuthRequirement(options = undefined) {
    if (options === undefined) {
        return Object.freeze({ required: true });
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy requireAuth options must be a plain object when provided.");
    }
    const requirement = { required: true };
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
    return Object.freeze(requirement);
}

export function snapshotAuthRequirement(requirement) {
    if (requirement === undefined || requirement === null) {
        return undefined;
    }
    return Object.freeze({
        required: requirement.required === true,
        ...(requirement.roles === undefined ? {} : { roles: Object.freeze([...requirement.roles]) }),
        ...(requirement.policy === undefined ? {} : { policy: requirement.policy }),
        ...(requirement.claims === undefined ? {} : { claims: Object.freeze([...requirement.claims]) }),
    });
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
    if (requirement.policy !== undefined) {
        const policy = state?.policies?.get(requirement.policy);
        if (typeof policy !== "function") {
            return forbidden();
        }
        if (policy(ctx.user, ctx) !== true) {
            return forbidden();
        }
    }
    return undefined;
}

function jwtBearer(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Auth.jwtBearer options must be a plain object.");
    }
    return Object.freeze({
        __sloppyAuth: true,
        kind: "jwtBearer",
        issuer: stringOption(options.issuer, "JWT issuer", false),
        audience: stringOption(options.audience, "JWT audience", false),
        secret: options.secret,
        clock: options.clock,
        clockSkewSeconds: integerOption(options.clockSkewSeconds ?? options.clockSkew, "JWT clockSkewSeconds", 0),
    });
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

function cookieSession(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Auth.cookieSession options must be a plain object.");
    }
    const name = options.name ?? DEFAULT_SESSION_COOKIE;
    if (typeof name !== "string" || !HEADER_TOKEN_PATTERN.test(name)) {
        throw new TypeError("Sloppy Auth.cookieSession name must be a safe HTTP token.");
    }
    return Object.freeze({
        __sloppyAuth: true,
        kind: "cookieSession",
        name,
        secret: options.secret,
        secure: booleanOption(options.secure, "Auth.cookieSession secure", true),
        httpOnly: booleanOption(options.httpOnly, "Auth.cookieSession httpOnly", true),
        sameSite: normalizeSameSiteOption(options.sameSite),
        path: stringOption(options.path ?? "/", "Auth.cookieSession path"),
        maxAgeSeconds: integerOption(options.maxAgeSeconds ?? options.maxAge, "Auth.cookieSession maxAgeSeconds"),
        clock: options.clock,
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
    const current = nowSeconds(scheme.clock);
    const payload = {
        iat: current,
        ...(scheme.maxAgeSeconds === undefined ? {} : { exp: current + scheme.maxAgeSeconds }),
        claims: sessionClaims,
    };
    const value = await signSessionPayload(scheme, payload);
    ctx.user = userFromClaims(sessionClaims, "cookieSession");
    return Results.ok({ ok: true }).cookie(scheme.name, value, sessionCookieOptions(scheme, options));
}

function signOut(ctx, options = undefined) {
    const scheme = findSessionScheme(ctx);
    ctx.user = anonymousUser();
    return Results.status(204).cookie(
        scheme.name,
        "",
        sessionCookieOptions(scheme, { ...options, maxAgeSeconds: 0, expires: new Date(0) }),
    );
}

function apiKey(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy Auth.apiKey options must be a plain object.");
    }
    const header = options.header ?? "x-api-key";
    validateHeaderName(header, "API key");
    const configKey = stringOption(options.configKey, "API key configKey", false)
        ?? configRequiredKeysFromFunction(options.validate)?.[0];
    if (typeof options.validate !== "function" && configKey === undefined) {
        throw new TypeError("Sloppy Auth.apiKey requires validate or configKey.");
    }
    if (options.validate !== undefined && typeof options.validate !== "function") {
        throw new TypeError("Sloppy Auth.apiKey validate must be a function.");
    }
    return Object.freeze({
        __sloppyAuth: true,
        kind: "apiKey",
        header: header.toLowerCase(),
        validate: options.validate,
        configKey,
        usesStaticConfigEquality: typeof options.validate !== "function" ||
            apiKeyValidatorUsesOnlyConfigKey(options.validate, configKey),
    });
}

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
    jwtBearer,
    apiKey,
    cookieSession,
    signIn,
    signOut,
    constantTimeEquals: constantTimeStringEquals,
});
