import { isPlainObject } from "./shared.js";

const MODULE_STATE = new WeakMap();
const FUNCTION_MODULE_NAME = "__sloppyModuleName";
const MODULE_NAME_PATTERN = /^[a-z][a-z0-9.-]*$/u;

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

    if (callback.constructor?.name === "AsyncFunction") {
        throw new TypeError(`Sloppy module ${phase} phase callback must be synchronous.`);
    }
}

function snapshotMetadataValue(value) {
    if (Array.isArray(value)) {
        return Object.freeze(value.map(snapshotMetadataValue));
    }

    if (isPlainObject(value)) {
        const snapshot = {};
        for (const [key, entry] of Object.entries(value)) {
            snapshot[key] = snapshotMetadataValue(entry);
        }
        return Object.freeze(snapshot);
    }

    return value;
}

function snapshotModuleMetadata(metadata) {
    const snapshot = {};
    for (const [key, value] of Object.entries(metadata)) {
        snapshot[key] = snapshotMetadataValue(value);
    }
    return Object.freeze(snapshot);
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
        capabilityCallbacks: [],
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

        capabilities(callback) {
            assertMutable();
            validateModulePhaseCallback(callback, "capabilities");
            state.capabilityCallbacks.push(callback);
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
            state.metadata[key] = snapshotMetadataValue(value);
            return module;
        },

        __debug() {
            return Object.freeze({
                name: state.name,
                dependencies: Object.freeze([...state.dependencies]),
                capabilities: state.capabilityCallbacks.length,
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

function assertRouteOnlyModule(state) {
    if (
        state.dependencies.length !== 0 ||
        state.capabilityCallbacks.length !== 0 ||
        state.serviceCallbacks.length !== 0
    ) {
        throw new Error(
            "Sloppy app.useModule only accepts route-only modules; use builder.addModule for dependencies, capabilities, or services.",
        );
    }
}

function functionModuleName(moduleFactory) {
    return moduleFactory[FUNCTION_MODULE_NAME] ?? moduleFactory.name;
}

function defineFunctionModuleName(target, name) {
    Object.defineProperty(target, FUNCTION_MODULE_NAME, {
        value: name,
        enumerable: false,
    });
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
        const result = callback(target);

        if (result !== null && typeof result === "object" && typeof result.then === "function") {
            throw new TypeError(`Sloppy module ${phase} phase callback must be synchronous.`);
        }

        return result;
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

function buildCapabilityContributions(capabilitySnapshot) {
    const byModule = new Map();

    for (const capability of capabilitySnapshot.values()) {
        if (capability.module === null) {
            continue;
        }

        if (!byModule.has(capability.module)) {
            byModule.set(capability.module, []);
        }

        byModule.get(capability.module).push(capability.token);
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

function createModuleDebugEntries(orderedModules, capabilitySnapshot, serviceSnapshot, routes) {
    const capabilitiesByModule = buildCapabilityContributions(capabilitySnapshot);
    const servicesByModule = buildServiceContributions(serviceSnapshot);
    const routesByModule = buildRouteContributions(routes);

    return Object.freeze(orderedModules.map((state, index) => {
        const capabilities = Object.freeze([...(capabilitiesByModule.get(state.name) ?? [])]);
        const services = Object.freeze([...(servicesByModule.get(state.name) ?? [])]);
        const routeContributions = Object.freeze([...(routesByModule.get(state.name) ?? [])]);
        const contributes = [];

        if (capabilities.length > 0) {
            contributes.push("capabilities");
        }

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
            capabilities,
            services,
            routes: routeContributions,
            metadata: snapshotModuleMetadata(state.metadata),
        });
    }));
}

export {
    assertRouteOnlyModule,
    createModule,
    createModuleDebugEntries,
    defineFunctionModuleName,
    functionModuleName,
    getModuleState,
    requireModuleState,
    resolveModuleOrder,
    runModulePhase,
};
