export function normalizeCreateUserRequest(input) {
    const body = input !== null && typeof input === "object" && !Array.isArray(input)
        ? input
        : {};
    return {
        name: String(body.name || "").trim(),
        email: String(body.email || "").trim().toLowerCase(),
    };
}

export function createUserValidationErrors(input) {
    const errors = {};
    if (!input.name) {
        errors.name = ["name is required"];
    }
    if (!input.email) {
        errors.email = ["email is required"];
    } else if (!input.email.includes("@")) {
        errors.email = ["email must contain @"];
    }
    return errors;
}
