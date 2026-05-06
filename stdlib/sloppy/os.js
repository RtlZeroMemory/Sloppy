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

function isCancellationSignal(value) {
    return (
        value !== null &&
        typeof value === "object" &&
        typeof value.aborted === "boolean" &&
        ("reason" in value || typeof value.addEventListener === "function")
    );
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
        if (!isCancellationSignal(options.signal)) {
            throw new TypeError("OS run signal must be a cancellation signal.");
        }
        if (options.signal.aborted === true) {
            throw osError("SLOPPY_E_OS_PROCESS_CANCELLED", "operation was cancelled");
        }
    }
    return normalized;
}

function validateStartOptions(options) {
    const normalized = {
        stdin: "ignore",
        stdout: "ignore",
        stderr: "ignore",
    };
    if (options === undefined) {
        return normalized;
    }
    if (options === null || typeof options !== "object" || Array.isArray(options)) {
        throw new TypeError("OS start options must be an object when provided.");
    }
    for (const key of Object.keys(options)) {
        if (!["cwd", "env", "stdin", "stdout", "stderr", "deadline", "signal"].includes(key)) {
            throw new TypeError(`OS start does not support option ${key}.`);
        }
    }
    if (options.cwd !== undefined) {
        if (typeof options.cwd !== "string" || options.cwd.includes("\0")) {
            throw new TypeError("OS start cwd must be a string without NUL.");
        }
        normalized.cwd = options.cwd;
    }
    if (options.env !== undefined) {
        if (options.env === null || typeof options.env !== "object" || Array.isArray(options.env)) {
            throw new TypeError("OS start env must be an object when provided.");
        }
        normalized.env = Object.freeze(Object.fromEntries(Object.entries(options.env).map(([key, value]) => {
            requireKey(key, "OS start env");
            if (typeof value !== "string" || value.includes("\0")) {
                throw new TypeError(`OS start env.${key} must be a string without NUL.`);
            }
            return [key, value];
        })));
    }
    for (const key of ["stdin", "stdout", "stderr"]) {
        if (options[key] !== undefined) {
            if (!["ignore", "pipe"].includes(options[key])) {
                throw new TypeError(`OS start ${key} must be "ignore" or "pipe".`);
            }
            normalized[key] = options[key];
        }
    }
    if (options.deadline !== undefined && options.deadline !== null) {
        if (typeof options.deadline.remainingMs !== "function") {
            throw new TypeError("OS start deadline must come from sloppy/time Deadline.");
        }
        if (options.deadline.remainingMs() <= 0) {
            throw osError("SLOPPY_E_OS_PROCESS_TIMEOUT", "deadline already expired");
        }
    }
    if (options.signal !== undefined) {
        if (!isCancellationSignal(options.signal)) {
            throw new TypeError("OS start signal must be a cancellation signal.");
        }
        if (options.signal.aborted === true) {
            throw osError("SLOPPY_E_OS_PROCESS_CANCELLED", "process was cancelled");
        }
    }
    return normalized;
}

function validateWaitOptions(options) {
    const normalized = { timeoutMs: 0 };
    if (options === undefined) {
        return normalized;
    }
    if (options === null || typeof options !== "object" || Array.isArray(options)) {
        throw new TypeError("ProcessHandle.wait options must be an object when provided.");
    }
    for (const key of Object.keys(options)) {
        if (!["timeoutMs", "deadline", "signal"].includes(key)) {
            throw new TypeError(`ProcessHandle.wait does not support option ${key}.`);
        }
    }
    if (options.timeoutMs !== undefined) {
        if (!Number.isFinite(options.timeoutMs) || options.timeoutMs < 0) {
            throw new TypeError("ProcessHandle.wait timeoutMs must be a non-negative number.");
        }
        normalized.timeoutMs = Math.ceil(options.timeoutMs);
    }
    if (options.deadline !== undefined && options.deadline !== null) {
        if (typeof options.deadline.remainingMs !== "function") {
            throw new TypeError("ProcessHandle.wait deadline must come from sloppy/time Deadline.");
        }
        const remaining = options.deadline.remainingMs();
        if (remaining <= 0) {
            throw osError("SLOPPY_E_OS_PROCESS_TIMEOUT", "deadline already expired");
        }
        if (remaining !== Infinity) {
            if (!Number.isFinite(remaining)) {
                throw new TypeError("ProcessHandle.wait deadline remaining time must be finite or Infinity.");
            }
            normalized.timeoutMs = Math.min(normalized.timeoutMs || Infinity, Math.ceil(remaining));
        }
    }
    if (options.signal !== undefined) {
        if (!isCancellationSignal(options.signal)) {
            throw new TypeError("ProcessHandle.wait signal must be a cancellation signal.");
        }
        if (options.signal.aborted === true) {
            throw osError("SLOPPY_E_OS_PROCESS_CANCELLED", "process was cancelled");
        }
    }
    return normalized;
}

class ProcessPipe {
    constructor(handle, name) {
        this._handle = handle;
        this._name = name;
    }

    async read(maxBytes = 65536) {
        if (!Number.isFinite(maxBytes) || maxBytes <= 0) {
            throw new TypeError("Process pipe read maxBytes must be a positive number.");
        }
        const method = this._name === "stderr" ? "readStderr" : "readStdout";
        if (typeof this._handle[method] !== "function") {
            throw osError("SLOPPY_E_OS_PIPE_CLOSED", "process pipe is closed");
        }
        return this._handle[method](Math.ceil(maxBytes));
    }

    async readText(maxBytes = 65536) {
        const value = await this.read(maxBytes);
        if (typeof value === "string") {
            return value;
        }
        return new TextDecoder().decode(value);
    }

    async *readLines(options = undefined) {
        const chunkSize = options?.chunkSize ?? 4096;
        const decoder = new TextDecoder();
        let buffered = "";
        for (;;) {
            const value = await this.read(chunkSize);
            const chunk = typeof value === "string" ? value : decoder.decode(value, { stream: true });
            if (chunk.length === 0) {
                break;
            }
            buffered += chunk;
            for (;;) {
                const newline = buffered.indexOf("\n");
                if (newline < 0) {
                    break;
                }
                const line = buffered.slice(0, newline).replace(/\r$/, "");
                buffered = buffered.slice(newline + 1);
                yield line;
            }
        }
        buffered += decoder.decode();
        if (buffered.length !== 0) {
            yield buffered.replace(/\r$/, "");
        }
    }
}

class ProcessInput {
    constructor(handle) {
        this._handle = handle;
    }

    async write(value) {
        if (typeof this._handle.writeStdin !== "function") {
            throw osError("SLOPPY_E_OS_PIPE_CLOSED", "process pipe is closed");
        }
        return this._handle.writeStdin(value);
    }

    async writeText(text) {
        if (typeof text !== "string") {
            throw new TypeError("Process stdin writeText requires a string.");
        }
        return this.write(text);
    }

    async close() {
        if (typeof this._handle.closeStdin !== "function") {
            throw osError("SLOPPY_E_OS_PIPE_CLOSED", "process pipe is closed");
        }
        return this._handle.closeStdin();
    }
}

class ProcessHandle {
    constructor(handle, options) {
        this._handle = handle;
        this.stdin = options.stdin === "pipe" ? new ProcessInput(handle) : undefined;
        this.stdout = options.stdout === "pipe" ? new ProcessPipe(handle, "stdout") : undefined;
        this.stderr = options.stderr === "pipe" ? new ProcessPipe(handle, "stderr") : undefined;
    }

    async wait(options = undefined) {
        if (typeof this._handle.wait !== "function") {
            throw osError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "OS process wait bridge is unavailable.");
        }
        return this._handle.wait(validateWaitOptions(options));
    }

    async terminate() {
        if (typeof this._handle.terminate !== "function") {
            throw osError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "OS process terminate bridge is unavailable.");
        }
        return this._handle.terminate();
    }

    async kill() {
        if (typeof this._handle.kill !== "function") {
            throw osError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "OS process kill bridge is unavailable.");
        }
        return this._handle.kill();
    }

    async cancel() {
        if (typeof this._handle.cancel !== "function") {
            throw osError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "OS process cancel bridge is unavailable.");
        }
        return this._handle.cancel();
    }

    async dispose() {
        if (typeof this._handle.dispose === "function") {
            return this._handle.dispose();
        }
        return undefined;
    }
}

function validateShutdownHandler(handler) {
    if (typeof handler !== "function") {
        throw new TypeError("Signals.onShutdown requires a function.");
    }
    return handler;
}

function normalizeShutdownContext(ctx = undefined) {
    const source = ctx === null || typeof ctx !== "object" ? {} : ctx;
    return Object.freeze({
        signal: typeof source.signal === "string" ? source.signal : "shutdown",
        forced: source.forced === true,
        reason: source.reason,
    });
}

function signalHandlerFailure(error) {
    const failure = osError("SLOPPY_E_OS_SIGNAL_HANDLER_FAILURE", "shutdown signal handler failed");
    failure.cause = error;
    return failure;
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
    async start(command, args = [], options = undefined) {
        const argv = requireArgv(command, args);
        const startOptions = validateStartOptions(options);
        const os = bridge();
        if (typeof os.processStart !== "function") {
            throw osError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "OS start bridge is unavailable.");
        }
        return new ProcessHandle(await os.processStart(command, argv, startOptions), startOptions);
    },
});

const Signals = Object.freeze({
    onShutdown(handler) {
        handler = validateShutdownHandler(handler);
        const os = bridge();
        if (typeof os.signalsOnShutdown !== "function") {
            throw osError("SLOPPY_E_OS_FEATURE_UNAVAILABLE", "OS shutdown signal bridge is unavailable.");
        }
        return os.signalsOnShutdown(async (ctx = undefined) => {
            try {
                await handler(normalizeShutdownContext(ctx));
            } catch (error) {
                throw signalHandlerFailure(error);
            }
        });
    },
});

export { Environment, OsError, Process, ProcessHandle, Signals, System };
