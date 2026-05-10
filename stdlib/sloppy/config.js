function validateConfigReferenceKey(key) {
    if (typeof key !== "string" || key.length === 0) {
        throw new TypeError("Sloppy Config.required key must be a non-empty string.");
    }
}

function required(key) {
    validateConfigReferenceKey(key);
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

export const Config = Object.freeze({
    required,
});
