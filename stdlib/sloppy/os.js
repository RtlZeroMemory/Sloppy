class OsError extends Error {
    constructor(code, message) {
        super(`${code}: ${message}`);
        this.name = "OsError";
        this.code = code;
    }
}

function osError(code, message) {
    return new OsError(code, message);
}

function bridge() {
    const os = globalThis.__sloppy?.os ?? null;
    if (os === null) {
        throw osError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "sloppy/os requires the stdlib.os runtime bridge.");
    }
    return os;
}

function requireKey(key, operation) {
    if (typeof key !== "string" || key.length === 0) {
        throw new TypeError(`${operation} requires a non-empty environment key string.`);
    }
    if (key.includes("=") || key.includes("\0")) {
        throw new TypeError(`${operation} environment key must not contain '=' or NUL.`);
    }
    return key;
}

function validateListOptions(options) {
    if (options === undefined) {
        return "";
    }
    if (options === null || typeof options !== "object" || Array.isArray(options)) {
        throw new TypeError("Environment.list options must be an object when provided.");
    }
    for (const key of Object.keys(options)) {
        if (key !== "prefix") {
            throw new TypeError(`Environment.list does not support option ${key}.`);
        }
    }
    if (options.prefix === undefined) {
        return "";
    }
    if (typeof options.prefix !== "string") {
        throw new TypeError("Environment.list prefix must be a string.");
    }
    return options.prefix;
}

function systemInfo() {
    const info = bridge().systemInfo();
    if (info === null || typeof info !== "object") {
        throw osError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "System metadata bridge returned invalid data.");
    }
    return info;
}

const System = Object.freeze({
    get platform() {
        return systemInfo().platform;
    },
    get arch() {
        return systemInfo().arch;
    },
    get cpuCount() {
        return systemInfo().cpuCount;
    },
    get tempDirectory() {
        return systemInfo().tempDirectory;
    },
    get hostname() {
        return systemInfo().hostname;
    },
    get endOfLine() {
        return systemInfo().endOfLine;
    },
});

const Environment = Object.freeze({
    get(key) {
        key = requireKey(key, "Environment.get");
        const value = bridge().environmentGet(key);
        return value === undefined ? undefined : String(value);
    },
    has(key) {
        key = requireKey(key, "Environment.has");
        return bridge().environmentHas(key) === true;
    },
    list(options = undefined) {
        const prefix = validateListOptions(options);
        return Object.freeze([...bridge().environmentList(prefix)].map(String));
    },
});

const Process = Object.freeze({
    run() {
        throw osError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "OS run API is deferred to CORE-OS-01.D.");
    },
    start() {
        throw osError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "OS start API is deferred to CORE-OS-01.E/F.");
    },
});

const Signals = Object.freeze({
    onShutdown() {
        throw osError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "Signals.onShutdown is deferred to CORE-OS-01.G.");
    },
});

export { Environment, OsError, Process, Signals, System };
