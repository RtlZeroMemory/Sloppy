import { validateSqliteProviderOptions } from "../internal/validation.js";

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

export function sqlite(name, options) {
    validateSqliteProviderName(name);

    return Object.freeze({
        __sloppyProvider: true,
        kind: "sqlite",
        name,
        token: name.includes(".") ? name : `data.${name}`,
        options: options === undefined ? Object.freeze({}) : validateSqliteProviderOptions(options),
    });
}
