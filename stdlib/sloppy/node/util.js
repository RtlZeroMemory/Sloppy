function inspect(value) {
    if (typeof value === "string") {
        return value;
    }
    try {
        return JSON.stringify(value);
    } catch (_) {
        return String(value);
    }
}

function promisify(fn) {
    if (typeof fn !== "function") {
        throw new TypeError("util.promisify expects a function.");
    }
    return (...args) => new Promise((resolve, reject) => {
        fn(...args, (error, value) => (error ? reject(error) : resolve(value)));
    });
}

function callbackify(fn) {
    if (typeof fn !== "function") {
        throw new TypeError("util.callbackify expects a function.");
    }
    return (...args) => {
        const callback = args.pop();
        Promise.resolve()
            .then(() => fn(...args))
            .then((value) => callback(null, value), (error) => callback(error));
    };
}

export { callbackify, inspect, promisify };
export default { callbackify, inspect, promisify };
