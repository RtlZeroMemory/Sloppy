export function isUserName(value) {
    return typeof value === "string" && value.trim() === value && value.length >= 2;
}

export function normalizeName(value) {
    return String(value || "").trim();
}
