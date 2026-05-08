import { isPlainObject } from "./shared.js";

const CONFIG_SECRET_INSPECT = Symbol.for("nodejs.util.inspect.custom");
const CONFIG_BIND_TYPES = Object.freeze([
    "array",
    "bool",
    "boolean",
    "bytes",
    "duration",
    "int",
    "integer",
    "number",
    "object",
    "secret",
    "size",
    "string",
]);
const DISALLOWED_CONFIG_SEGMENTS = new Set(["__proto__", "prototype", "constructor"]);

function createConfigContainer() {
    return Object.create(null);
}

function validateConfigKey(key) {
    if (typeof key !== "string" || key.length === 0) {
        throw new TypeError("Sloppy config key must be a non-empty string.");
    }
}

function assertSupportedConfigSegment(segment) {
    if (DISALLOWED_CONFIG_SEGMENTS.has(segment)) {
        throw new TypeError(`Sloppy config key segment '${segment}' is not supported.`);
    }
}

function normalizeConfigKey(key) {
    validateConfigKey(key);
    const segments = key.split(":");
    if (segments.some((segment) => segment.length === 0)) {
        throw new TypeError("Sloppy config key must not contain empty segments.");
    }
    return key
        .split(":")
        .map((segment) => segment.toUpperCase())
        .join(":");
}

function providerConfigPrefix(key) {
    validateConfigKey(key);
    if (/^[A-Za-z][A-Za-z0-9.-]*:[A-Za-z0-9_.-]+$/u.test(key) && !key.startsWith("Sloppy:")) {
        const [provider, name] = key.split(":");
        return `Sloppy:Providers:${provider}:${name}`;
    }
    return key;
}

function setConfigPath(target, segments, value) {
    let cursor = target;
    for (let index = 0; index < segments.length - 1; index += 1) {
        const segment = segments[index];
        assertSupportedConfigSegment(segment);
        const next = cursor[segment];
        if (next !== undefined && !isPlainObject(next)) {
            throw new Error(`Sloppy config bind cannot merge scalar and object at '${segments.slice(0, index + 1).join(":")}'.`);
        }
        cursor[segment] = next ?? createConfigContainer();
        cursor = cursor[segment];
    }
    const leaf = segments[segments.length - 1];
    assertSupportedConfigSegment(leaf);
    cursor[leaf] = value;
}

function flattenConfigObject(values, object, prefix = []) {
    for (const [key, value] of Object.entries(object)) {
        validateConfigKey(key);
        const segments = [...prefix, key];
        if (isPlainObject(value)) {
            flattenConfigObject(values, value, segments);
        } else {
            values[normalizeConfigKey(segments.join(":"))] = value;
        }
    }
}

function getConfigValue(values, key, fallback) {
    const normalized = normalizeConfigKey(key);
    return Object.prototype.hasOwnProperty.call(values, normalized) ? values[normalized] : fallback;
}

function requireConfigValue(values, key) {
    const normalized = normalizeConfigKey(key);
    if (!Object.prototype.hasOwnProperty.call(values, normalized)) {
        throw new Error(`Sloppy config key '${key}' is required but was not provided.`);
    }
    return values[normalized];
}

function hasConfigValue(values, key) {
    return Object.prototype.hasOwnProperty.call(values, normalizeConfigKey(key));
}

function getConfigObjectValue(values, key, fallback, required) {
    const normalized = normalizeConfigKey(key);
    if (Object.prototype.hasOwnProperty.call(values, normalized)) {
        return values[normalized];
    }
    const result = createConfigContainer();
    let found = false;
    for (const [entryKey, value] of Object.entries(values)) {
        if (!entryKey.startsWith(`${normalized}:`)) {
            continue;
        }
        found = true;
        setConfigPath(
            result,
            entryKey.slice(normalized.length + 1).split(":").map(configBindSegmentName),
            value,
        );
    }
    if (found) {
        return result;
    }
    if (!required) {
        return fallback;
    }
    throw new Error(`Sloppy config key '${key}' is required but was not provided.`);
}

function coerceConfigString(value, key) {
    if (typeof value !== "string") {
        throw new TypeError(`Sloppy config key '${key}' must be a string.`);
    }
    return value;
}

function coerceConfigNumber(value, key) {
    const parsed = typeof value === "string" && value.trim() !== "" ? Number(value) : value;
    if (typeof parsed !== "number" || !Number.isFinite(parsed)) {
        throw new TypeError(`Sloppy config key '${key}' must be a number.`);
    }
    return parsed;
}

function coerceConfigInt(value, key) {
    const parsed = coerceConfigNumber(value, key);
    if (!Number.isInteger(parsed)) {
        throw new TypeError(`Sloppy config key '${key}' must be an integer.`);
    }
    return parsed;
}

function coerceConfigBool(value, key) {
    if (typeof value === "boolean") {
        return value;
    }
    if (typeof value === "string") {
        if (value.toLowerCase() === "true") {
            return true;
        }
        if (value.toLowerCase() === "false") {
            return false;
        }
    }
    throw new TypeError(`Sloppy config key '${key}' must be a boolean.`);
}

class ConfigSecretValue {
    constructor(value) {
        this._value = coerceConfigString(value, "secret");
        Object.freeze(this);
    }

    value() {
        return this._value;
    }

    toString() {
        return "[Secret redacted]";
    }

    toJSON() {
        return "[Secret redacted]";
    }

    [CONFIG_SECRET_INSPECT]() {
        return "[Secret redacted]";
    }
}

function coerceConfigSecret(value, key) {
    if (value instanceof ConfigSecretValue) {
        return value;
    }
    return new ConfigSecretValue(coerceConfigString(value, key));
}

function coerceConfigArray(value, key) {
    if (!Array.isArray(value)) {
        throw new TypeError(`Sloppy config key '${key}' must be an array.`);
    }
    return Object.freeze([...value]);
}

function coerceConfigObject(value, key) {
    if (!isPlainObject(value)) {
        throw new TypeError(`Sloppy config key '${key}' must be an object.`);
    }
    return Object.freeze({ ...value });
}

function coerceConfigDuration(value, key) {
    if (typeof value === "number" && Number.isFinite(value) && value >= 0) {
        return value;
    }
    if (typeof value === "string") {
        const match = value.trim().match(/^(\d+(?:\.\d+)?)\s*(ms|s|m|h)$/iu);
        if (match !== null) {
            const amount = Number(match[1]);
            const unit = match[2].toLowerCase();
            const factors = {
                ms: 1,
                s: 1000,
                m: 60 * 1000,
                h: 60 * 60 * 1000,
            };
            return amount * factors[unit];
        }
    }
    throw new TypeError(`Sloppy config key '${key}' must be a duration in ms, s, m, or h.`);
}

function coerceConfigSize(value, key) {
    if (typeof value === "number" && Number.isInteger(value) && value >= 0) {
        return value;
    }
    if (typeof value === "string") {
        const match = value.trim().match(/^(\d+)\s*(b|kb|mb|gb|kib|mib|gib)$/iu);
        if (match !== null) {
            const amount = Number(match[1]);
            const unit = match[2].toLowerCase();
            const factors = {
                b: 1,
                kb: 1000,
                mb: 1000 * 1000,
                gb: 1000 * 1000 * 1000,
                kib: 1024,
                mib: 1024 * 1024,
                gib: 1024 * 1024 * 1024,
            };
            return amount * factors[unit];
        }
    }
    throw new TypeError(`Sloppy config key '${key}' must be a byte size.`);
}

function coerceConfigByType(value, key, type) {
    switch (type) {
        case "array":
            return coerceConfigArray(value, key);
        case "bool":
        case "boolean":
            return coerceConfigBool(value, key);
        case "bytes":
        case "size":
            return coerceConfigSize(value, key);
        case "duration":
            return coerceConfigDuration(value, key);
        case "int":
        case "integer":
            return coerceConfigInt(value, key);
        case "number":
            return coerceConfigNumber(value, key);
        case "object":
            return coerceConfigObject(value, key);
        case "secret":
            return coerceConfigSecret(value, key);
        case "string":
            return coerceConfigString(value, key);
        default:
            throw new TypeError(`Sloppy config bind descriptor has unsupported type '${type}'.`);
    }
}

function isConfigBindType(value) {
    return typeof value === "string" && CONFIG_BIND_TYPES.includes(value);
}

function isDescriptorObject(value) {
    return (
        isPlainObject(value) &&
        ("type" in value ||
            "default" in value ||
            "required" in value ||
            "enum" in value ||
            "values" in value ||
            "min" in value ||
            "max" in value ||
            "secret" in value)
    );
}

function schemaIsDescriptorMap(schema) {
    return Object.values(schema).some((value) => isConfigBindType(value) || isDescriptorObject(value));
}

function configDescriptorSegmentName(property) {
    if (property.includes(":")) {
        return property;
    }
    return property.charAt(0).toUpperCase() + property.slice(1);
}

function normalizeConfigDescriptor(property, descriptor) {
    if (isConfigBindType(descriptor)) {
        return {
            key: configDescriptorSegmentName(property),
            type: descriptor,
            required: true,
        };
    }
    if (!isDescriptorObject(descriptor)) {
        return {
            key: configDescriptorSegmentName(property),
            type: inferConfigDescriptorType(descriptor),
            default: descriptor,
            required: false,
        };
    }
    const type = descriptor.secret === true ? "secret" : (descriptor.type ?? inferConfigDescriptorType(descriptor.default));
    if (!isConfigBindType(type)) {
        throw new TypeError(`Sloppy config bind descriptor for '${property}' has unsupported type '${type}'.`);
    }
    if (type === "secret" && Object.prototype.hasOwnProperty.call(descriptor, "default")) {
        throw new TypeError(`Sloppy secret config key '${property}' must not declare a literal default.`);
    }
    const normalized = {
        key: descriptor.key ?? configDescriptorSegmentName(property),
        type,
        required: Object.prototype.hasOwnProperty.call(descriptor, "required")
            ? descriptor.required === true
            : !Object.prototype.hasOwnProperty.call(descriptor, "default"),
        allowed: descriptor.enum ?? descriptor.values,
        min: descriptor.min,
        max: descriptor.max,
    };
    if (Object.prototype.hasOwnProperty.call(descriptor, "default")) {
        normalized.default = descriptor.default;
    }
    return normalized;
}

function inferConfigDescriptorType(value) {
    switch (typeof value) {
        case "boolean":
            return "boolean";
        case "number":
            return "number";
        case "string":
            return "string";
        default:
            if (Array.isArray(value)) {
                return "array";
            }
            if (isPlainObject(value)) {
                return "object";
            }
            return "string";
    }
}

function bindConfigDescriptor(values, logicalPrefix, property, descriptor) {
    const normalized = normalizeConfigDescriptor(property, descriptor);
    const fullKey = normalized.key.includes(":")
        ? `${logicalPrefix}:${normalized.key}`
        : `${logicalPrefix}:${normalized.key}`;
    const normalizedKey = normalizeConfigKey(fullKey);
    const hasValue = Object.prototype.hasOwnProperty.call(values, normalizedKey);
    let rawValue;
    if (hasValue) {
        rawValue = values[normalizedKey];
    } else if (Object.prototype.hasOwnProperty.call(normalized, "default")) {
        rawValue = normalized.default;
    } else if (normalized.required) {
        const kind = normalized.type === "secret" ? "secret config key" : "config key";
        throw new Error(`Sloppy ${kind} '${fullKey}' is required but was not provided.`);
    } else {
        return undefined;
    }

    const coerced = coerceConfigByType(rawValue, fullKey, normalized.type);
    if (Array.isArray(normalized.allowed) && !normalized.allowed.some((value) => Object.is(value, coerced))) {
        throw new TypeError(`Sloppy config key '${fullKey}' must match one of the declared values.`);
    }
    if (normalized.min !== undefined && typeof coerced === "number" && coerced < normalized.min) {
        throw new RangeError(`Sloppy config key '${fullKey}' is below the declared minimum.`);
    }
    if (normalized.max !== undefined && typeof coerced === "number" && coerced > normalized.max) {
        throw new RangeError(`Sloppy config key '${fullKey}' is above the declared maximum.`);
    }
    return coerced;
}

function bindConfigValues(values, prefix, schema) {
    const logicalPrefix = providerConfigPrefix(prefix);
    const normalizedPrefix = normalizeConfigKey(logicalPrefix);
    const result = createConfigContainer();

    for (const [key, value] of Object.entries(values)) {
        if (key === normalizedPrefix || !key.startsWith(`${normalizedPrefix}:`)) {
            continue;
        }
        const segments = key.slice(normalizedPrefix.length + 1).split(":");
        setConfigPath(
            result,
            segments.map(configBindSegmentName),
            value,
        );
    }

    if (schema === undefined || schema === null) {
        return Object.freeze(result);
    }
    if (typeof schema === "function") {
        if (typeof schema.bindConfig === "function") {
            return schema.bindConfig(Object.freeze({ ...result }), Object.freeze({ key: logicalPrefix }));
        }
        if (/^[A-Z]/u.test(schema.name)) {
            return new schema(Object.freeze({ ...result }));
        }
        return schema(Object.freeze({ ...result }));
    }
    if (isPlainObject(schema)) {
        if (schemaIsDescriptorMap(schema)) {
            const bound = {};
            for (const [property, descriptor] of Object.entries(schema)) {
                const value = bindConfigDescriptor(values, logicalPrefix, property, descriptor);
                if (value !== undefined) {
                    bound[property] = value;
                }
            }
            return Object.freeze(bound);
        }
        return Object.freeze({ ...schema, ...result });
    }
    throw new TypeError("Sloppy config.bind schema must be a function, plain object, or omitted.");
}

function configBindSegmentName(segment) {
    switch (segment) {
        case "DATABASE":
            return "database";
        case "HOST":
            return "host";
        case "MAXCONNECTIONS":
            return "maxConnections";
        case "MAXREQUESTBODYBYTES":
            return "maxRequestBodyBytes";
        case "PORT":
            return "port";
        case "QUEUECAPACITY":
            return "queueCapacity";
        case "REQUESTTIMEOUTMS":
            return "requestTimeoutMs";
        case "V8MICROTASKDRAINLIMIT":
            return "v8MicrotaskDrainLimit";
        default:
            return segment.charAt(0).toLowerCase() + segment.slice(1).toLowerCase();
    }
}

function createConfigBuilder(guard) {
    const values = Object.create(null);

    const config = {
        addObject(object) {
            guard.assertMutable();

            if (!isPlainObject(object)) {
                throw new TypeError("Sloppy config.addObject value must be a plain object.");
            }

            flattenConfigObject(values, object);

            return config;
        },

        get(key, fallback) {
            return getConfigValue(values, key, fallback);
        },

        has(key) {
            return hasConfigValue(values, key);
        },

        require(key) {
            return requireConfigValue(values, key);
        },

        getString(key, fallback) {
            const value = fallback === undefined ? requireConfigValue(values, key) : getConfigValue(values, key, fallback);
            return coerceConfigString(value, key);
        },

        getInt(key, fallback) {
            const value = fallback === undefined ? requireConfigValue(values, key) : getConfigValue(values, key, fallback);
            return coerceConfigInt(value, key);
        },

        getNumber(key, fallback) {
            const value = fallback === undefined ? requireConfigValue(values, key) : getConfigValue(values, key, fallback);
            return coerceConfigNumber(value, key);
        },

        getBool(key, fallback) {
            const value = fallback === undefined ? requireConfigValue(values, key) : getConfigValue(values, key, fallback);
            return coerceConfigBool(value, key);
        },

        getBoolean(key, fallback) {
            return config.getBool(key, fallback);
        },

        getDuration(key, fallback) {
            const value = fallback === undefined ? requireConfigValue(values, key) : getConfigValue(values, key, fallback);
            return coerceConfigDuration(value, key);
        },

        getSize(key, fallback) {
            const value = fallback === undefined ? requireConfigValue(values, key) : getConfigValue(values, key, fallback);
            return coerceConfigSize(value, key);
        },

        getBytes(key, fallback) {
            return config.getSize(key, fallback);
        },

        getArray(key, fallback) {
            const value = fallback === undefined ? requireConfigValue(values, key) : getConfigValue(values, key, fallback);
            return coerceConfigArray(value, key);
        },

        getObject(key, fallback) {
            const value = getConfigObjectValue(values, key, fallback, fallback === undefined);
            return coerceConfigObject(value, key);
        },

        getSecret(key) {
            return coerceConfigSecret(requireConfigValue(values, key), key);
        },

        bind(prefix, schema) {
            return bindConfigValues(values, prefix, schema);
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
            return getConfigValue(snapshot, key, fallback);
        },

        has(key) {
            return hasConfigValue(snapshot, key);
        },

        require(key) {
            return requireConfigValue(snapshot, key);
        },

        getString(key, fallback) {
            const value = fallback === undefined ? requireConfigValue(snapshot, key) : getConfigValue(snapshot, key, fallback);
            return coerceConfigString(value, key);
        },

        getInt(key, fallback) {
            const value = fallback === undefined ? requireConfigValue(snapshot, key) : getConfigValue(snapshot, key, fallback);
            return coerceConfigInt(value, key);
        },

        getNumber(key, fallback) {
            const value = fallback === undefined ? requireConfigValue(snapshot, key) : getConfigValue(snapshot, key, fallback);
            return coerceConfigNumber(value, key);
        },

        getBool(key, fallback) {
            const value = fallback === undefined ? requireConfigValue(snapshot, key) : getConfigValue(snapshot, key, fallback);
            return coerceConfigBool(value, key);
        },

        getBoolean(key, fallback) {
            const value = fallback === undefined ? requireConfigValue(snapshot, key) : getConfigValue(snapshot, key, fallback);
            return coerceConfigBool(value, key);
        },

        getDuration(key, fallback) {
            const value = fallback === undefined ? requireConfigValue(snapshot, key) : getConfigValue(snapshot, key, fallback);
            return coerceConfigDuration(value, key);
        },

        getSize(key, fallback) {
            const value = fallback === undefined ? requireConfigValue(snapshot, key) : getConfigValue(snapshot, key, fallback);
            return coerceConfigSize(value, key);
        },

        getBytes(key, fallback) {
            const value = fallback === undefined ? requireConfigValue(snapshot, key) : getConfigValue(snapshot, key, fallback);
            return coerceConfigSize(value, key);
        },

        getArray(key, fallback) {
            const value = fallback === undefined ? requireConfigValue(snapshot, key) : getConfigValue(snapshot, key, fallback);
            return coerceConfigArray(value, key);
        },

        getObject(key, fallback) {
            const value = getConfigObjectValue(snapshot, key, fallback, fallback === undefined);
            return coerceConfigObject(value, key);
        },

        getSecret(key) {
            return coerceConfigSecret(requireConfigValue(snapshot, key), key);
        },

        bind(prefix, schema) {
            return bindConfigValues(snapshot, prefix, schema);
        },
    });
}

export { createConfigBuilder, createConfigProvider };
