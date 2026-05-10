function issue(message) {
    const error = new Error(message);
    error.name = "ZodLikeError";
    return error;
}

function string() {
    return {
        parse(value) {
            if (typeof value !== "string") {
                throw issue("expected string");
            }
            return value;
        }
    };
}

function number() {
    return {
        parse(value) {
            if (typeof value !== "number" || Number.isNaN(value)) {
                throw issue("expected number");
            }
            return value;
        }
    };
}

function object(shape) {
    return {
        parse(value) {
            if (value === null || typeof value !== "object") {
                throw issue("expected object");
            }
            const result = {};
            for (const key of Object.keys(shape)) {
                result[key] = shape[key].parse(value[key]);
            }
            return result;
        }
    };
}

export const z = Object.freeze({
    string,
    number,
    object
});

