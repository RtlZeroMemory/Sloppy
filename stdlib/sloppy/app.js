import {
    createCapabilityProvider,
    createCapabilityRegistry,
} from "./internal/capabilities.js";
import {
    createAuthState,
    isAuthProviderDescriptor,
    registerAuthProvider,
    snapshotAuthState,
} from "./auth.js";
import { createConfigBuilder, createConfigProvider } from "./internal/config.js";
import { createLogger, createLoggingBuilder } from "./internal/logging.js";
import {
    assertRouteOnlyModule,
    createModule,
    createModuleDebugEntries,
    functionModuleName,
    getModuleState,
    requireModuleState,
    resolveModuleOrder,
    runModulePhase,
} from "./internal/modules.js";
import {
    createControllerMapper,
    createRouteGroup,
    createRouterGroup,
    normalizeCorsPolicy,
    registerRoute,
    snapshotRoute,
} from "./internal/routes.js";
import { createServiceProvider, createServicesBuilder } from "./internal/services.js";
import { createMutationGuard, isPlainObject } from "./internal/shared.js";
import { normalizeJsonOptions, Results } from "./results.js";
import { isValidationError, validationProblem } from "./schema.js";

const DEFAULT_HEALTH_PATH = "/health";
const DEFAULT_LIVENESS_PATH = "/health/live";
const DEFAULT_READINESS_PATH = "/health/ready";
const DEFAULT_MAX_ERROR_BODY_BYTES = 1024 * 1024;
const DEFAULT_CONTENT_NEGOTIATION = Object.freeze({
    strictAccept: false,
});

function normalizeContentNegotiationOptions(options = undefined) {
    if (options === undefined) {
        return DEFAULT_CONTENT_NEGOTIATION;
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy content negotiation options must be a plain object.");
    }
    const strictAccept = options.strictAccept ?? DEFAULT_CONTENT_NEGOTIATION.strictAccept;
    if (typeof strictAccept !== "boolean") {
        throw new TypeError("Sloppy content negotiation strictAccept must be a boolean.");
    }
    return Object.freeze({ strictAccept });
}

function normalizeAppOptions(options = undefined) {
    if (options === undefined) {
        return Object.freeze({
            json: normalizeJsonOptions(),
            contentNegotiation: DEFAULT_CONTENT_NEGOTIATION,
        });
    }
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy.create options must be a plain object.");
    }
    return Object.freeze({
        json: normalizeJsonOptions(options.json),
        contentNegotiation: normalizeContentNegotiationOptions(options.contentNegotiation),
    });
}

function validateProviderDescriptor(provider) {
    if (!isPlainObject(provider) || provider.__sloppyProvider !== true) {
        throw new TypeError("Sloppy app.use expects a Sloppy provider descriptor.");
    }
    if (provider.kind !== "sqlite") {
        throw new TypeError("Sloppy app.use currently supports sqlite provider descriptors.");
    }
    if (typeof provider.name !== "string" || provider.name.length === 0) {
        throw new TypeError("Sloppy sqlite provider name must be a non-empty string.");
    }
}

function isProblemDetailsDescriptor(value) {
    return isPlainObject(value) &&
        (value.__sloppyProblemDetails === true || value.__sloppyErrorPolicy === true);
}

function isBooleanConfigReference(value) {
    return isPlainObject(value) &&
        value.__sloppyConfigReference === true &&
        value.type === "boolean" &&
        typeof value.key === "string";
}

function resolveBooleanOption(value, config, fallback, subject) {
    if (value === undefined) {
        return fallback;
    }
    if (typeof value === "boolean") {
        return value;
    }
    if (isBooleanConfigReference(value)) {
        const defaultValue = value.default === undefined ? fallback : value.default;
        return config.getBool(value.key, defaultValue);
    }
    throw new TypeError(`Sloppy ${subject} must be a boolean or Config.boolean reference.`);
}

function resolveDetailPolicy(options, config) {
    if (options?.detail !== undefined) {
        return options.detail;
    }
    return resolveBooleanOption(options?.includeDetails, config, false, "error includeDetails")
        ? "always"
        : "never";
}

function normalizeErrorPolicyOptions(options, config) {
    if (options !== undefined && !isPlainObject(options)) {
        throw new TypeError("Sloppy app.useErrors options must be a plain object.");
    }

    const detail = resolveDetailPolicy(options, config);
    if (detail !== "never" && detail !== "development" && detail !== "always") {
        throw new TypeError("Sloppy error detail policy must be never, development, or always.");
    }

    if (options?.missingRoute !== undefined && typeof options.missingRoute !== "boolean") {
        throw new TypeError("Sloppy app.useErrors missingRoute must be a boolean when provided.");
    }

    const maxBodyBytes = options?.maxBodyBytes ?? DEFAULT_MAX_ERROR_BODY_BYTES;
    if (!Number.isInteger(maxBodyBytes) || maxBodyBytes < 0) {
        throw new TypeError("Sloppy app.useErrors maxBodyBytes must be a non-negative integer.");
    }

    return Object.freeze({
        detail,
        missingRoute: options?.missingRoute ?? true,
        maxBodyBytes,
    });
}

function normalizeLegacyProblemDetails(descriptor, config) {
    return Object.freeze({
        detail: descriptor.detail ?? "never",
        missingRoute: false,
        maxBodyBytes: DEFAULT_MAX_ERROR_BODY_BYTES,
    });
}

function shouldIncludeDetails(policy, config) {
    const environment = String(config.get("Sloppy:Environment", config.get("Environment", "")));
    return policy.detail === "always" ||
        (policy.detail === "development" && environment.toLowerCase() === "development");
}

function problemBody(status, title, code, context, error, policy, config) {
    const problem = { status, title, code };
    if (typeof context?.requestId === "string" && context.requestId.length !== 0) {
        problem.requestId = context.requestId;
    }
    if (error !== undefined && shouldIncludeDetails(policy, config)) {
        problem.detail = String(error?.message ?? error);
    }
    return Object.freeze(problem);
}

function problemResult(status, title, code, context, error, policy, config) {
    return Results.problem(problemBody(status, title, code, context, error, policy, config), { status });
}

function validationProblemResult(error, context) {
    const problem = {
        ...validationProblem(error.issues),
    };
    if (typeof context?.requestId === "string" && context.requestId.length !== 0) {
        problem.requestId = context.requestId;
    }
    return Results.problem(Object.freeze(problem), { status: 400 });
}

function isProviderError(error) {
    return error !== null && typeof error === "object" &&
        (error.__sloppyProviderError === true || /provider|database|sqlite|postgres|sqlserver/iu.test(String(error.name ?? "")));
}

function statusTitle(status) {
    switch (status) {
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        default: return status >= 500 ? "Internal Server Error" : "Request Failed";
    }
}

function codeForStatus(status) {
    switch (status) {
        case 400: return "SLOPPY_E_BAD_REQUEST";
        case 401: return "SLOPPY_E_AUTH_UNAUTHORIZED";
        case 403: return "SLOPPY_E_AUTH_FORBIDDEN";
        case 404: return "SLOPPY_E_NOT_FOUND";
        case 413: return "SLOPPY_E_PAYLOAD_TOO_LARGE";
        case 415: return "SLOPPY_E_UNSUPPORTED_MEDIA_TYPE";
        default: return "SLOPPY_E_HANDLER_ERROR";
    }
}

function logErrorOnce(context, error, status, code) {
    if (context?.__sloppyErrorLogged === true ||
        context?.log === undefined ||
        typeof context.log.error !== "function")
    {
        return;
    }
    Object.defineProperty(context, "__sloppyErrorLogged", {
        value: true,
        enumerable: false,
        writable: true,
        configurable: true,
    });
    const fields = { status, code };
    if (typeof context.requestId === "string") {
        fields.requestId = context.requestId;
    }
    if (typeof context.routePattern === "string") {
        fields.route = context.routePattern;
        fields.routePattern = context.routePattern;
    }
    if (typeof error?.name === "string") {
        fields.errorName = error.name;
    }
    context.log.error("request failed", fields);
}

function resultFromMapper(mapped, status) {
    if (mapped?.__sloppyResult === true) {
        return mapped;
    }
    if (isPlainObject(mapped)) {
        const resolvedStatus = Number.isInteger(mapped.status) ? mapped.status : status;
        return Results.problem(mapped, { status: resolvedStatus });
    }
    throw new TypeError("Sloppy app.mapError mapper must return Results.* or a ProblemDetails object.");
}

function mappedErrorResult(error, context, policyState) {
    for (const mapping of policyState.mappings) {
        if (error instanceof mapping.type) {
            return resultFromMapper(mapping.mapper(error, context), 500);
        }
    }
    return undefined;
}

function errorPolicyResult(error, context, policyState, config) {
    if (isValidationError(error)) {
        return validationProblemResult(error, context);
    }
    let mapped;
    try {
        mapped = mappedErrorResult(error, context, policyState);
    } catch (mapperError) {
        logErrorOnce(context, mapperError, 500, "SLOPPY_E_HANDLER_ERROR");
        return problemResult(
            500,
            "Internal Server Error",
            "SLOPPY_E_HANDLER_ERROR",
            context,
            mapperError,
            policyState.policy,
            config,
        );
    }
    if (mapped !== undefined) {
        const status = Number.isInteger(mapped.status) ? mapped.status : 500;
        logErrorOnce(context, error, status, mapped.body?.code ?? codeForStatus(status));
        return mapped;
    }
    if (isProviderError(error)) {
        logErrorOnce(context, error, 500, "SLOPPY_E_PROVIDER_ERROR");
        return problemResult(500, "Provider error", "SLOPPY_E_PROVIDER_ERROR", context, undefined, policyState.policy, config);
    }
    logErrorOnce(context, error, 500, "SLOPPY_E_HANDLER_ERROR");
    return problemResult(500, "Internal Server Error", "SLOPPY_E_HANDLER_ERROR", context, error, policyState.policy, config);
}

function standardProblemResult(status, context, policyState, config) {
    return problemResult(status, statusTitle(status), codeForStatus(status), context, undefined, policyState.policy, config);
}

function isWorkerResource(resource) {
    return resource !== null &&
        typeof resource === "object" &&
        typeof resource.__sloppyPlanMetadata === "function" &&
        typeof resource.__sloppyWorkerResource === "string";
}

function snapshotWorkerResource(resource) {
    return Object.freeze({ ...resource.__sloppyPlanMetadata() });
}

function sqliteProviderToken(name) {
    return name.includes(".") ? name : `data.${name}`;
}

function validateMergedProviderOptions(provider, options) {
    if (provider.kind === "sqlite") {
        if (!isPlainObject(options)) {
            throw new TypeError("Sloppy sqlite provider options must be a plain object.");
        }
        if (typeof options.database !== "string" || options.database.length === 0) {
            throw new TypeError(
                "Sloppy sqlite provider database option must be a non-empty string.",
            );
        }
    }
}

function validateHealthPath(path, subject) {
    if (typeof path !== "string" || path.length === 0 || !path.startsWith("/")) {
        throw new TypeError(`Sloppy ${subject} path must be a non-empty string starting with '/'.`);
    }
}

function validateHealthCheckName(name) {
    if (typeof name !== "string" || name.length === 0) {
        throw new TypeError("Sloppy health check name must be a non-empty string.");
    }
}

function validateHealthCheckFunction(check) {
    if (typeof check !== "function") {
        throw new TypeError("Sloppy health check must be a function.");
    }
}

function validateStaticRequestPath(path) {
    if (typeof path !== "string" || path.length === 0 || !path.startsWith("/")) {
        throw new TypeError("Sloppy static files requestPath must be a non-empty string starting with '/'.");
    }
    if (path.length > 1 && path.endsWith("/")) {
        throw new TypeError("Sloppy static files requestPath must not end with '/'.");
    }
    if (path.includes("{") || path.includes("}")) {
        throw new TypeError("Sloppy static files requestPath must be a static route prefix.");
    }
}

function validateStaticRoot(root) {
    if (typeof root !== "string" || root.length === 0) {
        throw new TypeError("Sloppy static files root must be a non-empty string.");
    }
    if (root.startsWith("/") || /^[A-Za-z]:[\\/]/.test(root)) {
        throw new TypeError("Sloppy static files root must be project-relative.");
    }
    const parts = root.split(/[\\/]/);
    if (parts.some((part) => part.length === 0 || part === "." || part === "..")) {
        throw new TypeError("Sloppy static files root must not contain empty, '.', or '..' path segments.");
    }
}

function validateStaticFilesOptions(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy app.useStaticFiles options must be a plain object.");
    }
    validateStaticRequestPath(options.requestPath);
    validateStaticRoot(options.root);
    if (options.cache !== undefined) {
        if (!isPlainObject(options.cache)) {
            throw new TypeError("Sloppy static files cache option must be a plain object.");
        }
        if (
            options.cache.maxAgeSeconds !== undefined &&
            (!Number.isInteger(options.cache.maxAgeSeconds) || options.cache.maxAgeSeconds < 0)
        ) {
            throw new TypeError("Sloppy static files cache.maxAgeSeconds must be a non-negative integer.");
        }
    }
}

function normalizeHealthCheck(check, index) {
    if (typeof check === "function") {
        const name = check.name.length > 0 ? check.name : `check-${index + 1}`;
        return Object.freeze({
            name,
            check,
            liveness: false,
            readiness: true,
        });
    }

    if (!isPlainObject(check)) {
        throw new TypeError("Sloppy health checks must be functions or plain objects.");
    }

    validateHealthCheckName(check.name);
    validateHealthCheckFunction(check.check);

    const liveness = check.liveness === true;
    const readiness = check.readiness !== false;

    if (!liveness && !readiness) {
        throw new TypeError("Sloppy health check must target readiness or liveness.");
    }

    return Object.freeze({
        name: check.name,
        check: check.check,
        liveness,
        readiness,
    });
}

function normalizeHealthOptions(options) {
    if (options === undefined) {
        return Object.freeze({
            path: DEFAULT_HEALTH_PATH,
            livenessPath: DEFAULT_LIVENESS_PATH,
            readinessPath: DEFAULT_READINESS_PATH,
            checks: Object.freeze([]),
        });
    }

    if (typeof options === "string") {
        validateHealthPath(options, "health");
        if (new Set([options, DEFAULT_LIVENESS_PATH, DEFAULT_READINESS_PATH]).size !== 3) {
            throw new TypeError("Sloppy health, liveness, and readiness paths must be distinct.");
        }
        return Object.freeze({
            path: options,
            livenessPath: DEFAULT_LIVENESS_PATH,
            readinessPath: DEFAULT_READINESS_PATH,
            checks: Object.freeze([]),
        });
    }

    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy app.mapHealthChecks options must be a plain object.");
    }

    const path = options.path ?? DEFAULT_HEALTH_PATH;
    const livenessPath = options.livenessPath ?? DEFAULT_LIVENESS_PATH;
    const readinessPath = options.readinessPath ?? DEFAULT_READINESS_PATH;

    validateHealthPath(path, "health");
    validateHealthPath(livenessPath, "liveness");
    validateHealthPath(readinessPath, "readiness");

    if (new Set([path, livenessPath, readinessPath]).size !== 3) {
        throw new TypeError("Sloppy health, liveness, and readiness paths must be distinct.");
    }

    if (options.checks !== undefined && !Array.isArray(options.checks)) {
        throw new TypeError("Sloppy health checks option must be an array when provided.");
    }

    const checks = (options.checks ?? []).map(normalizeHealthCheck);

    return Object.freeze({
        path,
        livenessPath,
        readinessPath,
        checks: Object.freeze(checks),
    });
}

function checkAppliesToMode(check, mode) {
    if (mode === "liveness") {
        return check.liveness;
    }

    if (mode === "readiness") {
        return check.readiness;
    }

    return true;
}

function healthCheckNames(checks, predicate) {
    return Object.freeze(checks.filter(predicate).map((check) => check.name));
}

function normalizeHealthCheckResult(result) {
    if (result === undefined) {
        return true;
    }

    if (typeof result === "boolean") {
        return result;
    }

    if (isPlainObject(result) && typeof result.ok === "boolean") {
        return result.ok;
    }

    return true;
}

async function runHealthChecks(checks, mode, context) {
    const selected = checks.filter((check) => checkAppliesToMode(check, mode));
    const results = [];
    let healthy = true;

    for (const check of selected) {
        try {
            const ok = normalizeHealthCheckResult(await check.check(context));
            healthy = healthy && ok;
            results.push(Object.freeze({
                name: check.name,
                status: ok ? "healthy" : "unhealthy",
            }));
        } catch {
            healthy = false;
            results.push(Object.freeze({
                name: check.name,
                status: "unhealthy",
            }));
        }
    }

    return Object.freeze({
        status: healthy ? "healthy" : "unhealthy",
        checks: Object.freeze(results),
    });
}

function createHealthHandler(checks, mode) {
    return async function healthHandler(context) {
        const body = await runHealthChecks(checks, mode, context);
        if (body.status === "healthy") {
            return Results.ok(body);
        }

        return Results.status(503, body);
    };
}

function createApp(host) {
    const routes = [];
    const workerResources = [];
    const middleware = [];
    const middlewareSequence = { value: 0 };
    function nextMiddlewareSequence() {
        const seq = middlewareSequence.value;
        middlewareSequence.value = seq + 1;
        return seq;
    }
    const guard = createMutationGuard("app");
    let corsPolicy = null;
    let currentModule = null;
    const moduleDebugRef = host.moduleDebugRef ?? { modules: Object.freeze([]) };
    const directModules = new Set();
    const errorPolicyState = {
        policy: null,
        mappings: [],
    };
    let jsonOptions = host.options.json;
    let contentNegotiation = host.options.contentNegotiation;
    const authState = createAuthState();
    const routeHost = {
        ...host,
        auth: authState,
        handleError(error, context) {
            if (errorPolicyState.policy === null) {
                if (isValidationError(error)) {
                    return validationProblemResult(error, context);
                }
                throw error;
            }
            return errorPolicyResult(error, context, errorPolicyState, host.config);
        },
    };

    function assertAppMutable() {
        guard.assertMutable();
    }

    function getCurrentModule() {
        return currentModule;
    }

    function getCorsPolicy() {
        return corsPolicy;
    }

    const app = {
        config: host.config,
        log: host.log,
        services: host.services,
        capabilities: host.capabilities,
        auth: Object.freeze({
            addPolicy(name, policy) {
                assertAppMutable();
                if (typeof name !== "string" || name.length === 0) {
                    throw new TypeError("Sloppy auth policy name must be a non-empty string.");
                }
                if (typeof policy !== "function") {
                    throw new TypeError("Sloppy auth policy must be a function.");
                }
                if (authState.policies.has(name)) {
                    throw new TypeError(`Sloppy auth policy '${name}' is already registered.`);
                }
                authState.policies.set(name, policy);
                return app.auth;
            },
        }),

        use(provider) {
            assertAppMutable();
            if (isProblemDetailsDescriptor(provider)) {
                errorPolicyState.policy = provider.__sloppyProblemDetails === true
                    ? normalizeLegacyProblemDetails(provider, host.config)
                    : normalizeErrorPolicyOptions(provider, host.config);
                return app;
            }
            if (typeof provider === "function") {
                middleware.push({ fn: provider, sequence: nextMiddlewareSequence() });
                return app;
            }
            if (isAuthProviderDescriptor(provider)) {
                middleware.push({
                    fn: registerAuthProvider(authState, provider, host.config),
                    sequence: nextMiddlewareSequence(),
                });
                return app;
            }
            if (isWorkerResource(provider)) {
                if (workerResources.includes(provider)) {
                    return provider;
                }
                if (typeof provider.__sloppyStartForApp === "function") {
                    provider.__sloppyStartForApp(app);
                }
                workerResources.push(provider);
                return provider;
            }
            validateProviderDescriptor(provider);

            const configured = host.config.bind(`${provider.kind}:${provider.name}`);
            const options = Object.freeze({
                ...configured,
                ...(provider.options ?? {}),
            });
            validateMergedProviderOptions(provider, options);
            return Object.freeze({
                kind: provider.kind,
                name: provider.name,
                token: sqliteProviderToken(provider.name),
                options,
            });
        },

        useErrors(options = undefined) {
            assertAppMutable();
            errorPolicyState.policy = normalizeErrorPolicyOptions(options, host.config);
            return app;
        },

        mapError(type, mapper) {
            assertAppMutable();
            if (typeof type !== "function") {
                throw new TypeError("Sloppy app.mapError type must be an Error constructor.");
            }
            if (typeof mapper !== "function") {
                throw new TypeError("Sloppy app.mapError mapper must be a function.");
            }
            errorPolicyState.mappings.push(Object.freeze({ type, mapper }));
            return app;
        },

        useCors(policy) {
            assertAppMutable();
            corsPolicy = normalizeCorsPolicy(policy);
            return app;
        },

        useStaticFiles(options) {
            assertAppMutable();
            validateStaticFilesOptions(options);
            return app;
        },

        useJson(options) {
            assertAppMutable();
            if (options !== undefined && !isPlainObject(options)) {
                throw new TypeError("Sloppy JSON options must be a plain object.");
            }
            jsonOptions = normalizeJsonOptions({
                ...jsonOptions,
                ...(options ?? {}),
            });
            return app;
        },

        useContentNegotiation(options) {
            assertAppMutable();
            if (options !== undefined && !isPlainObject(options)) {
                throw new TypeError("Sloppy content negotiation options must be a plain object.");
            }
            contentNegotiation = normalizeContentNegotiationOptions({
                ...contentNegotiation,
                ...(options ?? {}),
            });
            return app;
        },

        useModule(moduleOrFactory) {
            assertAppMutable();

            const moduleState = getModuleState(moduleOrFactory);
            if (moduleState !== undefined) {
                assertRouteOnlyModule(moduleState);
                if (directModules.has(moduleState.name)) {
                    throw new Error(`Sloppy module '${moduleState.name}' is already registered.`);
                }
                moduleState.finalized = true;
                directModules.add(moduleState.name);
                for (const callback of moduleState.routeCallbacks) {
                    app.__runInModule(moduleState.name, (moduleApp) => {
                        runModulePhase(moduleState, "routes", callback, moduleApp);
                    });
                }
                return app;
            }

            if (typeof moduleOrFactory !== "function" || functionModuleName(moduleOrFactory).length === 0) {
                throw new TypeError(
                    "Sloppy app.useModule expected a named function module or route-only Sloppy.module.",
                );
            }
            const moduleName = functionModuleName(moduleOrFactory);
            if (directModules.has(moduleName)) {
                throw new Error(`Sloppy module '${moduleName}' is already registered.`);
            }
            directModules.add(moduleName);
            app.__runInModule(moduleName, (moduleApp) => {
                const result = moduleOrFactory(moduleApp);
                if (result !== null && typeof result === "object" && typeof result.then === "function") {
                    throw new TypeError("Sloppy function modules must be synchronous.");
                }
            });
            return app;
        },

        mapGet(pattern, optionsOrHandler, maybeHandler) {
            return registerRoute(
                routes,
                routeHost,
                assertAppMutable,
                currentModule,
                "GET",
                pattern,
                optionsOrHandler,
                maybeHandler,
                undefined,
                middleware,
                corsPolicy,
            );
        },

        mapPost(pattern, optionsOrHandler, maybeHandler) {
            return registerRoute(
                routes,
                routeHost,
                assertAppMutable,
                currentModule,
                "POST",
                pattern,
                optionsOrHandler,
                maybeHandler,
                undefined,
                middleware,
                corsPolicy,
            );
        },

        mapPut(pattern, optionsOrHandler, maybeHandler) {
            return registerRoute(
                routes,
                routeHost,
                assertAppMutable,
                currentModule,
                "PUT",
                pattern,
                optionsOrHandler,
                maybeHandler,
                undefined,
                middleware,
                corsPolicy,
            );
        },

        mapPatch(pattern, optionsOrHandler, maybeHandler) {
            return registerRoute(
                routes,
                routeHost,
                assertAppMutable,
                currentModule,
                "PATCH",
                pattern,
                optionsOrHandler,
                maybeHandler,
                undefined,
                middleware,
                corsPolicy,
            );
        },

        mapDelete(pattern, optionsOrHandler, maybeHandler) {
            return registerRoute(
                routes,
                routeHost,
                assertAppMutable,
                currentModule,
                "DELETE",
                pattern,
                optionsOrHandler,
                maybeHandler,
                undefined,
                middleware,
                corsPolicy,
            );
        },

        mapHealthChecks(options) {
            assertAppMutable();
            const health = normalizeHealthOptions(options);

            const targets = [health.path, health.livenessPath, health.readinessPath];
            for (const target of targets) {
                const conflict = routes.find(
                    (route) => route.method === "GET" && route.pattern === target,
                );
                if (conflict !== undefined) {
                    throw new Error(`Sloppy route 'GET ${target}' is already registered.`);
                }
            }

            registerRoute(
                routes,
                routeHost,
                assertAppMutable,
                currentModule,
                "GET",
                health.path,
                {
                    health: "aggregate",
                    checks: healthCheckNames(health.checks, () => true),
                },
                createHealthHandler(health.checks, "aggregate"),
                undefined,
                middleware,
                corsPolicy,
            ).withName("Health");

            registerRoute(
                routes,
                routeHost,
                assertAppMutable,
                currentModule,
                "GET",
                health.livenessPath,
                {
                    health: "liveness",
                    checks: healthCheckNames(health.checks, (check) => check.liveness),
                },
                createHealthHandler(health.checks, "liveness"),
                undefined,
                middleware,
                corsPolicy,
            ).withName("Health.Liveness");

            registerRoute(
                routes,
                routeHost,
                assertAppMutable,
                currentModule,
                "GET",
                health.readinessPath,
                {
                    health: "readiness",
                    checks: healthCheckNames(health.checks, (check) => check.readiness),
                },
                createHealthHandler(health.checks, "readiness"),
                undefined,
                middleware,
                corsPolicy,
            ).withName("Health.Readiness");

            return app;
        },

        get(pattern, optionsOrHandler, maybeHandler) {
            return app.mapGet(pattern, optionsOrHandler, maybeHandler);
        },

        post(pattern, optionsOrHandler, maybeHandler) {
            return app.mapPost(pattern, optionsOrHandler, maybeHandler);
        },

        put(pattern, optionsOrHandler, maybeHandler) {
            return app.mapPut(pattern, optionsOrHandler, maybeHandler);
        },

        patch(pattern, optionsOrHandler, maybeHandler) {
            return app.mapPatch(pattern, optionsOrHandler, maybeHandler);
        },

        delete(pattern, optionsOrHandler, maybeHandler) {
            return app.mapDelete(pattern, optionsOrHandler, maybeHandler);
        },

        mapGroup(prefix) {
            assertAppMutable();
            return createRouteGroup(
                routes,
                routeHost,
                assertAppMutable,
                getCurrentModule,
                prefix,
                () => middleware,
                nextMiddlewareSequence,
                getCorsPolicy,
            );
        },

        group(prefix) {
            return app.mapGroup(prefix);
        },

        mapController(prefix, Controller, configure) {
            assertAppMutable();
            const mapper = createControllerMapper(
                routes,
                routeHost,
                assertAppMutable,
                currentModule,
                prefix,
                Controller,
                middleware,
                getCorsPolicy,
            );

            if (configure === undefined) {
                return mapper;
            }
            if (typeof configure !== "function") {
                throw new TypeError("Sloppy app.mapController configure callback must be a function.");
            }
            configure(mapper);
            return app;
        },

        controller(prefix, Controller, configure) {
            return app.mapController(prefix, Controller, configure);
        },

        freeze() {
            guard.freeze();
            return app;
        },

        isFrozen() {
            return guard.isFrozen();
        },

        __getRoutes() {
            return Object.freeze(routes.map(snapshotRoute));
        },

        __debug() {
            return Object.freeze({
                modules: moduleDebugRef.modules,
                workers: Object.freeze(workerResources.map(snapshotWorkerResource)),
            });
        },

        __getModuleGraph() {
            return moduleDebugRef.modules;
        },

        __getPlanContributions() {
            return Object.freeze({
                modules: moduleDebugRef.modules,
                capabilities: host.capabilities.list(),
                auth: snapshotAuthState(authState),
                workers: Object.freeze(workerResources.map(snapshotWorkerResource)),
                errors: errorPolicyState.policy === null ? undefined : Object.freeze({
                    detail: errorPolicyState.policy.detail,
                    missingRoute: errorPolicyState.policy.missingRoute,
                    mappings: errorPolicyState.mappings.length,
                }),
            });
        },

        __getErrorPolicy() {
            return errorPolicyState.policy === null ? undefined : Object.freeze({
                detail: errorPolicyState.policy.detail,
                missingRoute: errorPolicyState.policy.missingRoute,
                maxBodyBytes: errorPolicyState.policy.maxBodyBytes,
                mappings: errorPolicyState.mappings.length,
            });
        },

        __handleErrorStatus(status, context = undefined) {
            if (errorPolicyState.policy === null) {
                return undefined;
            }
            return standardProblemResult(status, context, errorPolicyState, host.config);
        },

        __getSerializationOptions() {
            return Object.freeze({
                json: jsonOptions,
                contentNegotiation,
            });
        },

        __runInModule(moduleName, callback) {
            assertAppMutable();

            const previousModule = currentModule;
            currentModule = moduleName;

            try {
                return callback(app);
            } finally {
                currentModule = previousModule;
            }
        },

    };

    return Object.freeze(app);
}


function createBuilder() {
    const options = arguments[0];
    const appOptions = normalizeAppOptions(options);
    const guard = createMutationGuard("builder");
    const config = createConfigBuilder(guard);
    const logging = createLoggingBuilder(guard);
    const capabilities = createCapabilityRegistry(guard);
    const services = createServicesBuilder(guard);
    const modules = [];
    const moduleNames = new Set();

    const builder = {
        config,
        logging,
        capabilities,
        services,

        addModule(module) {
            guard.assertMutable();

            const state = requireModuleState(module);

            if (moduleNames.has(state.name)) {
                throw new Error(`Sloppy module '${state.name}' is already registered.`);
            }

            state.finalized = true;
            modules.push(state);
            moduleNames.add(state.name);
            return builder;
        },

        build() {
            guard.assertMutable();

            const orderedModules = resolveModuleOrder(modules);

            for (const state of orderedModules) {
                for (const callback of state.capabilityCallbacks) {
                    capabilities.__runInModule(state.name, (capabilityRegistry) => {
                        runModulePhase(state, "capabilities", callback, capabilityRegistry);
                    });
                }
            }

            for (const state of orderedModules) {
                for (const callback of state.serviceCallbacks) {
                    services.__runInModule(state.name, (servicesBuilder) => {
                        runModulePhase(state, "services", callback, servicesBuilder);
                    });
                }
            }

            const capabilitySnapshot = capabilities.__snapshot();
            const serviceSnapshot = services.__snapshot();

            guard.freeze();

            const moduleDebugRef = {
                modules: Object.freeze([]),
            };

            const app = createApp(Object.freeze({
                config: createConfigProvider(config.__snapshot()),
                log: createLogger(logging.__snapshot()),
                capabilities: createCapabilityProvider(capabilitySnapshot),
                services: createServiceProvider(serviceSnapshot, createCapabilityProvider(capabilitySnapshot)),
                moduleDebugRef,
                options: appOptions,
            }));

            for (const state of orderedModules) {
                for (const callback of state.routeCallbacks) {
                    app.__runInModule(state.name, (moduleApp) => {
                        runModulePhase(state, "routes", callback, moduleApp);
                    });
                }
            }

            moduleDebugRef.modules = createModuleDebugEntries(
                orderedModules,
                capabilitySnapshot,
                serviceSnapshot,
                app.__getRoutes(),
            );
            return app;
        },
    };

    return Object.freeze(builder);
}

function create() {
    const options = arguments[0];
    return createBuilder(options).build();
}

export const Sloppy = Object.freeze({
    create,
    createBuilder,
    module: createModule,
});

export const Router = Object.freeze({
    group: createRouterGroup,
});
