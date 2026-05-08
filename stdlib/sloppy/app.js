import { createConfigBuilder, createConfigProvider } from "./internal/config.js";
import { createLogger, createLoggingBuilder } from "./internal/logging.js";
import {
    assertRouteOnlyModule,
    createModule,
    createModuleDebugEntries,
    defineFunctionModuleName,
    functionModuleName,
    getModuleState,
    requireModuleState,
    resolveModuleOrder,
    runModulePhase,
} from "./internal/modules.js";
import { createMutationGuard, isPlainObject, isPromiseLike } from "./internal/shared.js";

const DATABASE_ACCESS_MODES = Object.freeze(["read", "write", "readwrite"]);

function validateServiceToken(token) {
    if (typeof token !== "string" || token.length === 0) {
        throw new TypeError("Sloppy service token must be a non-empty string.");
    }
}

function validateCapabilityToken(token) {
    if (typeof token !== "string" || token.length === 0) {
        throw new TypeError("Sloppy capability token must be a non-empty string.");
    }
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

function validateDatabaseCapabilityOptions(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("Sloppy database capability options must be a plain object.");
    }

    if (typeof options.provider !== "string" || options.provider.length === 0) {
        throw new TypeError("Sloppy database capability provider must be a non-empty string.");
    }

    if (!DATABASE_ACCESS_MODES.includes(options.access)) {
        throw new TypeError("Sloppy database capability access must be read, write, or readwrite.");
    }
}


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





function snapshotCapability(capability) {
    return Object.freeze({
        token: capability.token,
        kind: capability.kind,
        provider: capability.provider,
        access: capability.access,
        module: capability.module,
        metadata: Object.freeze({ ...capability.metadata }),
    });
}

function createCapabilityRegistry(guard) {
    const capabilities = new Map();
    let currentModule = null;

    function addCapability(token, capability) {
        guard.assertMutable();
        validateCapabilityToken(token);

        if (capabilities.has(token)) {
            throw new Error(`sloppy: capability token already declared

Token:
  ${token}

Fix:
  Declare each capability token once, or choose a more specific token such as 'data.main'.`);
        }

        capabilities.set(token, {
            ...capability,
            token,
            module: currentModule,
        });

        return registry;
    }

    const registry = {
        addDatabase(token, options) {
            validateDatabaseCapabilityOptions(options);

            return addCapability(token, {
                kind: "database",
                provider: options.provider,
                access: options.access,
                metadata: { ...options },
            });
        },

        has(token) {
            validateCapabilityToken(token);
            return capabilities.has(token);
        },

        get(token) {
            validateCapabilityToken(token);

            if (!capabilities.has(token)) {
                throw new Error(`sloppy: capability token is not declared

Token:
  ${token}

Fix:
  Add builder.capabilities.addDatabase(...) or a module .capabilities(...) declaration before build().`);
            }

            return snapshotCapability(capabilities.get(token));
        },

        list() {
            return Object.freeze(Array.from(capabilities.values(), snapshotCapability));
        },

        __snapshot() {
            return new Map(Array.from(capabilities.entries(), ([token, capability]) => [
                token,
                { ...capability, metadata: { ...capability.metadata } },
            ]));
        },

        __runInModule(moduleName, callback) {
            const previousModule = currentModule;
            currentModule = moduleName;

            try {
                return callback(registry);
            } finally {
                currentModule = previousModule;
            }
        },
    };

    return Object.freeze(registry);
}

function createCapabilityProvider(capabilitySnapshot) {
    return Object.freeze({
        has(token) {
            validateCapabilityToken(token);
            return capabilitySnapshot.has(token);
        },

        get(token) {
            validateCapabilityToken(token);

            if (!capabilitySnapshot.has(token)) {
                throw new Error(`sloppy: capability token is not declared

Token:
  ${token}

Fix:
  Check app.capabilities.list() or declare the capability before build().`);
            }

            return snapshotCapability(capabilitySnapshot.get(token));
        },

        list() {
            return Object.freeze(Array.from(capabilitySnapshot.values(), snapshotCapability));
        },
    });
}

function createServicesBuilder(guard) {
    const registrations = new Map();
    let currentModule = null;

    function addRegistration(token, registration) {
        guard.assertMutable();
        validateServiceToken(token);

        if (registrations.has(token)) {
            throw new Error(`Sloppy service '${token}' is already registered.`);
        }

        registrations.set(token, {
            ...registration,
            module: currentModule,
        });
        return services;
    }

    const services = {
        addSingleton(token, factoryOrValue) {
            const registration = {
                lifetime: "singleton",
                factory: typeof factoryOrValue === "function" ? factoryOrValue : null,
                value: factoryOrValue,
                initialized: typeof factoryOrValue !== "function",
            };

            return addRegistration(token, registration);
        },

        addTransient(token, factory) {
            guard.assertMutable();

            if (typeof factory !== "function") {
                throw new TypeError("Sloppy transient service factory must be a function.");
            }

            return addRegistration(token, {
                lifetime: "transient",
                factory,
            });
        },

        addScoped(token, factory) {
            guard.assertMutable();

            if (typeof factory !== "function") {
                throw new TypeError("Sloppy scoped service factory must be a function.");
            }

            return addRegistration(token, {
                lifetime: "scoped",
                factory,
            });
        },

        __snapshot() {
            return new Map(Array.from(registrations.entries(), ([token, registration]) => [
                token,
                { ...registration },
            ]));
        },

        __runInModule(moduleName, callback) {
            const previousModule = currentModule;
            currentModule = moduleName;

            try {
                return callback(services);
            } finally {
                currentModule = previousModule;
            }
        },
    };

    return Object.freeze(services);
}


function combineCleanupErrors(primary, cleanup) {
    return new AggregateError([primary, cleanup], "Sloppy cleanup failed after a handler error.");
}

function cleanupAfterSuccess(result, cleanup) {
    const cleanupResult = cleanup();
    if (isPromiseLike(cleanupResult)) {
        return Promise.resolve(cleanupResult).then(() => result);
    }
    return result;
}

function cleanupAfterFailure(error, cleanup) {
    try {
        const cleanupResult = cleanup();
        if (isPromiseLike(cleanupResult)) {
            return Promise.resolve(cleanupResult).then(
                () => {
                    throw error;
                },
                (cleanupError) => {
                    throw combineCleanupErrors(error, cleanupError);
                },
            );
        }
    } catch (cleanupError) {
        throw combineCleanupErrors(error, cleanupError);
    }
    throw error;
}

function finishWithCleanup(result, cleanup) {
    if (isPromiseLike(result)) {
        return Promise.resolve(result).then(
            (value) => cleanupAfterSuccess(value, cleanup),
            (error) => cleanupAfterFailure(error, cleanup),
        );
    }
    return cleanupAfterSuccess(result, cleanup);
}

function createServiceProvider(registrations, capabilities) {
    const singletonDisposables = [];
    let providerDisposed = false;

    function disposeValue(value) {
        if (value === null || value === undefined) {
            return undefined;
        }
        if (typeof value[Symbol.dispose] === "function") {
            return value[Symbol.dispose]();
        }
        if (typeof value.dispose === "function") {
            return value.dispose();
        }
        if (typeof value.close === "function") {
            return value.close();
        }
        return undefined;
    }

    function isPromiseLike(value) {
        return value !== null && typeof value === "object" && typeof value.then === "function";
    }

    function disposalError(errors, message) {
        if (errors.length === 1) {
            return errors[0];
        }
        return new AggregateError(errors, message);
    }

    function disposeValues(values, message) {
        const errors = [];
        const pending = [];
        for (const value of values) {
            try {
                const result = disposeValue(value);
                if (isPromiseLike(result)) {
                    pending.push(Promise.resolve(result).catch((error) => {
                        errors.push(error);
                    }));
                }
            } catch (error) {
                errors.push(error);
            }
        }

        if (pending.length === 0) {
            if (errors.length !== 0) {
                throw disposalError(errors, message);
            }
            return undefined;
        }

        return Promise.all(pending).then(() => {
            if (errors.length !== 0) {
                throw disposalError(errors, message);
            }
            return undefined;
        });
    }

    function combineCleanupErrors(primary, cleanup) {
        return new AggregateError([primary, cleanup], "Sloppy cleanup failed after a handler error.");
    }

    function cleanupAfterSuccess(result, cleanup) {
        const cleanupResult = cleanup();
        if (isPromiseLike(cleanupResult)) {
            return Promise.resolve(cleanupResult).then(() => result);
        }
        return result;
    }

    function cleanupAfterFailure(error, cleanup) {
        try {
            const cleanupResult = cleanup();
            if (isPromiseLike(cleanupResult)) {
                return Promise.resolve(cleanupResult).then(
                    () => {
                        throw error;
                    },
                    (cleanupError) => {
                        throw combineCleanupErrors(error, cleanupError);
                    },
                );
            }
        } catch (cleanupError) {
            throw combineCleanupErrors(error, cleanupError);
        }
        throw error;
    }

    function finishWithCleanup(result, cleanup) {
        if (isPromiseLike(result)) {
            return Promise.resolve(result).then(
                (value) => cleanupAfterSuccess(value, cleanup),
                (error) => cleanupAfterFailure(error, cleanup),
            );
        }
        return cleanupAfterSuccess(result, cleanup);
    }

    function createRootScope() {
        const resolving = [];
        const resolvingLifetimes = [];
        const scope = Object.freeze({
            capabilities,
            get(token) {
                return resolve(scope, token);
            },
            __disposed() {
                return false;
            },
            __hasScoped() {
                return false;
            },
            __getScoped() {
                return undefined;
            },
            __setScoped() {
                throw new Error("Sloppy root service scope cannot store scoped services.");
            },
            __trackTransient(value) {
                singletonDisposables.push(value);
            },
            __resolving() {
                return resolving;
            },
            __resolvingLifetimes() {
                return resolvingLifetimes;
            },
            __pushResolving(token, lifetime) {
                resolving.push(token);
                resolvingLifetimes.push(lifetime);
            },
            __popResolving() {
                resolving.pop();
                resolvingLifetimes.pop();
            },
        });
        return scope;
    }

    function resolve(scope, token) {
        validateServiceToken(token);

        if (providerDisposed) {
            throw new Error("Sloppy service provider is disposed.");
        }

        if (scope.__disposed()) {
            throw new Error("Sloppy service scope is disposed.");
        }

        if (!registrations.has(token)) {
            throw new Error(`Sloppy service '${token}' is not registered.`);
        }

        const registration = registrations.get(token);

        if (scope.__resolving().includes(token)) {
            throw new Error(`Sloppy service circular dependency detected: ${[...scope.__resolving(), token].join(" -> ")}.`);
        }

        if (
            registration.lifetime === "scoped" &&
            scope.__resolvingLifetimes().includes("singleton")
        ) {
            throw new Error(`Sloppy singleton service cannot depend on scoped service '${token}'.`);
        }

        if (registration.lifetime === "singleton") {
            if (!registration.initialized) {
                rootScope.__pushResolving(token, "singleton");
                try {
                    registration.value = registration.factory(rootScope);
                    singletonDisposables.push(registration.value);
                } finally {
                    rootScope.__popResolving();
                }
                registration.initialized = true;
            }

            return registration.value;
        }

        if (registration.lifetime === "scoped") {
            if (!scope.__hasScoped(token)) {
                scope.__pushResolving(token, "scoped");
                try {
                    scope.__setScoped(token, registration.factory(scope));
                } finally {
                    scope.__popResolving();
                }
            }
            return scope.__getScoped(token);
        }

        scope.__pushResolving(token, "transient");
        try {
            const value = registration.factory(scope);
            scope.__trackTransient(value);
            return value;
        } finally {
            scope.__popResolving();
        }
    }

    function createScope() {
        const scopedValues = new Map();
        const transientValues = [];
        const resolving = [];
        const resolvingLifetimes = [];
        let disposed = false;

        const scope = Object.freeze({
            capabilities,
            get(token) {
                return resolve(scope, token);
            },
            dispose() {
                if (disposed) {
                    return undefined;
                }
                disposed = true;
                const values = [...transientValues.reverse(), ...Array.from(scopedValues.values()).reverse()];
                return disposeValues(values, "Sloppy service scope disposal failed.");
            },
            __disposed() {
                return disposed;
            },
            __hasScoped(token) {
                return scopedValues.has(token);
            },
            __getScoped(token) {
                return scopedValues.get(token);
            },
            __setScoped(token, value) {
                scopedValues.set(token, value);
            },
            __trackTransient(value) {
                transientValues.push(value);
            },
            __resolving() {
                return resolving;
            },
            __resolvingLifetimes() {
                return resolvingLifetimes;
            },
            __pushResolving(token, lifetime) {
                resolving.push(token);
                resolvingLifetimes.push(lifetime);
            },
            __popResolving() {
                resolving.pop();
                resolvingLifetimes.pop();
            },
        });

        return scope;
    }

    const rootScope = createRootScope();

    const provider = Object.freeze({
        get(token) {
            validateServiceToken(token);
            if (providerDisposed) {
                throw new Error("Sloppy service provider is disposed.");
            }
            const registration = registrations.get(token);
            if (registration === undefined) {
                throw new Error(`Sloppy service '${token}' is not registered.`);
            }
            if (registration.lifetime !== "singleton") {
                throw new Error(`Sloppy root service resolution only supports singleton services; create a scope to resolve '${token}'.`);
            }
            return resolve(rootScope, token);
        },

        createScope,

        dispose() {
            if (providerDisposed) {
                return undefined;
            }
            providerDisposed = true;
            return disposeValues(singletonDisposables.reverse(), "Sloppy service provider disposal failed.");
        },
    });

    return provider;
}

function createHandlerContext(host) {
    return Object.freeze({
        services: host.services.createScope(),
        capabilities: host.capabilities,
        config: host.config,
        log: host.log,
        route: Object.freeze({}),
    });
}

function createRouteHandler(host, handler) {
    return function routeHandler(context) {
        if (context !== undefined && context !== null) {
            return handler(context);
        }

        const ownedContext = createHandlerContext(host);
        try {
            const result = handler(ownedContext);
            return finishWithCleanup(result, () => ownedContext.services.dispose());
        } catch (error) {
            return cleanupAfterFailure(error, () => ownedContext.services.dispose());
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

    return Object.freeze(snapshot);
}

function createEndpointBuilder(route, assertAppMutable) {
    const endpoint = {
        withName(name) {
            assertAppMutable();
            validateName(name, "endpoint");

            route.name = name;
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
) {
    const args = normalizeMapArguments(pattern, optionsOrHandler, maybeHandler);

    assertAppMutable();
    validatePattern(args.pattern);
    validateHandler(args.handler);

    if (routes.some((route) => route.method === method && route.pattern === args.pattern)) {
        throw new Error(`Sloppy route '${method} ${args.pattern}' is already registered.`);
    }

    const route = {
        method,
        pattern: args.pattern,
        handler: createRouteHandler(host, args.handler),
        name: null,
        metadata: {
            ...(metadataBase ? mergeRouteMetadata(metadataBase, args.metadata) : createRouteMetadata(args.metadata)),
            ...((currentModule !== null) ? { module: currentModule } : {}),
        },
    };

    routes.push(route);
    return createEndpointBuilder(route, assertAppMutable);
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

function createControllerHandler(host, Controller, action) {
    const inject = controllerInjectionTokens(Controller);
    const prototypeMethod = Controller.prototype?.[action];

    if (typeof prototypeMethod !== "function") {
        throw new TypeError(`Sloppy controller action '${action}' must name a prototype method.`);
    }

    return function controllerHandler(context) {
        let ctx = context ?? createHandlerContext(host);
        let ownsServices = context === undefined || context === null;
        if (ctx.services === undefined || ctx.services === null) {
            ctx = Object.freeze({
                ...ctx,
                services: host.services.createScope(),
            });
            ownsServices = true;
        }
        const services = ctx.services;
        const dependencies = inject.map((token) => services.get(token));
        const instance = new Controller(...dependencies);
        try {
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
) {
    validateGroupPrefix(prefix);
    validateController(Controller);

    function map(method, pattern, action, options) {
        validateControllerAction(action);
        return registerRoute(
            routes,
            host,
            assertAppMutable,
            currentModule,
            method,
            composeRoutePattern(prefix, pattern),
            {
                ...(options ?? {}),
                controller: Controller.name || "AnonymousController",
                action,
            },
            createControllerHandler(host, Controller, action),
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

function createRouteGroup(routes, host, assertAppMutable, getCurrentModule, prefix) {
    const groupMetadata = {
        prefix: normalizeGroupPrefix(prefix),
        tags: [],
        name: null,
    };

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

function createApp(host) {
    const routes = [];
    const workerResources = [];
    const guard = createMutationGuard("app");
    let currentModule = null;
    const moduleDebugRef = host.moduleDebugRef ?? { modules: Object.freeze([]) };
    const directModules = new Set();

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
                host,
                assertAppMutable,
                currentModule,
                "GET",
                pattern,
                optionsOrHandler,
                maybeHandler,
            );
        },

        mapPost(pattern, optionsOrHandler, maybeHandler) {
            return registerRoute(
                routes,
                host,
                assertAppMutable,
                currentModule,
                "POST",
                pattern,
                optionsOrHandler,
                maybeHandler,
            );
        },

        mapPut(pattern, optionsOrHandler, maybeHandler) {
            return registerRoute(
                routes,
                host,
                assertAppMutable,
                currentModule,
                "PUT",
                pattern,
                optionsOrHandler,
                maybeHandler,
            );
        },

        mapPatch(pattern, optionsOrHandler, maybeHandler) {
            return registerRoute(
                routes,
                host,
                assertAppMutable,
                currentModule,
                "PATCH",
                pattern,
                optionsOrHandler,
                maybeHandler,
            );
        },

        mapDelete(pattern, optionsOrHandler, maybeHandler) {
            return registerRoute(
                routes,
                host,
                assertAppMutable,
                currentModule,
                "DELETE",
                pattern,
                optionsOrHandler,
                maybeHandler,
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
            return createRouteGroup(routes, host, assertAppMutable, getCurrentModule, prefix);
        },

        group(prefix) {
            return app.mapGroup(prefix);
        },

        mapController(prefix, Controller, configure) {
            assertAppMutable();
            const mapper = createControllerMapper(
                routes,
                host,
                assertAppMutable,
                currentModule,
                prefix,
                Controller,
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
