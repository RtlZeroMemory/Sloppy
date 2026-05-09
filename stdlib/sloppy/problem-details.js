import { isPlainObject } from "./internal/shared.js";

const DETAIL_POLICIES = new Set(["never", "development", "always"]);

function validateDetailPolicy(value) {
    if (value === undefined) {
        return "never";
    }
    if (!DETAIL_POLICIES.has(value)) {
        throw new TypeError("Sloppy ProblemDetails detail policy must be never, development, or always.");
    }
    return value;
}

function defaults(options) {
    if (options !== undefined && !isPlainObject(options)) {
        throw new TypeError("Sloppy ProblemDetails.defaults options must be a plain object.");
    }

    return Object.freeze({
        __sloppyProblemDetails: true,
        detail: validateDetailPolicy(options?.detail),
    });
}

export const ProblemDetails = Object.freeze({
    defaults,
});
