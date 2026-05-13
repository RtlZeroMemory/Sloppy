export function deepFreeze(value, seen = new WeakSet()) {
    if (value === null || typeof value !== "object" || Object.isFrozen(value)) {
        return value;
    }
    if (seen.has(value)) {
        return value;
    }
    seen.add(value);
    for (const child of Object.values(value)) {
        deepFreeze(child, seen);
    }
    return Object.freeze(value);
}

export function snapshotJson(value) {
    if (value === undefined) {
        return undefined;
    }
    return deepFreeze(JSON.parse(JSON.stringify(value)));
}
