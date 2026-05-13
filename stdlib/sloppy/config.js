function validateConfigReferenceKey(key, subject = "Config.required") {
    if (typeof key !== "string" || key.length === 0) {
        throw new TypeError(`Sloppy ${subject} key must be a non-empty string.`);
    }
}

function required(key) {
    validateConfigReferenceKey(key, "Config.required");
    return Object.freeze({
        __sloppyConfigReference: true,
        key,
        required: true,
        toString() {
            return "[Config reference redacted]";
        },
        toJSON() {
            return {
                key,
                required: true,
                value: "[redacted]",
            };
        },
    });
}

function boolean(key, fallback = undefined) {
    validateConfigReferenceKey(key, "Config.boolean");
    if (fallback !== undefined && typeof fallback !== "boolean") {
        throw new TypeError("Sloppy Config.boolean fallback must be a boolean when provided.");
    }
    return Object.freeze({
        __sloppyConfigReference: true,
        key,
        type: "boolean",
        default: fallback,
        toString() {
            return "[Config reference redacted]";
        },
        toJSON() {
            return {
                key,
                type: "boolean",
                value: "[redacted]",
            };
        },
    });
}

export const Config = Object.freeze({
    boolean,
    required,
    requiredSecret: required,
});
