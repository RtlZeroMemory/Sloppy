function inspect(value) {
    if (typeof value === "string") {
        return value;
    }
    if (typeof value === "function") {
        return value.name ? `[Function: ${value.name}]` : "[Function]";
    }
    try {
        return JSON.stringify(value);
    } catch (_) {
        return String(value);
    }
}

function format(first = "", ...rest) {
    if (typeof first !== "string") {
        return [first, ...rest].map(inspect).join(" ");
    }
    let index = 0;
    const text = first.replace(/%[sdjifoO%]/g, (token) => {
        if (token === "%%") {
            return "%";
        }
        if (index >= rest.length) {
            return token;
        }
        const value = rest[index];
        index += 1;
        if (token === "%s") {
            return String(value);
        }
        if (token === "%d") {
            return String(Number(value));
        }
        if (token === "%i") {
            return String(Number.parseInt(value, 10));
        }
        if (token === "%f") {
            return String(Number.parseFloat(value));
        }
        if (token === "%j") {
            try {
                return JSON.stringify(value);
            } catch {
                return "[Circular]";
            }
        }
        return inspect(value);
    });
    return [text, ...rest.slice(index).map(inspect)].join(" ");
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

function inherits(ctor, superCtor) {
    if (typeof ctor !== "function" || typeof superCtor !== "function") {
        throw new TypeError("util.inherits expects constructor functions.");
    }
    Object.setPrototypeOf(ctor.prototype, superCtor.prototype);
    Object.defineProperty(ctor.prototype, "constructor", {
        configurable: true,
        enumerable: false,
        value: ctor,
        writable: true,
    });
}

const types = Object.freeze({
    isArrayBufferView: ArrayBuffer.isView,
    isDate: (value) => value instanceof Date,
    isRegExp: (value) => value instanceof RegExp,
    isUint8Array: (value) => value instanceof Uint8Array,
});

export { callbackify, format, inherits, inspect, promisify, types };
export default { callbackify, format, inherits, inspect, promisify, types };
