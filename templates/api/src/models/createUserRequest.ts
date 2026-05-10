export function normalizeCreateUserRequest(input) {
    return {
        name: String(input.name || "").trim(),
        email: String(input.email || "").trim().toLowerCase(),
    };
}
