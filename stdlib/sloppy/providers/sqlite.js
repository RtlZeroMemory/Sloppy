const SQLITE_PROVIDER_NAME_PATTERN = /^[A-Za-z0-9_.-]+$/u;

function validateSqliteProviderName(name) {
    if (typeof name !== "string" || name.length === 0) {
        throw new TypeError("Sloppy sqlite provider name must be a non-empty string.");
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
    if (
        Object.prototype.hasOwnProperty.call(options, "database") &&
        typeof options.database !== "string"
    ) {
        throw new TypeError("Sloppy sqlite provider database option must be a string.");
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
