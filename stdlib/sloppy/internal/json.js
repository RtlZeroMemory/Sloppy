export function deepFreeze(value) {
    if (value === null || typeof value !== "object" || Object.isFrozen(value)) {
        return value;
    }
    for (const child of Object.values(value)) {
        deepFreeze(child);
    }
    return Object.freeze(value);
}

export function snapshotJson(value) {
    if (value === undefined) {
        return undefined;
    }
    return deepFreeze(JSON.parse(JSON.stringify(value)));
}
