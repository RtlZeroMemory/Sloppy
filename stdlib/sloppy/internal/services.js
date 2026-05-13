import { isPromiseLike } from "./shared.js";
import { Cache, isCache } from "../cache.js";

function validateServiceToken(token) {
    if (typeof token !== "string" || token.length === 0) {
        throw new TypeError("Sloppy service token must be a non-empty string.");
    }
}

function cacheServiceToken(name = "default") {
    return Cache.token(name);
}

function validateCacheInstance(cache) {
    if (!isCache(cache) || typeof cache.stats !== "function") {
        throw new TypeError("Sloppy services.addCache expects a Cache instance.");
    }
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

        addCache(cache, name = undefined) {
            guard.assertMutable();
            validateCacheInstance(cache);
            const cacheName = name ?? cache.name ?? "default";
            return addRegistration(cacheServiceToken(cacheName), {
                lifetime: "singleton",
                factory: null,
                value: cache,
                initialized: true,
            });
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

    function createRootScope() {
        const resolving = [];
        const resolvingLifetimes = [];
        const scope = Object.freeze({
            capabilities,
            get(token) {
                return resolve(scope, token);
            },
            tryGet(token) {
                validateServiceToken(token);
                if (providerDisposed) {
                    throw new Error("Sloppy service provider is disposed.");
                }
                if (!registrations.has(token)) {
                    return undefined;
                }
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
        const scopeOwnedValues = [];
        const resolving = [];
        const resolvingLifetimes = [];
        let disposed = false;

        const scope = Object.freeze({
            capabilities,
            get(token) {
                return resolve(scope, token);
            },
            tryGet(token) {
                validateServiceToken(token);
                if (providerDisposed) {
                    throw new Error("Sloppy service provider is disposed.");
                }
                if (disposed) {
                    throw new Error("Sloppy service scope is disposed.");
                }
                if (!registrations.has(token)) {
                    return undefined;
                }
                return resolve(scope, token);
            },
            dispose() {
                if (disposed) {
                    return undefined;
                }
                disposed = true;
                return disposeValues([...scopeOwnedValues].reverse(), "Sloppy service scope disposal failed.");
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
                scopeOwnedValues.push(value);
            },
            __trackTransient(value) {
                scopeOwnedValues.push(value);
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

        tryGet(token) {
            validateServiceToken(token);
            if (providerDisposed) {
                throw new Error("Sloppy service provider is disposed.");
            }
            const registration = registrations.get(token);
            if (registration === undefined) {
                return undefined;
            }
            if (registration.lifetime !== "singleton") {
                throw new Error(`Sloppy root service resolution only supports singleton services; create a scope to resolve '${token}'.`);
            }
            return resolve(rootScope, token);
        },

        createScope,

        addCache(cache, name = undefined) {
            validateCacheInstance(cache);
            if (providerDisposed) {
                throw new Error("Sloppy service provider is disposed.");
            }
            const token = cacheServiceToken(name ?? cache.name ?? "default");
            if (registrations.has(token)) {
                throw new Error(`Sloppy service '${token}' is already registered.`);
            }
            registrations.set(token, {
                lifetime: "singleton",
                factory: null,
                value: cache,
                initialized: true,
            });
            singletonDisposables.push(cache);
            return provider;
        },

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

export {
    cleanupAfterFailure,
    createServiceProvider,
    createServicesBuilder,
    finishWithCleanup,
    validateServiceToken,
};
