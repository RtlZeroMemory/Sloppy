import {
    createCapabilityProvider,
    createCapabilityRegistry,
} from "./internal/capabilities.js";
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
import { Results } from "./results.js";

const DEFAULT_HEALTH_PATH = "/health";
const DEFAULT_LIVENESS_PATH = "/health/live";
const DEFAULT_READINESS_PATH = "/health/ready";

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
    return isPlainObject(value) && value.__sloppyProblemDetails === true;
}

function safeProblemDetails(error, descriptor, config) {
    const environment = String(config.get("Sloppy:Environment", config.get("Environment", "")));
    const includeDetail = descriptor.detail === "always" ||
        (descriptor.detail === "development" && environment.toLowerCase() === "development");
    const problem = {
        status: 500,
        title: "Internal Server Error",
        code: "SLOPPY_E_HANDLER_ERROR",
    };

    if (includeDetail) {
        problem.detail = String(error?.message ?? error);
    }

    return Results.problem(problem, { status: 500 });
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
    let problemDetails = null;
    const routeHost = {
        ...host,
        handleError(error) {
            if (problemDetails === null) {
                throw error;
            }
            return safeProblemDetails(error, problemDetails, host.config);
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

        use(provider) {
            assertAppMutable();
            if (isProblemDetailsDescriptor(provider)) {
                problemDetails = provider;
                return app;
            }
            if (typeof provider === "function") {
                middleware.push({ fn: provider, sequence: nextMiddlewareSequence() });
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

        useCors(policy) {
            assertAppMutable();
            corsPolicy = normalizeCorsPolicy(policy);
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
                workers: Object.freeze(workerResources.map(snapshotWorkerResource)),
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
    return createBuilder().build();
}

export const Sloppy = Object.freeze({
    create,
    createBuilder,
    module: createModule,
});

export const Router = Object.freeze({
    group: createRouterGroup,
});
