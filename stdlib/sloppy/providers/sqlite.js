const SQLITE_PROVIDER_NAME_PATTERN = /^[A-Za-z0-9_.-]+$/u;

function validateSqliteProviderName(name) {
    if (typeof name !== "string" || name.length === 0 || name.includes("\0")) {
        throw new TypeError("Sloppy sqlite provider name must be a non-empty string without NUL.");
    }
    if (name.trim() !== name || !SQLITE_PROVIDER_NAME_PATTERN.test(name)) {
        throw new TypeError(
            "Sloppy sqlite provider name must contain only letters, digits, dots, underscores, or hyphens.",
        );
    }
}

function validateSqliteProviderOptions(options) {
    if (options === undefined) {
        return Object.freeze({});
    }
    if (options === null || typeof options !== "object" || Array.isArray(options)) {
        throw new TypeError("Sloppy sqlite provider options must be a plain object.");
    }
    const allowedKeys = new Set(["database", "queueCapacity"]);
    for (const key of Object.keys(options)) {
        if (!allowedKeys.has(key)) {
            throw new TypeError(`Sloppy sqlite provider option '${key}' is not supported.`);
        }
    }
    if (
        Object.prototype.hasOwnProperty.call(options, "database") &&
        (typeof options.database !== "string" || options.database.includes("\0"))
    ) {
        throw new TypeError("Sloppy sqlite provider database option must be a string without NUL.");
    }
    if (
        Object.prototype.hasOwnProperty.call(options, "queueCapacity") &&
        (!Number.isInteger(options.queueCapacity) || options.queueCapacity < 1 || options.queueCapacity > 1_000_000)
    ) {
        throw new TypeError("Sloppy sqlite provider queueCapacity option must be a positive integer.");
    }
    return Object.freeze({ ...options });
}

export function sqlite(name, options) {
    validateSqliteProviderName(name);

    return Object.freeze({
        __sloppyProvider: true,
        kind: "sqlite",
        name,
        token: name.includes(".") ? name : `data.${name}`,
        options: validateSqliteProviderOptions(options),
    });
}
