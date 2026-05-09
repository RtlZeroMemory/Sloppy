import { defineFunctionModuleName } from "./modules.js";
import { Results } from "../results.js";
import { cleanupAfterFailure, finishWithCleanup, validateServiceToken } from "./services.js";
import { isPlainObject } from "./shared.js";

const ROUTE_METHODS = new Set(["GET", "POST", "PUT", "PATCH", "DELETE"]);
const PREFLIGHT_METHODS = new Set(["GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"]);
const HEADER_TOKEN_PATTERN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/u;

function validatePattern(pattern) {
    if (typeof pattern !== "string" || pattern.length === 0 || !pattern.startsWith("/")) {
        throw new TypeError("Sloppy app.mapGet pattern must be a non-empty string starting with '/'.");
    }
}

function validateGroupPrefix(prefix) {
    if (typeof prefix !== "string" || prefix.length === 0 || !prefix.startsWith("/")) {
        throw new TypeError("Sloppy app.mapGroup prefix must be a non-empty string starting with '/'.");
    }
}

function validateGroupChildPattern(pattern) {
    if (typeof pattern !== "string" || pattern.length === 0) {
        throw new TypeError("Sloppy route group child pattern must be a non-empty string.");
    }
}

function validateHandler(handler) {
    if (typeof handler !== "function") {
        throw new TypeError("Sloppy route handler must be a function.");
    }
}

function validateMiddleware(middleware) {
    if (typeof middleware !== "function") {
        throw new TypeError("Sloppy middleware must be a function.");
    }
}

function validateMiddlewareEntry(entry) {
    if (entry === null || typeof entry !== "object") {
        throw new TypeError("Sloppy middleware entries must carry { fn, sequence }.");
    }
    validateMiddleware(entry.fn);
    if (typeof entry.sequence !== "number") {
        throw new TypeError("Sloppy middleware entries must carry a numeric sequence.");
    }
}

function orderedMiddlewareFunctions(entries) {
    return [...entries]
        .sort((a, b) => a.sequence - b.sequence)
        .map((entry) => entry.fn);
}

function middlewareMetadata(middleware) {
    return Object.freeze({
        count: middleware.length,
    });
}

function invokeMiddlewarePipeline(context, middleware, terminal) {
    let index = -1;

    function dispatch(nextIndex) {
        if (nextIndex <= index) {
            throw new Error("Sloppy middleware next() must not be called more than once.");
        }

        index = nextIndex;
        const current = middleware[nextIndex];
        if (current === undefined) {
            return terminal();
        }

        let nextCalled = false;
        let downstreamPromise;
        function next() {
            if (nextCalled) {
                throw new Error("Sloppy middleware next() must not be called more than once.");
            }

            nextCalled = true;
            const downstream = dispatch(nextIndex + 1);
            downstreamPromise = Promise.resolve(downstream);
            return downstream;
        }

        const middlewareReturn = current(context, next);
        if (!nextCalled) {
            return middlewareReturn;
        }

        return Promise.resolve(middlewareReturn).then(
            (value) => downstreamPromise.then(() => value),
            (error) => downstreamPromise.then(
                () => {
                    throw error;
                },
                () => {
                    throw error;
                },
            ),
        );
    }

    return dispatch(0);
}

function handleRouteError(host, error) {
    if (typeof host.handleError !== "function") {
        throw error;
    }
    return host.handleError(error);
}

function appendContextResponseHeaders(result, context) {
    const responseHeaders = context?.__sloppyResponseHeaders;
    if (!isPlainObject(responseHeaders) || result === null || typeof result !== "object") {
        return result;
    }

    return Object.freeze({
        ...result,
        headers: Object.freeze({
            ...(isPlainObject(result.headers) ? result.headers : {}),
            ...responseHeaders,
        }),
    });
}

function finishRouteResult(result, policy, context) {
    if (result !== null && typeof result === "object" && typeof result.then === "function") {
        return Promise.resolve(result).then((value) => finishRouteResult(value, policy, context));
    }

    return finishWithCors(appendContextResponseHeaders(result, context), policy, context);
}

function finishHandledRouteError(host, error, policy, context) {
    return finishRouteResult(handleRouteError(host, error), policy, context);
}

function finishRouteError(host, error, policy, context, cleanup) {
    try {
        return finishWithCleanup(finishHandledRouteError(host, error, policy, context), cleanup);
    } catch (handledError) {
        return cleanupAfterFailure(handledError, cleanup);
    }
}

function validateController(controller) {
    if (typeof controller !== "function") {
        throw new TypeError("Sloppy controller must be a constructor function.");
    }
}

function validateControllerAction(action) {
    if (typeof action !== "string" || action.length === 0) {
        throw new TypeError("Sloppy controller action must be a non-empty string.");
    }
}

function validateMetadataOptions(options) {
    if (options === undefined) {
        return undefined;
    }

    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy route metadata options must be a plain object.");
    }

    return Object.freeze({ ...options });
}

function validateTag(tag) {
    if (typeof tag !== "string" || tag.length === 0) {
        throw new TypeError("Sloppy route group tags must be non-empty strings.");
    }
}

function validateName(name, subject) {
    if (typeof name !== "string" || name.length === 0) {
        throw new TypeError(`Sloppy ${subject} name must be a non-empty string.`);
    }
}

function validateHeaderToken(value, subject) {
    if (typeof value !== "string" || !HEADER_TOKEN_PATTERN.test(value)) {
        throw new TypeError(`Sloppy CORS ${subject} must be an HTTP token string.`);
    }
}

function normalizeStringList(value, subject, { lower = false } = {}) {
    if (value === undefined) {
        return Object.freeze([]);
    }

    const values = Array.isArray(value) ? value : [value];
    const normalized = [];

    for (const current of values) {
        if (typeof current !== "string" || current.length === 0 || /[\x00-\x1F\x7F]/u.test(current)) {
            throw new TypeError(`Sloppy CORS ${subject} entries must be non-empty strings without control characters.`);
        }
        normalized.push(lower ? current.toLowerCase() : current);
    }

    return Object.freeze([...new Set(normalized)]);
}

function normalizeTokenList(value, subject) {
    const values = normalizeStringList(value, subject);
    for (const current of values) {
        validateHeaderToken(current, subject);
    }
    return values;
}

function normalizeCorsMethods(value) {
    const methods = normalizeStringList(value, "methods").map((method) => method.toUpperCase());

    for (const method of methods) {
        if (!PREFLIGHT_METHODS.has(method)) {
            throw new TypeError("Sloppy CORS methods must be supported HTTP methods.");
        }
    }

    return Object.freeze([...new Set(methods)]);
}

function normalizeCorsPolicy(policy) {
    if (!isPlainObject(policy)) {
        throw new TypeError("Sloppy app.useCors policy must be a plain object.");
    }

    const origins = normalizeStringList(policy.origins ?? policy.origin, "origins");
    if (origins.length === 0) {
        throw new TypeError("Sloppy CORS origins must include at least one origin or '*'.");
    }

    const allowAnyOrigin = origins.includes("*");
    if (allowAnyOrigin && origins.length !== 1) {
        throw new TypeError("Sloppy CORS '*' origin cannot be combined with other origins.");
    }

    const credentials = policy.credentials === true;
    if (allowAnyOrigin && credentials) {
        throw new TypeError("Sloppy CORS credentials require explicit origins.");
    }

    const maxAgeSeconds = policy.maxAgeSeconds ?? policy.maxAge;
    if (maxAgeSeconds !== undefined && (!Number.isInteger(maxAgeSeconds) || maxAgeSeconds < 0)) {
        throw new TypeError("Sloppy CORS maxAgeSeconds must be a non-negative integer.");
    }

    const headers = normalizeTokenList(policy.headers ?? policy.allowHeaders, "headers")
        .map((header) => header.toLowerCase());
    const exposedHeaders = normalizeTokenList(policy.exposedHeaders ?? policy.exposeHeaders, "exposedHeaders");

    return Object.freeze({
        origins,
        allowAnyOrigin,
        methods: normalizeCorsMethods(policy.methods),
        headers: Object.freeze([...new Set(headers)]),
        exposedHeaders,
        credentials,
        maxAgeSeconds,
    });
}

function snapshotCorsPolicy(policy) {
    if (policy === null) {
        return undefined;
    }

    return Object.freeze({
        origins: policy.origins,
        methods: policy.methods,
        headers: policy.headers,
        exposedHeaders: policy.exposedHeaders,
        credentials: policy.credentials,
        maxAgeSeconds: policy.maxAgeSeconds,
    });
}

function getRequestHeader(context, name) {
    const headers = context?.request?.headers;
    if (headers === undefined || headers === null) {
        return undefined;
    }

    if (typeof headers.get === "function") {
        return headers.get(name) ?? headers.get(name.toLowerCase()) ?? undefined;
    }

    if (isPlainObject(headers)) {
        const lower = name.toLowerCase();
        for (const [key, value] of Object.entries(headers)) {
            if (key.toLowerCase() === lower) {
                return value;
            }
        }
    }

    return undefined;
}

function allowedOrigin(policy, origin) {
    if (typeof origin !== "string" || origin.length === 0) {
        return undefined;
    }

    if (policy.allowAnyOrigin) {
        return "*";
    }

    return policy.origins.includes(origin) ? origin : undefined;
}

function mergeVary(existing, value) {
    if (existing === undefined || existing.length === 0) {
        return value;
    }

    const tokens = existing.split(",").map((token) => token.trim().toLowerCase());
    return tokens.includes(value.toLowerCase()) ? existing : `${existing}, ${value}`;
}

function appendCorsHeaders(result, policy, context) {
    if (policy === null) {
        return result;
    }

    const origin = getRequestHeader(context, "origin");
    const allowed = allowedOrigin(policy, origin);
    if (allowed === undefined) {
        return result;
    }

    const headers = {
        ...(isPlainObject(result?.headers) ? result.headers : {}),
        "Access-Control-Allow-Origin": allowed,
    };

    if (!policy.allowAnyOrigin) {
        headers.Vary = mergeVary(headers.Vary, "Origin");
    }
    if (policy.credentials) {
        headers["Access-Control-Allow-Credentials"] = "true";
    }
    if (policy.exposedHeaders.length !== 0) {
        headers["Access-Control-Expose-Headers"] = policy.exposedHeaders.join(", ");
    }

    return Object.freeze({
        ...result,
        headers: Object.freeze(headers),
    });
}

function finishWithCors(result, policy, context) {
    if (result !== null && typeof result === "object" && typeof result.then === "function") {
        return Promise.resolve(result).then((value) => appendCorsHeaders(value, policy, context));
    }

    return appendCorsHeaders(result, policy, context);
}

function requestedHeadersAllowed(policy, requestedHeaders) {
    if (typeof requestedHeaders !== "string" || requestedHeaders.length === 0) {
        return true;
    }

    const requested = requestedHeaders
        .split(",")
        .map((header) => header.trim().toLowerCase())
        .filter((header) => header.length !== 0);

    return requested.every((header) => policy.headers.includes(header));
}

function createCorsPreflightHandler(state) {
    return function corsPreflightHandler(context) {
        const origin = getRequestHeader(context, "origin");
        const allowed = allowedOrigin(state.policy, origin);
        const requestedMethod = getRequestHeader(context, "access-control-request-method")?.toUpperCase();
        const requestedHeaders = getRequestHeader(context, "access-control-request-headers");
        const methods = state.policy.methods.length === 0 ? Array.from(state.methods) : state.policy.methods;

        if (
            allowed === undefined ||
            !methods.includes(requestedMethod) ||
            !requestedHeadersAllowed(state.policy, requestedHeaders)
        ) {
            return Results.status(403);
        }

        const headers = {
            "Access-Control-Allow-Origin": allowed,
            "Access-Control-Allow-Methods": methods.join(", "),
        };

        if (!state.policy.allowAnyOrigin) {
            headers.Vary = "Origin, Access-Control-Request-Method, Access-Control-Request-Headers";
        }
        if (state.policy.credentials) {
            headers["Access-Control-Allow-Credentials"] = "true";
        }
        if (state.policy.headers.length !== 0) {
            headers["Access-Control-Allow-Headers"] = state.policy.headers.join(", ");
        }
        if (state.policy.maxAgeSeconds !== undefined) {
            headers["Access-Control-Max-Age"] = String(state.policy.maxAgeSeconds);
        }

        return Results.status(204, undefined, { headers });
    };
}




const EMPTY_HEADERS = Object.freeze({
    get() {
        return undefined;
    },
    entries() {
        return Object.freeze([]);
    },
});

function createDefaultRequest(routeInfo) {
    return Object.freeze({
        method: routeInfo.method,
        path: routeInfo.pattern,
        rawTarget: routeInfo.pattern,
        headers: EMPTY_HEADERS,
    });
}

function createHandlerContext(host, routeInfo) {
    return {
        services: host.services.createScope(),
        capabilities: host.capabilities,
        config: host.config,
        log: host.log,
        route: {},
        routeName: routeInfo.name ?? "",
        routePattern: routeInfo.pattern,
        request: createDefaultRequest(routeInfo),
    };
}

function decorateProvidedContext(host, context, routeInfo) {
    const nextContext = {
        ...context,
    };

    nextContext.config ??= host.config;
    nextContext.log ??= host.log;
    nextContext.capabilities ??= host.capabilities;

    if (nextContext.route === undefined || nextContext.route === null) {
        nextContext.route = {};
    }
    if (nextContext.routeName === undefined) {
        nextContext.routeName = routeInfo.name ?? "";
    }
    if (nextContext.routePattern === undefined) {
        nextContext.routePattern = routeInfo.pattern;
    }
    if (nextContext.request === undefined || nextContext.request === null) {
        nextContext.request = createDefaultRequest(routeInfo);
    } else {
        nextContext.request = Object.freeze({
            ...nextContext.request,
            method: nextContext.request.method ?? routeInfo.method,
            path: nextContext.request.path ?? routeInfo.pattern,
            rawTarget: nextContext.request.rawTarget ?? nextContext.request.target ??
                nextContext.request.path ?? routeInfo.pattern,
            headers: nextContext.request.headers ?? EMPTY_HEADERS,
        });
    }

    return nextContext;
}

function createRouteHandler(host, handler, middleware = [], corsPolicy = null, routeInfo) {
    return function routeHandler(context) {
        if (context !== undefined && context !== null) {
            const providedContext = decorateProvidedContext(host, context, routeInfo);
            try {
                const result = invokeMiddlewarePipeline(
                    providedContext,
                    middleware,
                    () => handler(providedContext),
                );
                if (result !== null && typeof result === "object" && typeof result.then === "function") {
                    return Promise.resolve(result).then(
                        (value) => finishRouteResult(value, corsPolicy, providedContext),
                        (error) => finishHandledRouteError(host, error, corsPolicy, providedContext),
                    );
                }
                return finishRouteResult(result, corsPolicy, providedContext);
            } catch (error) {
                return finishHandledRouteError(host, error, corsPolicy, providedContext);
            }
        }

        const ownedContext = createHandlerContext(host, routeInfo);
        try {
            const result = invokeMiddlewarePipeline(
                ownedContext,
                middleware,
                () => handler(ownedContext),
            );
            if (result !== null && typeof result === "object" && typeof result.then === "function") {
                return Promise.resolve(result).then(
                    (value) => finishWithCleanup(
                        finishRouteResult(value, corsPolicy, ownedContext),
                        () => ownedContext.services.dispose(),
                    ),
                    (error) => finishRouteError(
                        host,
                        error,
                        corsPolicy,
                        ownedContext,
                        () => ownedContext.services.dispose(),
                    ),
                );
            }
            return finishWithCleanup(
                finishRouteResult(result, corsPolicy, ownedContext),
                () => ownedContext.services.dispose(),
            );
        } catch (error) {
            return finishRouteError(
                host,
                error,
                corsPolicy,
                ownedContext,
                () => ownedContext.services.dispose(),
            );
        }
    };
}

function snapshotRoute(route) {
    return Object.freeze({
        method: route.method,
        pattern: route.pattern,
        handler: route.handler,
        name: route.name,
        metadata: snapshotMetadata(route.metadata),
    });
}

function snapshotMetadata(metadata) {
    const snapshot = { ...metadata };

    if (Array.isArray(snapshot.tags)) {
        snapshot.tags = Object.freeze([...snapshot.tags]);
    }
    if (snapshot.cors !== undefined) {
        const { state, ...cors } = snapshot.cors;
        snapshot.cors = Object.freeze({
            ...cors,
            origins: Object.freeze([...(cors.origins ?? [])]),
            methods: Object.freeze([...(cors.methods ?? [])]),
            headers: Object.freeze([...(cors.headers ?? [])]),
            exposedHeaders: Object.freeze([...(cors.exposedHeaders ?? [])]),
        });
    }

    return Object.freeze(snapshot);
}

function createEndpointBuilder(route, assertAppMutable) {
    const endpoint = {
        withName(name) {
            assertAppMutable();
            validateName(name, "endpoint");

            route.name = name;
            if (route.routeInfo !== undefined) {
                route.routeInfo.name = name;
            }
            return endpoint;
        },
    };

    return Object.freeze(endpoint);
}

function normalizeGroupPrefix(prefix) {
    validateGroupPrefix(prefix);

    if (prefix === "/") {
        return "/";
    }

    return prefix.replace(/\/+$/u, "");
}

function composeRoutePattern(prefix, childPattern) {
    validateGroupChildPattern(childPattern);

    if (prefix === "/") {
        return childPattern.startsWith("/") ? childPattern : `/${childPattern}`;
    }

    if (childPattern === "/") {
        return prefix;
    }

    return childPattern.startsWith("/") ? `${prefix}${childPattern}` : `${prefix}/${childPattern}`;
}

function normalizeMapArguments(pattern, optionsOrHandler, maybeHandler) {
    if (typeof optionsOrHandler === "function" && maybeHandler === undefined) {
        return {
            pattern,
            metadata: undefined,
            handler: optionsOrHandler,
        };
    }

    return {
        pattern,
        metadata: validateMetadataOptions(optionsOrHandler),
        handler: maybeHandler,
    };
}

function mergeRouteMetadata(groupMetadata, routeMetadata) {
    if (routeMetadata?.tags !== undefined && !Array.isArray(routeMetadata.tags)) {
        throw new TypeError("Sloppy route metadata tags must be an array when provided.");
    }

    const tags = [
        ...groupMetadata.tags,
        ...((routeMetadata?.tags !== undefined) ? routeMetadata.tags : []),
    ];

    for (const tag of tags) {
        validateTag(tag);
    }

    return {
        ...routeMetadata,
        tags,
        groupName: groupMetadata.name,
        groupPrefix: groupMetadata.prefix,
    };
}

function createRouteMetadata(routeMetadata) {
    if (routeMetadata?.tags !== undefined) {
        if (!Array.isArray(routeMetadata.tags)) {
            throw new TypeError("Sloppy route metadata tags must be an array when provided.");
        }

        for (const tag of routeMetadata.tags) {
            validateTag(tag);
        }

        return {
            ...routeMetadata,
            tags: [...routeMetadata.tags],
        };
    }

    return routeMetadata ?? {};
}

function registerRoute(
    routes,
    host,
    assertAppMutable,
    currentModule,
    method,
    pattern,
    optionsOrHandler,
    maybeHandler,
    metadataBase,
    middleware = [],
    corsPolicy = null,
) {
    const args = normalizeMapArguments(pattern, optionsOrHandler, maybeHandler);

    assertAppMutable();
    validatePattern(args.pattern);
    validateHandler(args.handler);
    if (!ROUTE_METHODS.has(method)) {
        throw new TypeError("Sloppy route method is not supported by bootstrap registration.");
    }
    for (const current of middleware) {
        validateMiddlewareEntry(current);
    }

    if (routes.some((route) => route.method === method && route.pattern === args.pattern)) {
        throw new Error(`Sloppy route '${method} ${args.pattern}' is already registered.`);
    }

    const orderedMiddleware = orderedMiddlewareFunctions(middleware);
    const routeInfo = { method, pattern: args.pattern, name: null };
    const route = {
        method,
        pattern: args.pattern,
        handler: createRouteHandler(
            host,
            args.handler,
            Object.freeze(orderedMiddleware),
            corsPolicy,
            routeInfo,
        ),
        name: null,
        routeInfo,
        metadata: {
            ...(metadataBase ? mergeRouteMetadata(metadataBase, args.metadata) : createRouteMetadata(args.metadata)),
            ...((currentModule !== null) ? { module: currentModule } : {}),
            middleware: middlewareMetadata(orderedMiddleware),
            ...((corsPolicy !== null) ? { cors: snapshotCorsPolicy(corsPolicy) } : {}),
        },
    };

    routes.push(route);
    if (corsPolicy !== null) {
        registerCorsPreflightRoute(
            routes,
            host,
            assertAppMutable,
            args.pattern,
            method,
            corsPolicy,
            Object.freeze(orderedMiddleware),
        );
    }
    return createEndpointBuilder(route, assertAppMutable);
}

function corsPoliciesEqual(a, b) {
    if (a === b) {
        return true;
    }
    if (a === null || b === null || typeof a !== "object" || typeof b !== "object") {
        return false;
    }
    if (
        a.allowAnyOrigin !== b.allowAnyOrigin ||
        a.credentials !== b.credentials ||
        a.maxAgeSeconds !== b.maxAgeSeconds
    ) {
        return false;
    }
    const arraysEqual = (left, right) => {
        if (!Array.isArray(left) || !Array.isArray(right) || left.length !== right.length) {
            return false;
        }
        for (let index = 0; index < left.length; index += 1) {
            if (left[index] !== right[index]) {
                return false;
            }
        }
        return true;
    };
    return (
        arraysEqual(a.origins, b.origins) &&
        arraysEqual(a.methods, b.methods) &&
        arraysEqual(a.headers, b.headers) &&
        arraysEqual(a.exposedHeaders, b.exposedHeaders)
    );
}

function registerCorsPreflightRoute(
    routes,
    host,
    assertAppMutable,
    pattern,
    method,
    corsPolicy,
    middleware,
) {
    const existing = routes.find((route) => route.method === "OPTIONS" &&
        route.pattern === pattern &&
        route.metadata?.cors?.preflight === true);

    if (existing !== undefined) {
        if (!corsPoliciesEqual(existing.metadata.cors.state.policy, corsPolicy)) {
            throw new Error(`Sloppy CORS preflight route '${pattern}' already has a different policy.`);
        }
        existing.metadata.cors.state.methods.add(method);
        existing.handler = createRouteHandler(
            host,
            createCorsPreflightHandler(existing.metadata.cors.state),
            middleware,
            null,
            existing.routeInfo ?? { method: "OPTIONS", pattern, name: existing.name ?? null },
        );
        existing.metadata.middleware = middlewareMetadata(middleware);
        return;
    }

    const state = {
        policy: corsPolicy,
        methods: new Set([method]),
    };
    const routeInfo = { method: "OPTIONS", pattern, name: null };
    routes.push({
        method: "OPTIONS",
        pattern,
        handler: createRouteHandler(
            host,
            createCorsPreflightHandler(state),
            middleware,
            null,
            routeInfo,
        ),
        name: null,
        routeInfo,
        metadata: {
            cors: {
                ...snapshotCorsPolicy(corsPolicy),
                preflight: true,
                state,
            },
            middleware: middlewareMetadata(middleware),
        },
    });
}

function controllerInjectionTokens(controller) {
    const tokens = controller.inject ?? controller.dependencies ?? [];

    if (!Array.isArray(tokens)) {
        throw new TypeError("Sloppy controller inject metadata must be an array when provided.");
    }

    for (const token of tokens) {
        validateServiceToken(token);
    }

    return Object.freeze([...tokens]);
}

function createControllerHandler(host, Controller, action, routeInfo) {
    const inject = controllerInjectionTokens(Controller);
    const prototypeMethod = Controller.prototype?.[action];

    if (typeof prototypeMethod !== "function") {
        throw new TypeError(`Sloppy controller action '${action}' must name a prototype method.`);
    }

    return function controllerHandler(context) {
        let ctx = context === undefined || context === null
            ? createHandlerContext(host, routeInfo)
            : decorateProvidedContext(host, context, routeInfo);
        let ownsServices = context === undefined || context === null;
        if (ctx.services === undefined || ctx.services === null) {
            ctx = Object.freeze({
                ...ctx,
                services: host.services.createScope(),
            });
            ownsServices = true;
        }
        const services = ctx.services;
        try {
            const dependencies = inject.map((token) => services.get(token));
            const instance = new Controller(...dependencies);
            const result = instance[action](ctx);
            if (ownsServices) {
                return finishWithCleanup(result, () => services.dispose());
            }
            return result;
        } catch (error) {
            if (ownsServices) {
                return cleanupAfterFailure(error, () => services.dispose());
            }
            throw error;
        }
    };
}

function createControllerMapper(
    routes,
    host,
    assertAppMutable,
    currentModule,
    prefix,
    Controller,
    middleware = [],
    getCorsPolicy = () => null,
) {
    const normalizedPrefix = normalizeGroupPrefix(prefix);
    validateController(Controller);

    function map(method, pattern, action, options) {
        validateControllerAction(action);
        return registerRoute(
            routes,
            host,
            assertAppMutable,
            currentModule,
            method,
            composeRoutePattern(normalizedPrefix, pattern),
            {
                ...(options ?? {}),
                controller: Controller.name || "AnonymousController",
                action,
            },
            createControllerHandler(
                host,
                Controller,
                action,
                Object.freeze({ method, pattern: composeRoutePattern(normalizedPrefix, pattern) }),
            ),
            undefined,
            middleware,
            getCorsPolicy(),
        );
    }

    return Object.freeze({
        get(pattern, action, options) {
            return map("GET", pattern, action, options);
        },
        post(pattern, action, options) {
            return map("POST", pattern, action, options);
        },
        put(pattern, action, options) {
            return map("PUT", pattern, action, options);
        },
        patch(pattern, action, options) {
            return map("PATCH", pattern, action, options);
        },
        delete(pattern, action, options) {
            return map("DELETE", pattern, action, options);
        },
    });
}

function createRouteGroup(
    routes,
    host,
    assertAppMutable,
    getCurrentModule,
    prefix,
    getInheritedMiddleware = () => [],
    nextMiddlewareSequence = () => 0,
    getCorsPolicy = () => null,
) {
    const groupMetadata = {
        prefix: normalizeGroupPrefix(prefix),
        tags: [],
        name: null,
    };
    const groupMiddleware = [];

    function createMapMethod(method) {
        return function mapRoute(pattern, optionsOrHandler, maybeHandler) {
            const fullPattern = composeRoutePattern(groupMetadata.prefix, pattern);
            return registerRoute(
                routes,
                host,
                assertAppMutable,
                getCurrentModule(),
                method,
                fullPattern,
                optionsOrHandler,
                maybeHandler,
                {
                    prefix: groupMetadata.prefix,
                    tags: groupMetadata.tags,
                    name: groupMetadata.name,
                },
                [...getInheritedMiddleware(), ...groupMiddleware],
                getCorsPolicy(),
            );
        };
    }

    const group = {
        get prefix() {
            return groupMetadata.prefix;
        },

        withTags(...tags) {
            assertAppMutable();

            for (const tag of tags) {
                validateTag(tag);
            }

            groupMetadata.tags.push(...tags);
            return group;
        },

        withName(name) {
            assertAppMutable();
            validateName(name, "route group");

            groupMetadata.name = name;
            return group;
        },

        use(middleware) {
            assertAppMutable();
            validateMiddleware(middleware);

            groupMiddleware.push({ fn: middleware, sequence: nextMiddlewareSequence() });
            return group;
        },

        mapGet: createMapMethod("GET"),
        mapPost: createMapMethod("POST"),
        mapPut: createMapMethod("PUT"),
        mapPatch: createMapMethod("PATCH"),
        mapDelete: createMapMethod("DELETE"),
        get: createMapMethod("GET"),
        post: createMapMethod("POST"),
        put: createMapMethod("PUT"),
        patch: createMapMethod("PATCH"),
        delete: createMapMethod("DELETE"),
        group(childPrefix) {
            assertAppMutable();
            return createRouteGroup(
                routes,
                host,
                assertAppMutable,
                getCurrentModule,
                composeRoutePattern(groupMetadata.prefix, childPrefix),
                () => [...getInheritedMiddleware(), ...groupMiddleware],
                nextMiddlewareSequence,
                getCorsPolicy,
            );
        },
    };

    return Object.freeze(group);
}


function createRouterGroup(prefix, configure) {
    validateGroupPrefix(prefix);

    if (configure !== undefined && typeof configure !== "function") {
        throw new TypeError("Sloppy Router.group configure callback must be a function.");
    }

    function routerGroup(app) {
        const group = app.group(prefix);
        if (configure !== undefined) {
            configure(group);
        }
        return group;
    }

    defineFunctionModuleName(routerGroup, `router:${normalizeGroupPrefix(prefix)}`);
    return Object.freeze(routerGroup);
}

export {
    createControllerMapper,
    createRouteGroup,
    createRouterGroup,
    normalizeCorsPolicy,
    registerRoute,
    snapshotRoute,
};
