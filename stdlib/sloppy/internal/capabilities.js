import { isPlainObject } from "./shared.js";

const DATABASE_ACCESS_MODES = Object.freeze(["read", "write", "readwrite"]);

function validateCapabilityToken(token) {
    if (typeof token !== "string" || token.length === 0) {
        throw new TypeError("Sloppy capability token must be a non-empty string.");
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

export {
    createCapabilityProvider,
    createCapabilityRegistry,
};
