async function disposeAll(values) {
    const errors = [];
    for (const value of values) {
        if (value === undefined || value === null) {
            continue;
        }
        try {
            await value.dispose?.();
        } catch (error) {
            errors.push(error);
        }
    }
    if (errors.length === 1) {
        throw errors[0];
    }
    if (errors.length > 1) {
        throw new AggregateError(errors, "Multiple Sloppy cleanup operations failed.");
    }
}

function onceAsync(fn) {
    let promise = undefined;
    return () => {
        if (promise === undefined) {
            promise = Promise.resolve().then(fn).then(() => undefined);
        }
        return promise;
    };
}

export { disposeAll, onceAsync };
