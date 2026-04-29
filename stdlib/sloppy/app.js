const LOG_LEVEL_RANK = Object.freeze({
    trace: 0,
    debug: 1,
    info: 2,
    warn: 3,
    error: 4,
});
const MEMORY_SINK_STATE = new WeakMap();
const MODULE_STATE = new WeakMap();
const MODULE_NAME_PATTERN = /^[a-z][a-z0-9.-]*$/u;

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }

    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function validateConfigKey(key) {
    if (typeof key !== "string" || key.length === 0) {
        throw new TypeError("Sloppy config key must be a non-empty string.");
    }
}

function validateLogLevel(level) {
    if (!Object.prototype.hasOwnProperty.call(LOG_LEVEL_RANK, level)) {
        throw new TypeError("Sloppy log level must be one of trace, debug, info, warn, or error.");
    }
}

function validateServiceToken(token) {
    if (typeof token !== "string" || token.length === 0) {
        throw new TypeError("Sloppy service token must be a non-empty string.");
    }
}

function validateModuleName(name) {
    if (typeof name !== "string" || name.length === 0) {
        throw new TypeError("Sloppy module name must be a non-empty string.");
    }

    if (!MODULE_NAME_PATTERN.test(name)) {
        throw new TypeError(
            "Sloppy module name must start with a lowercase letter and contain only lowercase letters, digits, dots, or hyphens.",
        );
    }
}

function validateModuleMetadataKey(key) {
    if (typeof key !== "string" || key.length === 0) {
        throw new TypeError("Sloppy module metadata key must be a non-empty string.");
    }
}

function validateModulePhaseCallback(callback, phase) {
    if (typeof callback !== "function") {
        throw new TypeError(`Sloppy module ${phase} phase callback must be a function.`);
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

function createMutationGuard(subject) {
    let frozen = false;

    return Object.freeze({
        assertMutable() {
            if (frozen) {
                throw new Error(`Sloppy ${subject} is frozen and cannot be modified.`);
            }
        },
        freeze() {
            frozen = true;
        },
        isFrozen() {
            return frozen;
        },
    });
}

function snapshotModuleMetadata(metadata) {
    return Object.freeze({ ...metadata });
}

function getModuleState(module) {
    return MODULE_STATE.get(module);
}

function requireModuleState(module) {
    const state = getModuleState(module);

    if (state === undefined) {
        throw new TypeError(
            "Sloppy builder.addModule expected a module created by Sloppy.module(name).",
        );
    }

    return state;
}

function createModule(name) {
    validateModuleName(name);

    const state = {
        name,
        dependencies: [],
        serviceCallbacks: [],
        routeCallbacks: [],
        metadata: Object.create(null),
        finalized: false,
    };

    function assertMutable() {
        if (state.finalized) {
            throw new Error(`Sloppy module '${state.name}' is frozen and cannot be modified.`);
        }
    }

    const module = {
        get name() {
            return state.name;
        },

        get dependencies() {
            return Object.freeze([...state.dependencies]);
        },

        dependsOn(...names) {
            assertMutable();

            for (const dependency of names) {
                validateModuleName(dependency);

                if (!state.dependencies.includes(dependency)) {
                    state.dependencies.push(dependency);
                }
            }

            return module;
        },

        services(callback) {
            assertMutable();
            validateModulePhaseCallback(callback, "services");
            state.serviceCallbacks.push(callback);
            return module;
        },

        routes(callback) {
            assertMutable();
            validateModulePhaseCallback(callback, "routes");
            state.routeCallbacks.push(callback);
            return module;
        },

        metadata(key, value) {
            assertMutable();
            validateModuleMetadataKey(key);
            state.metadata[key] = value;
            return module;
        },

        __debug() {
            return Object.freeze({
                name: state.name,
                dependencies: Object.freeze([...state.dependencies]),
                services: state.serviceCallbacks.length,
                routes: state.routeCallbacks.length,
                metadata: snapshotModuleMetadata(state.metadata),
                finalized: state.finalized,
            });
        },
    };

    const frozenModule = Object.freeze(module);
    MODULE_STATE.set(frozenModule, state);
    return frozenModule;
}

function createConfigBuilder(guard) {
    const values = Object.create(null);

    const config = {
        addObject(object) {
            guard.assertMutable();

            if (!isPlainObject(object)) {
                throw new TypeError("Sloppy config.addObject value must be a plain object.");
            }

            for (const [key, value] of Object.entries(object)) {
                validateConfigKey(key);
                values[key] = value;
            }

            return config;
        },

        get(key, fallback) {
            validateConfigKey(key);
            return Object.prototype.hasOwnProperty.call(values, key) ? values[key] : fallback;
        },

        has(key) {
            validateConfigKey(key);
            return Object.prototype.hasOwnProperty.call(values, key);
        },

        require(key) {
            validateConfigKey(key);

            if (!Object.prototype.hasOwnProperty.call(values, key)) {
                throw new Error(`Sloppy config key '${key}' is required but was not provided.`);
            }

            return values[key];
        },

        __snapshot() {
            return Object.freeze({ ...values });
        },
    };

    return Object.freeze(config);
}

function createConfigProvider(snapshot) {
    return Object.freeze({
        get(key, fallback) {
            validateConfigKey(key);
            return Object.prototype.hasOwnProperty.call(snapshot, key) ? snapshot[key] : fallback;
        },

        has(key) {
            validateConfigKey(key);
            return Object.prototype.hasOwnProperty.call(snapshot, key);
        },

        require(key) {
            validateConfigKey(key);

            if (!Object.prototype.hasOwnProperty.call(snapshot, key)) {
                throw new Error(`Sloppy config key '${key}' is required but was not provided.`);
            }

            return snapshot[key];
        },
    });
}

function snapshotLogEntry(entry) {
    return Object.freeze({
        level: entry.level,
        message: entry.message,
        fields: entry.fields,
    });
}

function createLoggingBuilder(guard) {
    const memorySinks = [];
    let minimumLevel = "info";

    const logging = {
        setMinimumLevel(level) {
            guard.assertMutable();
            validateLogLevel(level);
            minimumLevel = level;
            return logging;
        },

        addMemorySink() {
            guard.assertMutable();

            const state = {
                entries: [],
            };

            const sink = Object.freeze({
                entries() {
                    return Object.freeze(state.entries.map(snapshotLogEntry));
                },
            });

            MEMORY_SINK_STATE.set(sink, state);
            memorySinks.push(sink);
            return sink;
        },

        __snapshot() {
            return Object.freeze({
                minimumLevel,
                memorySinks: Object.freeze([...memorySinks]),
            });
        },
    };

    return Object.freeze(logging);
}

function createLogger(snapshot) {
    function write(level, message, fields) {
        validateLogLevel(level);

        if (LOG_LEVEL_RANK[level] < LOG_LEVEL_RANK[snapshot.minimumLevel]) {
            return;
        }

        const entry = Object.freeze({
            level,
            message: String(message),
            fields,
        });

        for (const sink of snapshot.memorySinks) {
            MEMORY_SINK_STATE.get(sink).entries.push(entry);
        }
    }

    return Object.freeze({
        trace(message, fields) {
            write("trace", message, fields);
        },
        debug(message, fields) {
            write("debug", message, fields);
        },
        info(message, fields) {
            write("info", message, fields);
        },
        warn(message, fields) {
            write("warn", message, fields);
        },
        error(message, fields) {
            write("error", message, fields);
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

function createServiceProvider(registrations) {
    function resolve(scope, token) {
        validateServiceToken(token);

        if (!registrations.has(token)) {
            throw new Error(`Sloppy service '${token}' is not registered.`);
        }

        const registration = registrations.get(token);

        if (registration.lifetime === "singleton") {
            if (!registration.initialized) {
                registration.value = registration.factory(scope);
                registration.initialized = true;
            }

            return registration.value;
        }

        return registration.factory(scope);
    }

    function createScope() {
        const scope = Object.freeze({
            get(token) {
                return resolve(scope, token);
            },
        });

        return scope;
    }

    const provider = Object.freeze({
        get(token) {
            return resolve(provider.createScope(), token);
        },

        createScope,
    });

    return provider;
}

function createHandlerContext(host) {
    return Object.freeze({
        services: host.services.createScope(),
        config: host.config,
        log: host.log,
        route: Object.freeze({}),
    });
}

function createRouteHandler(host, handler) {
    return function routeHandler(context) {
        return handler(context ?? createHandlerContext(host));
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

function normalizeMapGetArguments(pattern, optionsOrHandler, maybeHandler) {
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
    pattern,
    optionsOrHandler,
    maybeHandler,
    metadataBase,
) {
    const args = normalizeMapGetArguments(pattern, optionsOrHandler, maybeHandler);

    assertAppMutable();
    validatePattern(args.pattern);
    validateHandler(args.handler);

    const route = {
        method: "GET",
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

function createRouteGroup(routes, host, assertAppMutable, getCurrentModule, prefix) {
    const groupMetadata = {
        prefix: normalizeGroupPrefix(prefix),
        tags: [],
        name: null,
    };

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

        mapGet(pattern, optionsOrHandler, maybeHandler) {
            const fullPattern = composeRoutePattern(groupMetadata.prefix, pattern);
            return registerRoute(
                routes,
                host,
                assertAppMutable,
                getCurrentModule(),
                fullPattern,
                optionsOrHandler,
                maybeHandler,
                {
                    prefix: groupMetadata.prefix,
                    tags: groupMetadata.tags,
                    name: groupMetadata.name,
                },
            );
        },
    };

    return Object.freeze(group);
}

function createApp(host) {
    const routes = [];
    const guard = createMutationGuard("app");
    let currentModule = null;
    const moduleDebugRef = host.moduleDebugRef ?? { modules: Object.freeze([]) };

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

        mapGet(pattern, optionsOrHandler, maybeHandler) {
            return registerRoute(
                routes,
                host,
                assertAppMutable,
                currentModule,
                pattern,
                optionsOrHandler,
                maybeHandler,
            );
        },

        mapGroup(prefix) {
            assertAppMutable();
            return createRouteGroup(routes, host, assertAppMutable, getCurrentModule, prefix);
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
            });
        },

        __getModuleGraph() {
            return moduleDebugRef.modules;
        },

        __getPlanContributions() {
            return Object.freeze({
                modules: moduleDebugRef.modules,
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

function createModuleDependencyMissingError(moduleName, dependencyName) {
    return new Error(`sloppy: module dependency missing

Module:
  ${moduleName}

Missing dependency:
  ${dependencyName}

Fix:
  builder.addModule(/* module named '${dependencyName}' */) before build()`);
}

function createModuleCycleError(cycle) {
    return new Error(`sloppy: module dependency cycle detected

Cycle:
  ${cycle.join(" -> ")}

Fix:
  Remove one dependsOn(...) edge from the cycle.`);
}

function createModulePhaseError(moduleName, phase, cause) {
    const detail = cause instanceof Error ? cause.message : String(cause);
    const error = new Error(`sloppy: module phase failed

Module:
  ${moduleName}

Phase:
  ${phase}

Reason:
  ${detail}`);

    error.cause = cause;
    return error;
}

function resolveModuleOrder(moduleStates) {
    const byName = new Map();

    for (const state of moduleStates) {
        byName.set(state.name, state);
    }

    for (const state of moduleStates) {
        for (const dependency of state.dependencies) {
            if (!byName.has(dependency)) {
                throw createModuleDependencyMissingError(state.name, dependency);
            }
        }
    }

    const ordered = [];
    const visitState = new Map();
    const stack = [];

    function visit(state) {
        const currentVisitState = visitState.get(state.name);

        if (currentVisitState === "done") {
            return;
        }

        if (currentVisitState === "visiting") {
            const start = stack.indexOf(state.name);
            const cycle = [...stack.slice(start), state.name];
            throw createModuleCycleError(cycle);
        }

        visitState.set(state.name, "visiting");
        stack.push(state.name);

        for (const dependency of state.dependencies) {
            visit(byName.get(dependency));
        }

        stack.pop();
        visitState.set(state.name, "done");
        ordered.push(state);
    }

    for (const state of moduleStates) {
        visit(state);
    }

    return ordered;
}

function runModulePhase(state, phase, callback, target) {
    try {
        return callback(target);
    } catch (error) {
        throw createModulePhaseError(state.name, phase, error);
    }
}

function buildServiceContributions(serviceSnapshot) {
    const byModule = new Map();

    for (const [token, registration] of serviceSnapshot.entries()) {
        if (registration.module === null) {
            continue;
        }

        if (!byModule.has(registration.module)) {
            byModule.set(registration.module, []);
        }

        byModule.get(registration.module).push(token);
    }

    for (const tokens of byModule.values()) {
        tokens.sort();
    }

    return byModule;
}

function buildRouteContributions(routes) {
    const byModule = new Map();

    for (const route of routes) {
        const moduleName = route.metadata.module;

        if (moduleName === undefined) {
            continue;
        }

        if (!byModule.has(moduleName)) {
            byModule.set(moduleName, []);
        }

        byModule.get(moduleName).push(`${route.method} ${route.pattern}`);
    }

    return byModule;
}

function createModuleDebugEntries(orderedModules, serviceSnapshot, routes) {
    const servicesByModule = buildServiceContributions(serviceSnapshot);
    const routesByModule = buildRouteContributions(routes);

    return Object.freeze(orderedModules.map((state, index) => {
        const services = Object.freeze([...(servicesByModule.get(state.name) ?? [])]);
        const routeContributions = Object.freeze([...(routesByModule.get(state.name) ?? [])]);
        const contributes = [];

        if (services.length > 0) {
            contributes.push("services");
        }

        if (routeContributions.length > 0) {
            contributes.push("routes");
        }

        if (Object.keys(state.metadata).length > 0) {
            contributes.push("metadata");
        }

        return Object.freeze({
            name: state.name,
            dependencies: Object.freeze([...state.dependencies]),
            order: index,
            contributes: Object.freeze(contributes),
            services,
            routes: routeContributions,
            metadata: snapshotModuleMetadata(state.metadata),
        });
    }));
}

function createBuilder() {
    const guard = createMutationGuard("builder");
    const config = createConfigBuilder(guard);
    const logging = createLoggingBuilder(guard);
    const services = createServicesBuilder(guard);
    const modules = [];
    const moduleNames = new Set();

    const builder = {
        config,
        logging,
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
                for (const callback of state.serviceCallbacks) {
                    services.__runInModule(state.name, (servicesBuilder) => {
                        runModulePhase(state, "services", callback, servicesBuilder);
                    });
                }
            }

            const serviceSnapshot = services.__snapshot();

            guard.freeze();

            const moduleDebugRef = {
                modules: Object.freeze([]),
            };

            const app = createApp(Object.freeze({
                config: createConfigProvider(config.__snapshot()),
                log: createLogger(logging.__snapshot()),
                services: createServiceProvider(serviceSnapshot),
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
