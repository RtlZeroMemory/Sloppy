import { defineFunctionModuleName } from "./modules.js";
import { cleanupAfterFailure, finishWithCleanup, validateServiceToken } from "./services.js";
import { isPlainObject } from "./shared.js";

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

function createHandlerContext(host) {
    return Object.freeze({
        services: host.services.createScope(),
        capabilities: host.capabilities,
        config: host.config,
        log: host.log,
        route: Object.freeze({}),
    });
}

function handleRouteError(host, error) {
    if (typeof host.handleError !== "function") {
        throw error;
    }
    return host.handleError(error);
}

function finishRouteError(host, error, cleanup) {
    try {
        return finishWithCleanup(handleRouteError(host, error), cleanup);
    } catch (handledError) {
        return cleanupAfterFailure(handledError, cleanup);
    }
}

function createRouteHandler(host, handler) {
    return function routeHandler(context) {
        if (context !== undefined && context !== null) {
            try {
                const result = handler(context);
                if (result !== null && typeof result === "object" && typeof result.then === "function") {
                    return Promise.resolve(result).catch((error) => handleRouteError(host, error));
                }
                return result;
            } catch (error) {
                return handleRouteError(host, error);
            }
        }

        const ownedContext = createHandlerContext(host);
        try {
            const result = handler(ownedContext);
            if (result !== null && typeof result === "object" && typeof result.then === "function") {
                return Promise.resolve(result).then(
                    (value) => finishWithCleanup(value, () => ownedContext.services.dispose()),
                    (error) => finishRouteError(host, error, () => ownedContext.services.dispose()),
                );
            }
            return finishWithCleanup(result, () => ownedContext.services.dispose());
        } catch (error) {
            return finishRouteError(host, error, () => ownedContext.services.dispose());
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

export {
    createControllerMapper,
    createRouteGroup,
    createRouterGroup,
    registerRoute,
    snapshotRoute,
};
