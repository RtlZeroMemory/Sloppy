export function normalizeCreateUserRequest(input) {
    const body = input !== null && typeof input === "object" && !Array.isArray(input)
        ? input
        : {};
    const name = typeof body.name === "string" ? body.name : "";
    const email = typeof body.email === "string" ? body.email : "";
    return {
        name: name.trim(),
        email: email.trim().toLowerCase(),
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
