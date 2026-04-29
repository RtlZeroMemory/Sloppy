const LOG_LEVEL_RANK = Object.freeze({
    trace: 0,
    debug: 1,
    info: 2,
    warn: 3,
    error: 4,
});
const MEMORY_SINK_STATE = new WeakMap();

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

    function addRegistration(token, registration) {
        guard.assertMutable();
        validateServiceToken(token);

        if (registrations.has(token)) {
            throw new Error(`Sloppy service '${token}' is already registered.`);
        }

        registrations.set(token, registration);
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

function registerRoute(routes, host, assertAppMutable, pattern, optionsOrHandler, maybeHandler, metadataBase) {
    const args = normalizeMapGetArguments(pattern, optionsOrHandler, maybeHandler);

    assertAppMutable();
    validatePattern(args.pattern);
    validateHandler(args.handler);

    const route = {
        method: "GET",
        pattern: args.pattern,
        handler: createRouteHandler(host, args.handler),
        name: null,
        metadata: metadataBase ? mergeRouteMetadata(metadataBase, args.metadata) : createRouteMetadata(args.metadata),
    };

    routes.push(route);
    return createEndpointBuilder(route, assertAppMutable);
}

function createRouteGroup(routes, host, assertAppMutable, prefix) {
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
            return registerRoute(routes, host, assertAppMutable, fullPattern, optionsOrHandler, maybeHandler, {
                prefix: groupMetadata.prefix,
                tags: groupMetadata.tags,
                name: groupMetadata.name,
            });
        },
    };

    return Object.freeze(group);
}

function createApp(host) {
    const routes = [];
    const guard = createMutationGuard("app");

    function assertAppMutable() {
        guard.assertMutable();
    }

    const app = {
        config: host.config,
        log: host.log,
        services: host.services,

        mapGet(pattern, optionsOrHandler, maybeHandler) {
            return registerRoute(routes, host, assertAppMutable, pattern, optionsOrHandler, maybeHandler);
        },

        mapGroup(prefix) {
            assertAppMutable();
            return createRouteGroup(routes, host, assertAppMutable, prefix);
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
    };

    return Object.freeze(app);
}

function createBuilder() {
    const guard = createMutationGuard("builder");
    const config = createConfigBuilder(guard);
    const logging = createLoggingBuilder(guard);
    const services = createServicesBuilder(guard);

    const builder = {
        config,
        logging,
        services,

        build() {
            guard.assertMutable();
            guard.freeze();

            return createApp(Object.freeze({
                config: createConfigProvider(config.__snapshot()),
                log: createLogger(logging.__snapshot()),
                services: createServiceProvider(services.__snapshot()),
            }));
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
});
