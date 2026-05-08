function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }

    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function createMutationGuard(subject) {
    let frozen = false;

    return Object.freeze({
        assertMutable() {
            if (frozen) {
                throw new Error(`Sloppy ${subject} is frozen and cannot be modified.`);
            }
        },
        freeze() {
            frozen = true;
        },
        isFrozen() {
            return frozen;
        },
    });
}

function isPromiseLike(value) {
    return value !== null && typeof value === "object" && typeof value.then === "function";
}

export { createMutationGuard, isPlainObject, isPromiseLike };
