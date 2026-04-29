function issue(path, code, message) {
    return Object.freeze({
        path: Object.freeze([...path]),
        code,
        message,
    });
}

function success(value) {
    return Object.freeze({
        ok: true,
        value,
    });
}

function failure(issues) {
    return Object.freeze({
        ok: false,
        issues: Object.freeze(issues),
    });
}

function createStringSchema(rules = []) {
    function validateAtPath(value, path) {
        const issues = [];

        if (typeof value !== "string") {
            issues.push(issue(path, "type", "Expected a string."));
            return failure(issues);
        }

        for (const rule of rules) {
            if (rule.kind === "min" && value.length < rule.value) {
                issues.push(issue(path, "string.min", `Expected at least ${rule.value} character(s).`));
            }

            if (rule.kind === "email" && !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value)) {
                issues.push(issue(path, "string.email", "Expected an email address."));
            }
        }

        return issues.length === 0 ? success(value) : failure(issues);
    }

    const schema = {
        kind: "string",
        metadata: Object.freeze({
            kind: "string",
            rules: Object.freeze(rules.map((rule) => Object.freeze({ ...rule }))),
        }),
        min(length) {
            if (!Number.isInteger(length) || length < 0) {
                throw new TypeError("Sloppy schema.string().min length must be a non-negative integer.");
            }

            return createStringSchema([...rules, Object.freeze({ kind: "min", value: length })]);
        },
        email() {
            return createStringSchema([...rules, Object.freeze({ kind: "email" })]);
        },
        validate(value) {
            return validateAtPath(value, []);
        },
        __validateAtPath: validateAtPath,
    };

    return Object.freeze(schema);
}

function createPrimitiveSchema(kind, predicate, expected) {
    function validateAtPath(value, path) {
        if (!predicate(value)) {
            return failure([issue(path, "type", `Expected ${expected}.`)]);
        }

        return success(value);
    }

    return Object.freeze({
        kind,
        metadata: Object.freeze({ kind }),
        validate(value) {
            return validateAtPath(value, []);
        },
        __validateAtPath: validateAtPath,
    });
}

function validateShape(shape) {
    if (shape === null || typeof shape !== "object" || Array.isArray(shape)) {
        throw new TypeError("Sloppy schema.object shape must be a plain object.");
    }

    for (const [key, value] of Object.entries(shape)) {
        if (typeof key !== "string" || key.length === 0) {
            throw new TypeError("Sloppy schema.object keys must be non-empty strings.");
        }

        if (value === null || typeof value !== "object" || typeof value.validate !== "function") {
            throw new TypeError(`Sloppy schema.object field '${key}' must be a schema.`);
        }
    }
}

function object(shape) {
    validateShape(shape);

    const fields = Object.freeze({ ...shape });
    const metadata = Object.freeze({
        kind: "object",
        shape: Object.freeze(Object.fromEntries(
            Object.entries(fields).map(([key, value]) => [key, value.metadata]),
        )),
    });

    function validateAtPath(value, path) {
        if (value === null || typeof value !== "object" || Array.isArray(value)) {
            return failure([issue(path, "type", "Expected an object.")]);
        }

        const issues = [];

        for (const [key, fieldSchema] of Object.entries(fields)) {
            const fieldResult = fieldSchema.__validateAtPath(value[key], [...path, key]);

            if (!fieldResult.ok) {
                issues.push(...fieldResult.issues);
            }
        }

        return issues.length === 0 ? success(value) : failure(issues);
    }

    return Object.freeze({
        kind: "object",
        metadata,
        shape: fields,
        validate(value) {
            return validateAtPath(value, []);
        },
        __validateAtPath: validateAtPath,
    });
}

export const schema = Object.freeze({
    string() {
        return createStringSchema();
    },
    number() {
        return createPrimitiveSchema(
            "number",
            (value) => typeof value === "number" && Number.isFinite(value),
            "a finite number",
        );
    },
    boolean() {
        return createPrimitiveSchema("boolean", (value) => typeof value === "boolean", "a boolean");
    },
    object,
});
