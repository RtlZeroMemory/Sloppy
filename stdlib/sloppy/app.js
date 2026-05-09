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
    registerRoute,
    snapshotRoute,
} from "./internal/routes.js";
import { createServiceProvider, createServicesBuilder } from "./internal/services.js";
import { createMutationGuard, isPlainObject } from "./internal/shared.js";
import { Results } from "./results.js";

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
            );
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
