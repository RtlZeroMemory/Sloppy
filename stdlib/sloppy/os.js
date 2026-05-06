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

function requireArgv(command, args) {
    if (typeof command !== "string" || command.length === 0 || command.includes("\0")) {
        throw new TypeError("OS run command must be a non-empty string without NUL.");
    }
    if (args === undefined) {
        return [];
    }
    if (!Array.isArray(args)) {
        throw new TypeError("OS run args must be an array when provided.");
    }
    return args.map((arg, index) => {
        if (typeof arg !== "string" || arg.includes("\0")) {
            throw new TypeError(`OS run args[${index}] must be a string without NUL.`);
        }
        return arg;
    });
}

function validateRunOptions(options) {
    const normalized = {
        capture: "text",
        maxStdoutBytes: 65536,
        maxStderrBytes: 65536,
        timeoutMs: 0,
    };
    if (options === undefined) {
        return normalized;
    }
    if (options === null || typeof options !== "object" || Array.isArray(options)) {
        throw new TypeError("OS run options must be an object when provided.");
    }
    for (const key of Object.keys(options)) {
        if (!["cwd", "env", "capture", "maxStdoutBytes", "maxStderrBytes", "timeoutMs", "deadline", "signal"].includes(key)) {
            throw new TypeError(`OS run does not support option ${key}.`);
        }
    }
    if (options.cwd !== undefined) {
        if (typeof options.cwd !== "string" || options.cwd.includes("\0")) {
            throw new TypeError("OS run cwd must be a string without NUL.");
        }
        normalized.cwd = options.cwd;
    }
    if (options.env !== undefined) {
        if (options.env === null || typeof options.env !== "object" || Array.isArray(options.env)) {
            throw new TypeError("OS run env must be an object when provided.");
        }
        normalized.env = Object.freeze(Object.fromEntries(Object.entries(options.env).map(([key, value]) => {
            requireKey(key, "OS run env");
            if (typeof value !== "string" || value.includes("\0")) {
                throw new TypeError(`OS run env.${key} must be a string without NUL.`);
            }
            return [key, value];
        })));
    }
    if (options.capture !== undefined) {
        if (!["none", "text", "bytes"].includes(options.capture)) {
            throw new TypeError('OS run capture must be "none", "text", or "bytes".');
        }
        normalized.capture = options.capture;
    }
    for (const key of ["maxStdoutBytes", "maxStderrBytes", "timeoutMs"]) {
        if (options[key] !== undefined) {
            if (!Number.isFinite(options[key]) || options[key] < 0) {
                throw new TypeError(`OS run ${key} must be a non-negative number.`);
            }
            normalized[key] = Math.ceil(options[key]);
        }
    }
    if (options.deadline !== undefined && options.deadline !== null) {
        if (typeof options.deadline.remainingMs !== "function") {
            throw new TypeError("OS run deadline must come from sloppy/time Deadline.");
        }
        const remaining = options.deadline.remainingMs();
        if (remaining <= 0) {
            throw osError("SLOPPY_E_OS_PROCESS_TIMEOUT", "deadline already expired");
        }
        if (remaining !== Infinity) {
            if (!Number.isFinite(remaining)) {
                throw new TypeError("OS run deadline remaining time must be finite or Infinity.");
            }
            normalized.timeoutMs = Math.min(normalized.timeoutMs || Infinity, Math.ceil(remaining));
        }
    }
    if (options.signal !== undefined) {
        if (options.signal === null || typeof options.signal !== "object") {
            throw new TypeError("OS run signal must be a cancellation signal.");
        }
        if (options.signal.aborted === true) {
            throw osError("SLOPPY_E_OS_PROCESS_CANCELLED", "operation was cancelled");
        }
    }
    return normalized;
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
    async run(command, args = [], options = undefined) {
        const argv = requireArgv(command, args);
        const runOptions = validateRunOptions(options);
        const os = bridge();
        if (typeof os.processRun !== "function") {
            throw osError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "OS run bridge is unavailable.");
        }
        return os.processRun(command, argv, runOptions);
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
