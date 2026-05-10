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

class SloppyValidationError extends Error {
    constructor(issues) {
        super("Sloppy request validation failed.");
        this.name = "SloppyValidationError";
        this.issues = Object.freeze([...issues]);
        this.__sloppyValidationError = true;
    }
}

function validationProblem(issues) {
    return Object.freeze({
        type: "https://sloppy.dev/problems/validation",
        title: "Validation failed",
        status: 400,
        code: "SLOPPY_E_VALIDATION_FAILED",
        errors: Object.freeze(issues.map((current) => Object.freeze({
            path: Object.freeze([...current.path]),
            code: current.code,
            message: current.message,
        }))),
    });
}

function isValidationError(error) {
    return error !== null && typeof error === "object" && error.__sloppyValidationError === true;
}

function throwValidationError(issues) {
    throw new SloppyValidationError(issues);
}

function isPlainObject(value) {
    if (value === null || typeof value !== "object") {
        return false;
    }

    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function isSchema(value) {
    return value !== null &&
        typeof value === "object" &&
        typeof value.validate === "function" &&
        typeof value.__validateAtPath === "function";
}

function withModifiers(schema) {
    return Object.freeze({
        ...schema,
        optional() {
            return createOptionalSchema(schema);
        },
        nullable() {
            return createNullableSchema(schema);
        },
        default(value) {
            return createDefaultSchema(schema, value);
        },
    });
}

function createOptionalSchema(inner) {
    function validateAtPath(value, path) {
        if (value === undefined) {
            return success(undefined);
        }

        return inner.__validateAtPath(value, path);
    }

    return withModifiers({
        kind: inner.kind,
        metadata: Object.freeze({
            ...inner.metadata,
            optional: true,
        }),
        validate(value) {
            return validateAtPath(value, []);
        },
        __validateAtPath: validateAtPath,
    });
}

function createNullableSchema(inner) {
    function validateAtPath(value, path) {
        if (value === null) {
            return success(null);
        }

        return inner.__validateAtPath(value, path);
    }

    return withModifiers({
        kind: inner.kind,
        metadata: Object.freeze({
            ...inner.metadata,
            nullable: true,
        }),
        validate(value) {
            return validateAtPath(value, []);
        },
        __validateAtPath: validateAtPath,
    });
}

function createDefaultSchema(inner, defaultValue) {
    function validateAtPath(value, path) {
        if (value === undefined) {
            return success(defaultValue);
        }

        return inner.__validateAtPath(value, path);
    }

    return withModifiers({
        kind: inner.kind,
        metadata: Object.freeze({
            ...inner.metadata,
            default: defaultValue,
        }),
        validate(value) {
            return validateAtPath(value, []);
        },
        __validateAtPath: validateAtPath,
    });
}

function normalizeStringRuleValue(value, name) {
    if (!Number.isInteger(value) || value < 0) {
        throw new TypeError(`Sloppy schema.string().${name} length must be a non-negative integer.`);
    }
    return value;
}

function createStringSchema(rules = []) {
    function validateAtPath(value, path) {
        const issues = [];

        if (typeof value !== "string") {
            issues.push(issue(path, "type", "Expected a string."));
            return failure(issues);
        }

        for (const rule of rules) {
            if ((rule.kind === "min" || rule.kind === "minLength") && value.length < rule.value) {
                issues.push(issue(path, "string.min", `Expected at least ${rule.value} character(s).`));
            }

            if ((rule.kind === "max" || rule.kind === "maxLength") && value.length > rule.value) {
                issues.push(issue(path, "string.max", `Expected at most ${rule.value} character(s).`));
            }

            if (rule.kind === "email" && !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value)) {
                issues.push(issue(path, "string.email", "Expected an email address."));
            }

            if (rule.kind === "uuid" &&
                !/^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/iu.test(value)) {
                issues.push(issue(path, "string.uuid", "Expected a UUID."));
            }

            if (rule.kind === "pattern" && !rule.value.test(value)) {
                issues.push(issue(path, "string.pattern", rule.message ?? "Expected string to match pattern."));
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
            return createStringSchema([...rules, Object.freeze({ kind: "min", value: normalizeStringRuleValue(length, "min") })]);
        },
        max(length) {
            return createStringSchema([...rules, Object.freeze({ kind: "max", value: normalizeStringRuleValue(length, "max") })]);
        },
        minLength(length) {
            return createStringSchema([...rules, Object.freeze({ kind: "minLength", value: normalizeStringRuleValue(length, "minLength") })]);
        },
        maxLength(length) {
            return createStringSchema([...rules, Object.freeze({ kind: "maxLength", value: normalizeStringRuleValue(length, "maxLength") })]);
        },
        email() {
            return createStringSchema([...rules, Object.freeze({ kind: "email" })]);
        },
        uuid() {
            return createStringSchema([...rules, Object.freeze({ kind: "uuid" })]);
        },
        pattern(pattern, message) {
            if (!(pattern instanceof RegExp)) {
                throw new TypeError("Sloppy schema.string().pattern expects a RegExp.");
            }
            if (message !== undefined && typeof message !== "string") {
                throw new TypeError("Sloppy schema.string().pattern message must be a string.");
            }
            return createStringSchema([...rules, Object.freeze({ kind: "pattern", value: pattern, message })]);
        },
        validate(value) {
            return validateAtPath(value, []);
        },
        __validateAtPath: validateAtPath,
    };

    return withModifiers(schema);
}

function createNumberSchema(kind, integer, rules = []) {
    function validateAtPath(value, path) {
        if (typeof value !== "number" || !Number.isFinite(value) || (integer && !Number.isInteger(value))) {
            return failure([issue(path, "type", integer ? "Expected an integer." : "Expected a finite number.")]);
        }

        const issues = [];
        for (const rule of rules) {
            if (rule.kind === "min" && value < rule.value) {
                issues.push(issue(path, "number.min", `Expected a value greater than or equal to ${rule.value}.`));
            }
            if (rule.kind === "max" && value > rule.value) {
                issues.push(issue(path, "number.max", `Expected a value less than or equal to ${rule.value}.`));
            }
        }

        return issues.length === 0 ? success(value) : failure(issues);
    }

    return withModifiers({
        kind,
        metadata: Object.freeze({
            kind,
            rules: Object.freeze(rules.map((rule) => Object.freeze({ ...rule }))),
        }),
        min(value) {
            if (typeof value !== "number" || !Number.isFinite(value)) {
                throw new TypeError(`Sloppy schema.${kind}().min value must be a finite number.`);
            }
            return createNumberSchema(kind, integer, [...rules, Object.freeze({ kind: "min", value })]);
        },
        max(value) {
            if (typeof value !== "number" || !Number.isFinite(value)) {
                throw new TypeError(`Sloppy schema.${kind}().max value must be a finite number.`);
            }
            return createNumberSchema(kind, integer, [...rules, Object.freeze({ kind: "max", value })]);
        },
        validate(value) {
            return validateAtPath(value, []);
        },
        __validateAtPath: validateAtPath,
    });
}

function createPrimitiveSchema(kind, predicate, expected) {
    function validateAtPath(value, path) {
        if (!predicate(value)) {
            return failure([issue(path, "type", `Expected ${expected}.`)]);
        }

        return success(value);
    }

    return withModifiers({
        kind,
        metadata: Object.freeze({ kind }),
        validate(value) {
            return validateAtPath(value, []);
        },
        __validateAtPath: validateAtPath,
    });
}

function createArraySchema(itemSchema) {
    if (!isSchema(itemSchema)) {
        throw new TypeError("Sloppy schema.array item must be a schema.");
    }

    function validateAtPath(value, path) {
        if (!Array.isArray(value)) {
            return failure([issue(path, "type", "Expected an array.")]);
        }

        const issues = [];
        for (let index = 0; index < value.length; index += 1) {
            const itemResult = itemSchema.__validateAtPath(value[index], [...path, index]);
            if (!itemResult.ok) {
                issues.push(...itemResult.issues);
            }
        }

        return issues.length === 0 ? success(value) : failure(issues);
    }

    return withModifiers({
        kind: "array",
        metadata: Object.freeze({
            kind: "array",
            item: itemSchema.metadata,
        }),
        item: itemSchema,
        validate(value) {
            return validateAtPath(value, []);
        },
        __validateAtPath: validateAtPath,
    });
}

function literal(value) {
    if (!["string", "number", "boolean"].includes(typeof value) && value !== null) {
        throw new TypeError("Sloppy schema.literal expects a string, number, boolean, or null.");
    }

    function validateAtPath(input, path) {
        return Object.is(input, value)
            ? success(input)
            : failure([issue(path, "literal", "Expected literal value.")]);
    }

    return withModifiers({
        kind: "literal",
        metadata: Object.freeze({ kind: "literal", value }),
        validate(input) {
            return validateAtPath(input, []);
        },
        __validateAtPath: validateAtPath,
    });
}

function oneOf(values) {
    if (!Array.isArray(values) || values.length === 0) {
        throw new TypeError("Sloppy schema.enum expects a non-empty array.");
    }
    const variants = Object.freeze([...values]);

    function validateAtPath(input, path) {
        return variants.some((value) => Object.is(value, input))
            ? success(input)
            : failure([issue(path, "enum", "Expected one of the allowed values.")]);
    }

    return withModifiers({
        kind: "enum",
        metadata: Object.freeze({ kind: "enum", values: variants }),
        validate(input) {
            return validateAtPath(input, []);
        },
        __validateAtPath: validateAtPath,
    });
}

function validateShape(shape) {
    if (!isPlainObject(shape)) {
        throw new TypeError("Sloppy schema.object shape must be a plain object.");
    }

    for (const [key, value] of Object.entries(shape)) {
        if (typeof key !== "string" || key.length === 0) {
            throw new TypeError("Sloppy schema.object keys must be non-empty strings.");
        }

        if (!isSchema(value)) {
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

    return withModifiers({
        kind: "object",
        metadata,
        shape: fields,
        validate(value) {
            return validateAtPath(value, []);
        },
        __validateAtPath: validateAtPath,
    });
}

function validate(value, schemaValue) {
    if (!isSchema(schemaValue)) {
        throw new TypeError("Sloppy Schema.validate expects a schema.");
    }
    const result = schemaValue.validate(value);
    if (!result.ok) {
        throwValidationError(result.issues);
    }
    return result.value;
}

const schemaApi = Object.freeze({
    string() {
        return createStringSchema();
    },
    number() {
        return createNumberSchema("number", false);
    },
    int() {
        return createNumberSchema("int", true);
    },
    integer() {
        return createNumberSchema("int", true);
    },
    boolean() {
        return createPrimitiveSchema("boolean", (value) => typeof value === "boolean", "a boolean");
    },
    bool() {
        return createPrimitiveSchema("boolean", (value) => typeof value === "boolean", "a boolean");
    },
    array(itemSchema) {
        return createArraySchema(itemSchema);
    },
    enum(values) {
        return oneOf(values);
    },
    literal(value) {
        return literal(value);
    },
    object,
    validate,
    isSchema,
    validationProblem,
    isValidationError,
});

export const schema = schemaApi;
export const Schema = schemaApi;
export { SloppyValidationError, isSchema, isValidationError, validationProblem };
