import { Text } from "./codec.js";

class SloppyNetError extends Error {
    constructor(name, message, options) {
        super(message);
        this.name = name;
        const code = /^((?:SLOPPY_E|SLOPPY_W)_[A-Z0-9_]+):/.exec(message)?.[1];
        if (code !== undefined) {
            this.code = code;
        }
        if (options?.cause !== undefined) {
            this.cause = options.cause;
        }
    }
}

class NetworkAddress {
    constructor(host, port) {
        if (typeof host !== "string" || host.length === 0) {
            throw new TypeError("NetworkAddress host must be a non-empty string.");
        }
        if (!Number.isInteger(port) || port < 0 || port > 65535) {
            throw new TypeError("NetworkAddress port must be an integer from 0 to 65535.");
        }
        this.host = host;
        this.port = port;
        Object.freeze(this);
    }

    static parse(value) {
        if (value instanceof NetworkAddress) {
            return value;
        }
        if (typeof value === "string") {
            if (value.startsWith("[")) {
                const end = value.indexOf("]");
                if (end < 0 || value[end + 1] !== ":") {
                    throw new TypeError("NetworkAddress IPv6 text must be [host]:port.");
                }
                return new NetworkAddress(
                    value.slice(1, end),
                    parsePortText(value.slice(end + 2)),
                );
            }
            const firstColon = value.indexOf(":");
            const lastColon = value.lastIndexOf(":");
            if (firstColon <= 0 || firstColon !== lastColon || lastColon === value.length - 1) {
                throw new TypeError("NetworkAddress text must be host:port or [ipv6]:port.");
            }
            return new NetworkAddress(
                value.slice(0, lastColon),
                parsePortText(value.slice(lastColon + 1)),
            );
        }
        if (typeof value !== "object" || value === null) {
            throw new TypeError("NetworkAddress.parse requires an address object.");
        }
        return new NetworkAddress(value.host, value.port);
    }

    toString() {
        return this.host.includes(":") ? `[${this.host}]:${this.port}` : `${this.host}:${this.port}`;
    }
}

function requireNetBridge() {
    const bridge = globalThis.__sloppy?.net;

    if (bridge === undefined) {
        throw new Error(`SLOPPY_E_UNAVAILABLE_RUNTIME_FEATURE: runtime feature stdlib.net is inactive or unavailable

Feature:
  stdlib.net

Reason:
  The active Sloppy Plan did not enable the __sloppy.net V8 intrinsic namespace.`);
    }

    return bridge;
}

function isPlainObject(value) {
    if (value === null || typeof value !== "object" || Array.isArray(value)) {
        return false;
    }
    const prototype = Object.getPrototypeOf(value);
    return prototype === Object.prototype || prototype === null;
}

function normalizePort(port, allowZero) {
    const minimum = allowZero ? 0 : 1;
    if (!Number.isInteger(port) || port < minimum || port > 65535) {
        throw new TypeError(`TCP port must be an integer from ${minimum} to 65535.`);
    }
    return port;
}

function parsePortText(text) {
    if (typeof text !== "string" || text.length === 0 || !/^[0-9]+$/.test(text)) {
        throw new TypeError("TCP port text must contain decimal digits.");
    }
    return normalizePort(Number(text), true);
}

function normalizeConnectOptions(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("TcpClient.connect options must be a plain object.");
    }
    if (typeof options.host !== "string" || options.host.length === 0) {
        throw new TypeError("TcpClient.connect host must be a non-empty string.");
    }
    const normalized = {
        host: options.host,
        port: normalizePort(options.port, false),
        noDelay: options.noDelay === true,
    };
    if (options.timeoutMs !== undefined) {
        if (!Number.isFinite(options.timeoutMs) || options.timeoutMs < 0) {
            throw new TypeError("TcpClient.connect timeoutMs must be a non-negative number.");
        }
        normalized.timeoutMs = Math.ceil(options.timeoutMs);
    }
    if (options.keepAlive !== undefined) {
        if (!isPlainObject(options.keepAlive) || typeof options.keepAlive.enabled !== "boolean") {
            throw new TypeError("TcpClient.connect keepAlive must be { enabled, delayMs? }.");
        }
        normalized.keepAlive = { enabled: options.keepAlive.enabled };
        if (options.keepAlive.delayMs !== undefined) {
            if (!Number.isFinite(options.keepAlive.delayMs) || options.keepAlive.delayMs < 0) {
                throw new TypeError("TcpClient.connect keepAlive.delayMs must be non-negative.");
            }
            normalized.keepAlive.delayMs = Math.ceil(options.keepAlive.delayMs);
        }
    }
    return normalized;
}

function normalizeListenOptions(options) {
    if (!isPlainObject(options)) {
        throw new TypeError("TcpListener.listen options must be a plain object.");
    }
    if (typeof options.host !== "string" || options.host.length === 0) {
        throw new TypeError("TcpListener.listen host must be a non-empty string.");
    }
    const normalized = {
        host: options.host,
        port: normalizePort(options.port, true),
    };
    if (options.backlog !== undefined) {
        if (!Number.isInteger(options.backlog) || options.backlog < 1) {
            throw new TypeError("TcpListener.listen backlog must be a positive integer.");
        }
        normalized.backlog = options.backlog;
    }
    return normalized;
}

function normalizeLocalEndpointPath(path, operation) {
    if (typeof path !== "string" || path.length === 0) {
        throw new TypeError(`${operation} path must be a non-empty string.`);
    }
    if (path.includes("\0")) {
        throw new TypeError(`${operation} path must not contain NUL bytes.`);
    }
    if (/^[A-Za-z]:[\\/]/.test(path) || path.startsWith("/") || path.startsWith("\\\\")) {
        throw new TypeError(`${operation} path must use a configured named root such as runtime:/.`);
    }
    const separator = path.indexOf(":/");
    if (separator <= 0) {
        throw new TypeError(`${operation} path must use a named-root path such as runtime:/my-app.sock.`);
    }
    const root = path.slice(0, separator);
    const rest = path.slice(separator + 2);
    if (!/^[A-Za-z][A-Za-z0-9_.-]*$/.test(root)) {
        throw new TypeError(`${operation} named root is invalid.`);
    }
    const segments = rest.split("/");
    if (
        rest.length === 0 ||
        rest.startsWith("/") ||
        rest.endsWith("/") ||
        rest.includes("\\") ||
        segments.some(
            (segment) =>
                segment.length === 0 ||
                segment === "." ||
                segment === ".." ||
                !/^[A-Za-z0-9_.-]+$/.test(segment),
        )
    ) {
        throw new TypeError(`${operation} path must stay inside the named root.`);
    }
    return path;
}

function isLocalCancellationSignal(value) {
    return (
        value !== undefined &&
        value !== null &&
        typeof value === "object" &&
        typeof value.aborted === "boolean"
    );
}

function subscribeLocalCancellation(signal, listener) {
    if (!isLocalCancellationSignal(signal)) {
        return () => {};
    }
    if (signal.aborted) {
        listener(signal.reason);
        return () => {};
    }
    if (typeof signal._subscribe === "function") {
        return signal._subscribe(listener);
    }
    if (typeof signal.addEventListener === "function") {
        const wrapped = () => listener(signal.reason);
        signal.addEventListener("abort", wrapped);
        return () => signal.removeEventListener?.("abort", wrapped);
    }
    return () => {};
}

function localOperationCode(operation) {
    if (operation.includes(".accept")) {
        return "SLOPPY_E_NET_LOCAL_IPC_ACCEPT_CANCELLED";
    }
    if (operation.includes(".read") || operation.includes(".write")) {
        return "SLOPPY_E_NET_LOCAL_IPC_READ_WRITE_CANCELLED";
    }
    if (operation.includes(".listen")) {
        return "SLOPPY_E_NET_LOCAL_IPC_LISTEN_FAILED";
    }
    return "SLOPPY_E_NET_LOCAL_IPC_CONNECT_FAILED";
}

function localCancelledError(operation, reason = undefined) {
    return new SloppyNetError(
        "LocalEndpointCancelledError",
        `${localOperationCode(operation)}: local IPC operation was cancelled.`,
        { cause: reason },
    );
}

function localTimeoutError(operation) {
    return new SloppyNetError(
        "LocalEndpointTimeoutError",
        `${localOperationCode(operation)}: local IPC operation timed out.`,
    );
}

function localDeadlineRemainingMs(deadline, operation) {
    if (deadline === undefined || deadline === null) {
        return Infinity;
    }
    if (typeof deadline !== "object" || typeof deadline.remainingMs !== "function") {
        throw new TypeError(`${operation} deadline must come from sloppy/time Deadline.`);
    }
    const remainingMs = deadline.remainingMs();
    if (!Number.isFinite(remainingMs) && remainingMs !== Infinity) {
        throw new TypeError(`${operation} deadline remaining time must be finite or Infinity.`);
    }
    return Math.max(0, Math.ceil(remainingMs));
}

function normalizeLocalTimingOptions(options, operation, allowedKeys) {
    let timeoutMs = Infinity;
    const signal = options?.signal;

    if (options !== undefined) {
        if (!isPlainObject(options)) {
            throw new TypeError(`${operation} options must be a plain object.`);
        }
        for (const key of Object.keys(options)) {
            if (!allowedKeys.has(key)) {
                throw new TypeError(`${operation} option '${key}' is not supported.`);
            }
        }
    }
    if (signal !== undefined && !isLocalCancellationSignal(signal)) {
        throw new TypeError(`${operation} signal must be a Sloppy cancellation signal or AbortSignal-like object.`);
    }
    if (signal?.aborted === true) {
        throw localCancelledError(operation, signal.reason);
    }
    if (options?.timeoutMs !== undefined) {
        if (
            !Number.isFinite(options.timeoutMs) ||
            options.timeoutMs < 0 ||
            options.timeoutMs > 0xffffffff
        ) {
            throw new TypeError(`${operation} timeoutMs must be a non-negative uint32 value.`);
        }
        timeoutMs = Math.ceil(options.timeoutMs);
    }
    timeoutMs = Math.min(timeoutMs, localDeadlineRemainingMs(options?.deadline, operation));
    if (timeoutMs <= 0) {
        throw localTimeoutError(operation);
    }
    return {
        signal,
        timeoutMs: timeoutMs === Infinity ? undefined : timeoutMs,
    };
}

function raceLocalOperation(promise, timing, operation, hooks = {}) {
    if (timing.signal === undefined && timing.timeoutMs === undefined) {
        return promise;
    }
    let settled = false;
    let timeoutId = undefined;
    let cleanupSignal = () => {};
    return new Promise((resolve, reject) => {
        const finish = (callback, value) => {
            if (settled) {
                return;
            }
            settled = true;
            cleanupSignal();
            if (timeoutId !== undefined) {
                clearTimeout(timeoutId);
            }
            callback(value);
        };
        cleanupSignal = subscribeLocalCancellation(timing.signal, (reason) => {
            hooks.onCancel?.();
            finish(reject, localCancelledError(operation, reason));
        });
        if (timing.timeoutMs !== undefined) {
            timeoutId = setTimeout(() => {
                hooks.onTimeout?.();
                finish(reject, localTimeoutError(operation));
            }, timing.timeoutMs);
        }
        promise.then(
            (value) => {
                if (settled) {
                    hooks.onLateSuccess?.(value);
                    return;
                }
                finish(resolve, value);
            },
            (error) => {
                if (settled) {
                    return;
                }
                finish(reject, error);
            },
        );
    });
}

function normalizePermissionMode(value, operation) {
    if (value === undefined) {
        return undefined;
    }
    if (typeof value !== "string" || !/^0[0-7]{3}$/.test(value)) {
        throw new TypeError(`${operation} permissions must be an octal string such as "0600".`);
    }
    return value;
}

function normalizeLocalConnectOptions(options, operation) {
    if (!isPlainObject(options)) {
        throw new TypeError(`${operation} options must be a plain object.`);
    }
    const timing = normalizeLocalTimingOptions(
        options,
        operation,
        new Set(["path", "backend", "timeoutMs", "deadline", "signal"]),
    );
    const normalized = { path: normalizeLocalEndpointPath(options.path, operation) };
    if (options.backend !== undefined) {
        if (options.backend !== "unix" && options.backend !== "namedPipe") {
            throw new TypeError(`${operation} backend must be "unix" or "namedPipe" when specified.`);
        }
        normalized.backend = options.backend;
    }
    if (timing.timeoutMs !== undefined) {
        normalized.timeoutMs = timing.timeoutMs;
    }
    return { bridgeOptions: normalized, timing };
}

function normalizeLocalListenOptions(options, operation) {
    if (!isPlainObject(options)) {
        throw new TypeError(`${operation} options must be a plain object.`);
    }
    const timing = normalizeLocalTimingOptions(
        options,
        operation,
        new Set([
            "path",
            "backend",
            "timeoutMs",
            "deadline",
            "signal",
            "unlinkExisting",
            "permissions",
            "backlog",
        ]),
    );
    const normalized = { path: normalizeLocalEndpointPath(options.path, operation) };
    if (options.backend !== undefined) {
        if (options.backend !== "unix" && options.backend !== "namedPipe") {
            throw new TypeError(`${operation} backend must be "unix" or "namedPipe" when specified.`);
        }
        normalized.backend = options.backend;
    }
    if (options.unlinkExisting !== undefined && typeof options.unlinkExisting !== "boolean") {
        throw new TypeError(`${operation} unlinkExisting must be a boolean.`);
    }
    if (options.backlog !== undefined && (!Number.isInteger(options.backlog) || options.backlog < 1)) {
        throw new TypeError(`${operation} backlog must be a positive integer.`);
    }
    normalized.unlinkExisting = options.unlinkExisting === true;
    normalized.permissions = normalizePermissionMode(options.permissions, operation);
    if (options.backlog !== undefined) {
        normalized.backlog = options.backlog;
    }
    if (timing.timeoutMs !== undefined) {
        normalized.timeoutMs = timing.timeoutMs;
    }
    return { bridgeOptions: normalized, timing };
}

function localIpcUnavailable() {
    throw new SloppyNetError(
        "LocalIpcFeatureUnavailableError",
        "SLOPPY_E_NET_LOCAL_IPC_FEATURE_UNAVAILABLE: local IPC is specified but no Unix socket or named pipe backend is active.",
    );
}

class LocalEndpointServer {
    constructor(bridge, handle) {
        this._bridge = bridge;
        this._handle = handle;
        this._closed = false;
    }

    get closed() {
        return this._closed;
    }

    _acceptOne(options = undefined) {
        if (this._closed) {
            throw new SloppyNetError("LocalEndpointDisposedError", "Local endpoint server is closed.");
        }
        const timing = normalizeLocalTimingOptions(
            options,
            "LocalEndpoint.accept",
            new Set(["timeoutMs", "deadline", "signal"]),
        );
        if (typeof this._bridge.acceptLocal !== "function") {
            localIpcUnavailable();
        }
        const promise = this._bridge.acceptLocal(this._handle, timing.timeoutMs);
        return raceLocalOperation(promise, timing, "LocalEndpoint.accept", {
            onLateSuccess: (handle) => this._bridge.abortLocal?.(handle).catch(() => {}),
        }).then((handle) => new LocalEndpointConnection(this._bridge, handle));
    }

    accept(options = undefined) {
        return new LocalEndpointAcceptOperation(this, options);
    }

    acceptLoop(options = undefined) {
        return this.accept(options);
    }

    async close() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        if (typeof this._bridge.closeLocalServer === "function") {
            await this._bridge.closeLocalServer(this._handle);
        }
    }

    async abort() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        if (typeof this._bridge.abortLocalServer === "function") {
            await this._bridge.abortLocalServer(this._handle);
        }
    }
}

class LocalEndpointAcceptOperation {
    constructor(server, options) {
        this._server = server;
        this._options = options;
        this._promise = undefined;
    }

    _one() {
        if (this._promise === undefined) {
            this._promise = this._server._acceptOne(this._options);
        }
        return this._promise;
    }

    then(onFulfilled, onRejected) {
        return this._one().then(onFulfilled, onRejected);
    }

    catch(onRejected) {
        return this._one().catch(onRejected);
    }

    finally(onFinally) {
        return this._one().finally(onFinally);
    }

    [Symbol.asyncIterator]() {
        const server = this._server;
        const options = this._options;
        return {
            async next() {
                if (server.closed) {
                    return { done: true, value: undefined };
                }
                return { done: false, value: await server._acceptOne(options) };
            },
        };
    }
}

class LocalEndpointConnection {
    constructor(bridge, handle) {
        this._bridge = bridge;
        this._handle = handle;
        this._closed = false;
    }

    get closed() {
        return this._closed;
    }

    async write(bytes, options = undefined) {
        if (this._closed) {
            throw new SloppyNetError("LocalEndpointDisposedError", "Local endpoint connection is closed.");
        }
        if (!(bytes instanceof Uint8Array)) {
            throw new TypeError("LocalEndpointConnection.write requires a Uint8Array.");
        }
        const timing = normalizeLocalTimingOptions(
            options,
            "LocalEndpoint.write",
            new Set(["timeoutMs", "deadline", "signal"]),
        );
        if (typeof this._bridge.writeLocal !== "function") {
            localIpcUnavailable();
        }
        await raceLocalOperation(
            this._bridge.writeLocal(this._handle, bytes, timing.timeoutMs),
            timing,
            "LocalEndpoint.write",
        );
    }

    async writeText(text, options = undefined) {
        if (typeof text !== "string") {
            throw new TypeError("LocalEndpointConnection.writeText requires a string.");
        }
        await this.write(Text.utf8.encode(text), options);
    }

    async read(options = undefined) {
        if (this._closed) {
            throw new SloppyNetError("LocalEndpointDisposedError", "Local endpoint connection is closed.");
        }
        const maxBytes = normalizeMaxBytesOption(options, "LocalEndpoint.read");
        const timing = normalizeLocalTimingOptions(
            options,
            "LocalEndpoint.read",
            new Set(["maxBytes", "timeoutMs", "deadline", "signal"]),
        );
        if (typeof this._bridge.readLocal !== "function") {
            localIpcUnavailable();
        }
        return raceLocalOperation(
            this._bridge.readLocal(this._handle, maxBytes, timing.timeoutMs),
            timing,
            "LocalEndpoint.read",
        );
    }

    async readUntil(delimiter, options = undefined) {
        if (this._closed) {
            throw new SloppyNetError("LocalEndpointDisposedError", "Local endpoint connection is closed.");
        }
        const delimiterBytes =
            typeof delimiter === "string" ? Text.utf8.encode(delimiter) : delimiter;
        if (!(delimiterBytes instanceof Uint8Array) || delimiterBytes.byteLength === 0) {
            throw new TypeError("LocalEndpointConnection.readUntil delimiter must be non-empty bytes.");
        }
        const maxBytes = normalizeMaxBytesOption(options, "LocalEndpoint.readUntil");
        const timing = normalizeLocalTimingOptions(
            options,
            "LocalEndpoint.readUntil",
            new Set(["maxBytes", "timeoutMs", "deadline", "signal"]),
        );
        if (typeof this._bridge.readUntilLocal !== "function") {
            localIpcUnavailable();
        }
        return raceLocalOperation(
            this._bridge.readUntilLocal(this._handle, delimiterBytes, maxBytes, timing.timeoutMs),
            timing,
            "LocalEndpoint.readUntil",
        );
    }

    async readLine(options = undefined) {
        if (this._closed) {
            throw new SloppyNetError("LocalEndpointDisposedError", "Local endpoint connection is closed.");
        }
        const maxBytes = normalizeMaxBytesOption(options, "LocalEndpoint.readLine");
        const timing = normalizeLocalTimingOptions(
            options,
            "LocalEndpoint.readLine",
            new Set(["maxBytes", "timeoutMs", "deadline", "signal"]),
        );
        if (typeof this._bridge.readLineLocal !== "function") {
            localIpcUnavailable();
        }
        return raceLocalOperation(
            this._bridge.readLineLocal(this._handle, maxBytes, timing.timeoutMs),
            timing,
            "LocalEndpoint.readLine",
        );
    }

    async *readChunks(options = undefined) {
        while (!this._closed) {
            yield await this.read(options);
        }
    }

    async close() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        if (typeof this._bridge.closeLocal !== "function") {
            localIpcUnavailable();
        }
        await this._bridge.closeLocal(this._handle);
    }

    async abort() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        if (typeof this._bridge.abortLocal !== "function") {
            localIpcUnavailable();
        }
        await this._bridge.abortLocal(this._handle);
    }
}

const LocalEndpoint = Object.freeze({
    async connect(options) {
        const bridge = requireNetBridge();
        const { bridgeOptions, timing } = normalizeLocalConnectOptions(options, "LocalEndpoint.connect");
        if (typeof bridge.connectLocal !== "function") {
            localIpcUnavailable();
        }
        const handle = await raceLocalOperation(
            bridge.connectLocal(bridgeOptions),
            timing,
            "LocalEndpoint.connect",
            {
                onLateSuccess: (lateHandle) => bridge.abortLocal?.(lateHandle).catch(() => {}),
            },
        );
        return new LocalEndpointConnection(bridge, handle);
    },

    async listen(options) {
        const bridge = requireNetBridge();
        const { bridgeOptions, timing } = normalizeLocalListenOptions(options, "LocalEndpoint.listen");
        if (typeof bridge.listenLocal !== "function") {
            localIpcUnavailable();
        }
        const handle = await raceLocalOperation(
            bridge.listenLocal(bridgeOptions),
            timing,
            "LocalEndpoint.listen",
            {
                onLateSuccess: (lateHandle) => bridge.abortLocalServer?.(lateHandle).catch(() => {}),
            },
        );
        return new LocalEndpointServer(bridge, handle);
    },
});

const UnixSocket = Object.freeze({
    async connect(options) {
        return LocalEndpoint.connect({ ...options, backend: "unix" });
    },

    async listen(options) {
        return LocalEndpoint.listen({ ...options, backend: "unix" });
    },
});

const NamedPipe = Object.freeze({
    async connect(options) {
        return LocalEndpoint.connect({ ...options, backend: "namedPipe" });
    },

    async listen(options) {
        return LocalEndpoint.listen({ ...options, backend: "namedPipe" });
    },
});

function normalizeTimeoutOption(options, operation, extraKeys = []) {
    if (options === undefined) {
        return undefined;
    }
    if (!isPlainObject(options)) {
        throw new TypeError(`${operation} options must be a plain object.`);
    }
    const allowed = new Set(["timeoutMs", ...extraKeys]);
    for (const key of Object.keys(options)) {
        if (!allowed.has(key)) {
            throw new TypeError(`${operation} option '${key}' is not supported.`);
        }
    }
    if (options.timeoutMs === undefined) {
        return undefined;
    }
    if (!Number.isFinite(options.timeoutMs) || options.timeoutMs < 0) {
        throw new TypeError(`${operation} timeoutMs must be a non-negative number.`);
    }
    return Math.ceil(options.timeoutMs);
}

function normalizeMaxBytesOption(options, operation) {
    if (options === undefined || options.maxBytes === undefined) {
        return 8192;
    }
    if (!Number.isInteger(options.maxBytes) || options.maxBytes < 1 || options.maxBytes > 64 * 1024) {
        throw new TypeError(`${operation} maxBytes must be an integer from 1 to 65536.`);
    }
    return options.maxBytes;
}

class TcpConnection {
    constructor(bridge, handle) {
        this._bridge = bridge;
        this._handle = handle;
        this._closed = false;
    }

    get closed() {
        return this._closed;
    }

    async write(bytes) {
        if (this._closed) {
            throw new SloppyNetError("ConnectionClosedError", "TCP connection is closed.");
        }
        if (!(bytes instanceof Uint8Array)) {
            throw new TypeError("TcpConnection.write requires a Uint8Array.");
        }
        await this._bridge.write(this._handle, bytes);
    }

    async writeText(text) {
        if (typeof text !== "string") {
            throw new TypeError("TcpConnection.writeText requires a string.");
        }
        await this.write(Text.utf8.encode(text));
    }

    async read(options = undefined) {
        if (this._closed) {
            throw new SloppyNetError("ConnectionClosedError", "TCP connection is closed.");
        }
        const maxBytes = options?.maxBytes;
        if (maxBytes !== undefined && (!Number.isInteger(maxBytes) || maxBytes < 1)) {
            throw new TypeError("TcpConnection.read maxBytes must be a positive integer.");
        }
        return this._bridge.read(this._handle, maxBytes ?? 8192);
    }

    async readUntil(delimiter, options = undefined) {
        const delimiterBytes =
            typeof delimiter === "string" ? Text.utf8.encode(delimiter) : delimiter;
        if (!(delimiterBytes instanceof Uint8Array) || delimiterBytes.byteLength === 0) {
            throw new TypeError("TcpConnection.readUntil delimiter must be non-empty bytes.");
        }
        const maxBytes = options?.maxBytes ?? 8192;
        return this._bridge.readUntil(this._handle, delimiterBytes, maxBytes);
    }

    async readLine(options = undefined) {
        return this._bridge.readLine(this._handle, options?.maxBytes ?? 8192);
    }

    async *readChunks(options = undefined) {
        const maxBytes = options?.maxBytes ?? 8192;
        while (!this._closed) {
            yield await this.read({ maxBytes });
        }
    }

    async close() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        await this._bridge.close(this._handle);
    }

    async abort() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        await this._bridge.abort(this._handle);
    }
}

const TcpClient = Object.freeze({
    async connect(options) {
        const bridge = requireNetBridge();
        const handle = await bridge.connect(normalizeConnectOptions(options));
        return new TcpConnection(bridge, handle);
    },
});

class TcpListenerResource {
    constructor(bridge, handle) {
        this._bridge = bridge;
        this._handle = handle;
        this._closed = false;
    }

    get closed() {
        return this._closed;
    }

    async accept(options = undefined) {
        if (this._closed) {
            throw new SloppyNetError("ListenerClosedError", "TCP listener is closed.");
        }
        const timeoutMs = normalizeTimeoutOption(options, "TcpListener.accept");
        const handle = await this._bridge.accept(this._handle, timeoutMs);
        return new TcpConnection(this._bridge, handle);
    }

    async *[Symbol.asyncIterator]() {
        while (!this._closed) {
            yield await this.accept();
        }
    }

    acceptLoop(options = undefined) {
        const listener = this;
        return {
            async *[Symbol.asyncIterator]() {
                while (!listener.closed) {
                    yield await listener.accept(options);
                }
            },
        };
    }

    async close() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        await this._bridge.closeListener(this._handle);
    }

    async abort() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        await this._bridge.abortListener(this._handle);
    }
}

const TcpListener = Object.freeze({
    async listen(options) {
        const bridge = requireNetBridge();
        const handle = await bridge.listen(normalizeListenOptions(options));
        return new TcpListenerResource(bridge, handle);
    },
});

const HTTP_CLIENT_DEFAULT_MAX_HEADER_BYTES = 16 * 1024;
const HTTP_CLIENT_DEFAULT_MAX_RESPONSE_BYTES = 1024 * 1024;
const HTTP_CLIENT_DEFAULT_MAX_REQUEST_BYTES = 1024 * 1024;
const HTTP_CLIENT_DEFAULT_MAX_REDIRECTS = 5;
const HTTP_CLIENT_DEFAULT_POOL_IDLE_TIMEOUT_MS = 30000;
const HTTP_CLIENT_DEFAULT_MAX_CONNECTIONS_PER_ORIGIN = 8;
const HTTP_CLIENT_PROTOCOLS = new Set(["auto", "http/1.1", "h2", "h2c"]);
const HTTP_CLIENT_TLS_OPTION_KEYS = new Set([
    "caPath",
    "caBundlePath",
    "trustStorePath",
    "clientCertificatePath",
    "clientPrivateKeyPath",
    "clientPrivateKeyPassphrase",
    "insecureSkipVerify",
]);
const HTTP_CLIENT_TLS_STRING_OPTION_KEYS = new Set([
    "caPath",
    "caBundlePath",
    "trustStorePath",
    "clientCertificatePath",
    "clientPrivateKeyPath",
    "clientPrivateKeyPassphrase",
]);
const HTTP_CLIENT_SENSITIVE_HEADERS = new Set([
    "authorization",
    "cookie",
    "proxy-authorization",
    "x-api-key",
    "api-key",
]);
const HTTP_CLIENT_SIZE_UNITS = Object.freeze({
    b: 1,
    kb: 1024,
    mb: 1024 * 1024,
    gb: 1024 * 1024 * 1024,
});

function httpClientError(name, code, message, options = undefined) {
    return new SloppyNetError(name, `${code}: ${message}`, options);
}

function httpClientUnavailable(operation) {
    return Promise.reject(
        httpClientError(
            "HttpClientUnavailableError",
            "SLOPPY_E_HTTP_CLIENT_FEATURE_UNAVAILABLE",
            `HttpClient.${operation} requires the outbound HTTP client transport lane.`,
        ),
    );
}

function parseHttpSize(value, operation) {
    if (value === undefined) {
        return undefined;
    }
    if (Number.isInteger(value) && value >= 0) {
        return value;
    }
    if (typeof value === "string") {
        const match = /^([0-9]+)\s*(b|kb|mb|gb)$/i.exec(value.trim());
        if (match !== null) {
            const size = Number(match[1]) * HTTP_CLIENT_SIZE_UNITS[match[2].toLowerCase()];
            if (Number.isSafeInteger(size)) {
                return size;
            }
        }
    }
    throw httpClientError(
        "HttpClientInvalidOptionsError",
        "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
        `${operation} size option must be a non-negative integer byte count or size string like "4mb".`,
    );
}

function hasHttpControlChars(value) {
    for (let index = 0; index < value.length; index += 1) {
        const code = value.charCodeAt(index);
        if (code < 0x20 || code === 0x7f) {
            return true;
        }
    }
    return false;
}

function setHttpDefaultHeader(headers, name, value) {
    const normalizedName = name.toLowerCase();
    if (!headers.has(normalizedName)) {
        headers.set(normalizedName, { name, value });
    }
}

function normalizeHttpHostHeaderForOrigin(hostHeader) {
    if (hostHeader.startsWith("[")) {
        const close = hostHeader.indexOf("]");
        if (close > 0) {
            return `[${hostHeader.slice(1, close).toLowerCase()}]${hostHeader.slice(close + 1)}`;
        }
    }
    const colon = hostHeader.lastIndexOf(":");
    if (colon > 0) {
        return `${hostHeader.slice(0, colon).toLowerCase()}${hostHeader.slice(colon)}`;
    }
    return hostHeader.toLowerCase();
}

function httpOriginKey(url) {
    return `${url.scheme}://${normalizeHttpHostHeaderForOrigin(url.hostHeader)}`;
}

function httpUrlToString(url) {
    return `${url.scheme}://${url.hostHeader}${url.target}`;
}

function cloneHttpHeaders(headers) {
    const cloned = new Map();
    for (const [name, entry] of headers.entries()) {
        cloned.set(name, { name: entry.name, value: entry.value });
    }
    return cloned;
}

function parseHttpSecretHeaders(value, operation) {
    if (value === undefined) {
        return [];
    }
    if (!Array.isArray(value)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} secretHeaders must be an array of header names.`,
        );
    }
    return value.map((name) => {
        if (typeof name !== "string" || !/^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/.test(name)) {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} secretHeaders entries must be valid header names.`,
            );
        }
        return name.toLowerCase();
    });
}

function isHttpSensitiveHeader(name, value, secretHeaders) {
    const normalized = name.toLowerCase();
    return (
        HTTP_CLIENT_SENSITIVE_HEADERS.has(normalized) ||
        secretHeaders.has(normalized) ||
        /token|secret|api[-_]?key/i.test(name) ||
        /bearer\s+/i.test(value)
    );
}

function stripHttpSensitiveHeaders(headers, policy) {
    const stripped = [];
    for (const [name, entry] of Array.from(headers.entries())) {
        if (isHttpSensitiveHeader(name, entry.value, policy.secretHeaders)) {
            stripped.push(entry.name);
            headers.delete(name);
        }
    }
    if (stripped.length > 0 && policy.crossOriginSensitiveHeaders === "deny") {
        throw httpClientError(
            "HttpClientSensitiveHeaderError",
            "SLOPPY_E_HTTP_CLIENT_SENSITIVE_HEADER_STRIPPED",
            "HTTP client cross-origin redirect refused sensitive headers.",
        );
    }
    return stripped;
}

function normalizeHttpRedirectPolicy(baseOptions, requestObject, operation) {
    const raw = requestObject.redirects ?? baseOptions?.redirects;
    let enabled = true;
    let max = HTTP_CLIENT_DEFAULT_MAX_REDIRECTS;
    let allowPost = false;
    let crossOriginSensitiveHeaders = "strip";
    const secretHeaders = [
        ...parseHttpSecretHeaders(baseOptions?.secretHeaders, operation),
        ...parseHttpSecretHeaders(requestObject.secretHeaders, operation),
    ];

    if (raw === false) {
        enabled = false;
    } else if (raw === true || raw === undefined) {
        enabled = true;
    } else if (isPlainObject(raw)) {
        if (raw.enabled !== undefined) {
            if (typeof raw.enabled !== "boolean") {
                throw httpClientError(
                    "HttpClientInvalidOptionsError",
                    "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                    `${operation} redirects.enabled must be a boolean.`,
                );
            }
            enabled = raw.enabled;
        }
        if (raw.max !== undefined) {
            if (!Number.isInteger(raw.max) || raw.max < 0 || raw.max > 20) {
                throw httpClientError(
                    "HttpClientInvalidOptionsError",
                    "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                    `${operation} redirects.max must be an integer from 0 to 20.`,
                );
            }
            max = raw.max;
        }
        if (raw.allowPost !== undefined) {
            if (typeof raw.allowPost !== "boolean") {
                throw httpClientError(
                    "HttpClientInvalidOptionsError",
                    "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                    `${operation} redirects.allowPost must be a boolean.`,
                );
            }
            allowPost = raw.allowPost;
        }
        if (raw.crossOriginSensitiveHeaders !== undefined) {
            if (raw.crossOriginSensitiveHeaders !== "strip" && raw.crossOriginSensitiveHeaders !== "deny") {
                throw httpClientError(
                    "HttpClientInvalidOptionsError",
                    "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                    `${operation} redirects.crossOriginSensitiveHeaders must be "strip" or "deny".`,
                );
            }
            crossOriginSensitiveHeaders = raw.crossOriginSensitiveHeaders;
        }
        secretHeaders.push(...parseHttpSecretHeaders(raw.secretHeaders, operation));
    } else {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} redirects must be a boolean or policy object.`,
        );
    }

    return Object.freeze({
        enabled,
        max,
        allowPost,
        crossOriginSensitiveHeaders,
        secretHeaders: new Set(secretHeaders),
    });
}

function normalizeHttpPoolOptions(value, operation) {
    if (value === undefined || value === null || value === false) {
        return undefined;
    }
    if (value !== true && !isPlainObject(value)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} pool must be a policy object.`,
        );
    }
    const raw = value === true ? {} : value;
    const maxConnectionsPerOrigin =
        raw.maxConnectionsPerOrigin ?? HTTP_CLIENT_DEFAULT_MAX_CONNECTIONS_PER_ORIGIN;
    const idleTimeoutMs = raw.idleTimeoutMs ?? HTTP_CLIENT_DEFAULT_POOL_IDLE_TIMEOUT_MS;
    const connectionLifetimeMs = raw.connectionLifetimeMs ?? undefined;
    const pendingQueueLimit = raw.pendingQueueLimit ?? 0;
    const pendingQueueTimeoutMs = raw.pendingQueueTimeoutMs ?? 1000;
    if (!Number.isInteger(maxConnectionsPerOrigin) || maxConnectionsPerOrigin < 1 || maxConnectionsPerOrigin > 256) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} pool.maxConnectionsPerOrigin must be an integer from 1 to 256.`,
        );
    }
    if (!Number.isInteger(idleTimeoutMs) || idleTimeoutMs < 0) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} pool.idleTimeoutMs must be a non-negative integer.`,
        );
    }
    if (connectionLifetimeMs !== undefined &&
        (!Number.isInteger(connectionLifetimeMs) || connectionLifetimeMs < 0))
    {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} pool.connectionLifetimeMs must be a non-negative integer.`,
        );
    }
    if (!Number.isInteger(pendingQueueLimit) || pendingQueueLimit < 0 || pendingQueueLimit > 100000) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} pool.pendingQueueLimit must be an integer from 0 to 100000.`,
        );
    }
    if (!Number.isInteger(pendingQueueTimeoutMs) || pendingQueueTimeoutMs < 0) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} pool.pendingQueueTimeoutMs must be a non-negative integer.`,
        );
    }
    return Object.freeze({
        maxConnectionsPerOrigin,
        idleTimeoutMs,
        connectionLifetimeMs,
        pendingQueueLimit,
        pendingQueueTimeoutMs,
    });
}

function normalizeHttpTlsOptions(value, operation) {
    if (value === undefined) {
        return Object.freeze({});
    }
    if (!isPlainObject(value)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} tls must be a plain object when provided.`,
        );
    }
    for (const key of Object.keys(value)) {
        if (!HTTP_CLIENT_TLS_OPTION_KEYS.has(key)) {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} tls option ${key} is not supported.`,
            );
        }
        if (HTTP_CLIENT_TLS_STRING_OPTION_KEYS.has(key) && typeof value[key] !== "string") {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} tls option ${key} must be a string.`,
            );
        }
        if (key === "insecureSkipVerify" && typeof value[key] !== "boolean") {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} tls option insecureSkipVerify must be a boolean.`,
            );
        }
    }
    return Object.freeze({ ...value });
}

function hasHttpTlsOption(tls, key) {
    return Object.prototype.hasOwnProperty.call(tls, key);
}

function hasHttpTlsOptions(tls) {
    return tls !== undefined && tls !== null && Object.keys(tls).length > 0;
}

function assertHttpTlsAllowedForScheme(url, tls, operation) {
    if (url.scheme === "https" || !hasHttpTlsOptions(tls)) {
        return;
    }
    throw httpClientError(
        "HttpClientInvalidOptionsError",
        "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
        `${operation} tls options are only valid for https:// URLs.`,
    );
}

function sanitizeHttpTlsDescriptor(tls) {
    if (!hasHttpTlsOptions(tls)) {
        return Object.freeze({ enabled: false });
    }
    return Object.freeze({
        enabled: true,
        hasCaPath: hasHttpTlsOption(tls, "caPath"),
        hasCaBundlePath: hasHttpTlsOption(tls, "caBundlePath"),
        hasTrustStorePath: hasHttpTlsOption(tls, "trustStorePath"),
        hasClientCertificate:
            hasHttpTlsOption(tls, "clientCertificatePath") ||
            hasHttpTlsOption(tls, "clientPrivateKeyPath"),
        hasClientPrivateKeyPassphrase: hasHttpTlsOption(tls, "clientPrivateKeyPassphrase"),
        insecureSkipVerify: tls.insecureSkipVerify === true,
    });
}

function sanitizeHttpClientOptionsDescriptor(baseOptions, normalizedTls, poolOptions) {
    if (baseOptions === undefined) {
        return undefined;
    }
    const descriptor = { ...baseOptions };
    if (Object.prototype.hasOwnProperty.call(baseOptions, "tls")) {
        descriptor.tls = sanitizeHttpTlsDescriptor(normalizedTls);
    } else {
        delete descriptor.tls;
    }
    if (poolOptions !== undefined) {
        descriptor.pool = poolOptions;
    }
    return Object.freeze(descriptor);
}

function createHttpClientBaseOptions(baseOptions, normalizedTls, poolOptions) {
    if (baseOptions === undefined) {
        return undefined;
    }
    const internal = { ...baseOptions, tls: normalizedTls };
    if (poolOptions !== undefined) {
        internal.pool = poolOptions;
    }
    return Object.freeze(internal);
}

function assertHttpTlsBridgeCapability(bridge, tls, key, capability, operation) {
    if (!hasHttpTlsOption(tls, key) || bridge[capability] === true) {
        return;
    }
    throw httpClientError(
        "HttpClientInvalidOptionsError",
        "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
        `${operation} tls option ${key} is not supported by the active TLS bridge.`,
    );
}

function assertHttpTlsBridgeCapabilities(bridge, tls, operation) {
    if (!hasHttpTlsOptions(tls)) {
        return;
    }
    assertHttpTlsBridgeCapability(bridge, tls, "caPath", "tlsCaPath", operation);
    assertHttpTlsBridgeCapability(bridge, tls, "caBundlePath", "tlsCaBundlePath", operation);
    assertHttpTlsBridgeCapability(bridge, tls, "trustStorePath", "tlsTrustStorePath", operation);
    assertHttpTlsBridgeCapability(
        bridge,
        tls,
        "clientCertificatePath",
        "tlsClientCertificate",
        operation,
    );
    assertHttpTlsBridgeCapability(
        bridge,
        tls,
        "clientPrivateKeyPath",
        "tlsClientCertificate",
        operation,
    );
    assertHttpTlsBridgeCapability(
        bridge,
        tls,
        "clientPrivateKeyPassphrase",
        "tlsClientCertificate",
        operation,
    );
    assertHttpTlsBridgeCapability(
        bridge,
        tls,
        "insecureSkipVerify",
        "tlsInsecureSkipVerify",
        operation,
    );
}

function normalizeHttpProtocol(baseOptions, requestObject, operation) {
    const raw = requestObject.protocol ?? baseOptions?.protocol ?? "auto";
    if (typeof raw === "string" && HTTP_CLIENT_PROTOCOLS.has(raw)) {
        return raw;
    }
    throw httpClientError(
        "HttpClientInvalidOptionsError",
        "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
        `${operation} protocol must be "auto", "http/1.1", "h2", or "h2c".`,
    );
}

function normalizeHttpNetworkPolicy(baseOptions, requestObject, operation) {
    const raw = requestObject.network ?? baseOptions?.network;
    if (raw === undefined || raw === null || raw === false) {
        return Object.freeze({ strict: false, allowedOrigins: new Set() });
    }
    if (!isPlainObject(raw)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} network must be a policy object.`,
        );
    }
    const strict = raw.strict === true;
    const allowed = raw.allow ?? raw.allowedOrigins ?? [];
    if (!Array.isArray(allowed)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} network.allow must be an array of origins.`,
        );
    }
    const allowedOrigins = new Set();
    for (const origin of allowed) {
        if (origin === "*") {
            allowedOrigins.add("*");
            continue;
        }
        const parsed = parseAbsoluteHttpUrl(origin, operation);
        allowedOrigins.add(httpOriginKey(parsed));
    }
    return Object.freeze({ strict, allowedOrigins });
}

function assertHttpNetworkAllowed(policy, url) {
    if (!policy.strict) {
        return;
    }
    const origin = httpOriginKey(url);
    if (!policy.allowedOrigins.has("*") && !policy.allowedOrigins.has(origin)) {
        throw httpClientError(
            "HttpClientStrictNetworkError",
            "SLOPPY_E_HTTP_CLIENT_STRICT_NETWORK_DENIED",
            "HTTP client strict network policy denied the outbound origin.",
        );
    }
}

function isHttpRedirectStatus(status) {
    return status === 301 || status === 302 || status === 303 || status === 307 || status === 308;
}

function httpConnectionHeaderHas(value, token) {
    return value.split(",").some((part) => part.trim().toLowerCase() === token);
}

function isHttpBodyForbiddenStatus(status) {
    return (status >= 100 && status <= 199) || status === 204 || status === 304;
}

function resolveHttpRedirectUrl(currentUrl, location, operation) {
    if (typeof location !== "string" || location.length === 0 || hasHttpControlChars(location)) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            "HTTP redirect Location must be a non-empty URL without control characters.",
        );
    }
    if (location.includes("://")) {
        return parseAbsoluteHttpUrl(location, operation);
    }
    if (location.startsWith("//")) {
        return parseAbsoluteHttpUrl(`${currentUrl.scheme}:${location}`, operation);
    }
    return Object.freeze({
        ...currentUrl,
        target: joinHttpTarget(currentUrl.target, location, operation),
    });
}

class HttpConnectionPool {
    constructor(options) {
        this._options = options;
        this._entries = new Map();
        this._http2Entries = new Map();
        this._stats = {
            connectionsCreated: 0,
            connectionsReused: 0,
            connectionsClosedIdle: 0,
            connectionsClosed: 0,
            poolWaitCount: 0,
            poolRejectedCount: 0,
        };
        this._closed = false;
    }

    _entry(originKey) {
        let entry = this._entries.get(originKey);
        if (entry === undefined) {
            entry = { idle: [], total: 0, inUse: 0, queue: [] };
            this._entries.set(originKey, entry);
        }
        return entry;
    }

    _prune(originKey, entry) {
        if (entry.total === 0 && entry.inUse === 0 && entry.idle.length === 0 && entry.queue.length === 0) {
            this._entries.delete(originKey);
        }
    }

    _isExpired(record) {
        const lifetimeMs = this._options.connectionLifetimeMs;
        return lifetimeMs !== undefined && Date.now() - record.createdAt >= lifetimeMs;
    }

    async _closeIdleRecord(originKey, entry, record, idleClose) {
        const index = entry.idle.indexOf(record);
        if (index >= 0) {
            entry.idle.splice(index, 1);
        }
        if (record.timer !== undefined) {
            clearTimeout(record.timer);
        }
        entry.total -= 1;
        if (idleClose) {
            this._stats.connectionsClosedIdle += 1;
        }
        this._stats.connectionsClosed += 1;
        await record.connection.close().catch(() => {});
        this._prune(originKey, entry);
    }

    async _openQueued(originKey, entry, waiter) {
        waiter.cleanup();
        if (this._closed) {
            waiter.reject(httpClientError(
                "HttpClientPoolClosedError",
                "SLOPPY_E_HTTP_CLIENT_POOL_CLOSED",
                "HTTP client connection pool was closed.",
            ));
            return;
        }
        entry.total += 1;
        entry.inUse += 1;
        try {
            const connection = await waiter.connect();
            this._stats.connectionsCreated += 1;
            waiter.resolve({ connection, reused: false, createdAt: Date.now() });
        } catch (error) {
            entry.total -= 1;
            entry.inUse -= 1;
            waiter.reject(error);
            this._prune(originKey, entry);
        }
    }

    _settleNextQueued(originKey, entry, reusableRecord = undefined) {
        const waiter = entry.queue.shift();
        if (waiter === undefined) {
            return false;
        }
        waiter.cleanup();
        if (this._closed) {
            waiter.reject(httpClientError(
                "HttpClientPoolClosedError",
                "SLOPPY_E_HTTP_CLIENT_POOL_CLOSED",
                "HTTP client connection pool was closed.",
            ));
            return false;
        }
        if (reusableRecord !== undefined && !this._isExpired(reusableRecord)) {
            this._stats.connectionsReused += 1;
            entry.inUse += 1;
            waiter.resolve({
                connection: reusableRecord.connection,
                reused: true,
                createdAt: reusableRecord.createdAt,
            });
            return true;
        }
        this._openQueued(originKey, entry, waiter);
        return false;
    }

    _queueTimeoutMs(options = {}) {
        const pendingTimeoutMs = this._options.pendingQueueTimeoutMs;
        const remainingMs = options.expiresAtMs === undefined ? Infinity : httpRemainingMs(options.expiresAtMs);
        return Math.max(0, Math.min(pendingTimeoutMs, remainingMs));
    }

    _closedError() {
        return httpClientError(
            "HttpClientPoolClosedError",
            "SLOPPY_E_HTTP_CLIENT_POOL_CLOSED",
            "HTTP client connection pool was closed.",
        );
    }

    async acquire(originKey, connect, options = {}) {
        if (this._closed) {
            throw this._closedError();
        }
        const entry = this._entry(originKey);
        while (entry.idle.length > 0) {
            const idle = entry.idle.pop();
            clearTimeout(idle.timer);
            if (this._isExpired(idle)) {
                entry.total -= 1;
                this._stats.connectionsClosed += 1;
                await idle.connection.close().catch(() => {});
                this._prune(originKey, entry);
                continue;
            }
            this._stats.connectionsReused += 1;
            entry.inUse += 1;
            return { connection: idle.connection, reused: true, createdAt: idle.createdAt };
        }
        if (entry.total >= this._options.maxConnectionsPerOrigin) {
            if (entry.queue.length < this._options.pendingQueueLimit) {
                this._stats.poolWaitCount += 1;
                return await new Promise((resolve, reject) => {
                    const timeoutMs = this._queueTimeoutMs(options);
                    let cleanupSignal = () => {};
                    const waiter = {
                        connect,
                        resolve,
                        reject,
                        timer: undefined,
                        cleanup() {
                            if (waiter.timer !== undefined) {
                                clearTimeout(waiter.timer);
                                waiter.timer = undefined;
                            }
                            cleanupSignal();
                            cleanupSignal = () => {};
                        },
                    };
                    const rejectQueued = (error) => {
                        const index = entry.queue.indexOf(waiter);
                        if (index >= 0) {
                            entry.queue.splice(index, 1);
                        }
                        waiter.cleanup();
                        reject(error);
                    };
                    waiter.timer = setTimeout(() => {
                        this._stats.poolRejectedCount += 1;
                        rejectQueued(httpClientError(
                            "HttpClientPoolExhaustedError",
                            "SLOPPY_E_HTTP_CLIENT_POOL_EXHAUSTED",
                            "HTTP client connection pool pending queue timed out for origin.",
                        ));
                    }, timeoutMs);
                    cleanupSignal = subscribeHttpCancellation(options.signal, () => {
                        this._stats.poolRejectedCount += 1;
                        rejectQueued(httpClientError(
                            "HttpClientCancelledError",
                            "SLOPPY_E_HTTP_CLIENT_REQUEST_CANCELLED",
                            "HTTP client request was cancelled.",
                        ));
                    });
                    entry.queue.push(waiter);
                });
            }
            this._stats.poolRejectedCount += 1;
            throw httpClientError(
                "HttpClientPoolExhaustedError",
                "SLOPPY_E_HTTP_CLIENT_POOL_EXHAUSTED",
                "HTTP client connection pool exhausted for origin.",
            );
        }
        entry.total += 1;
        entry.inUse += 1;
        try {
            if (this._closed) {
                throw this._closedError();
            }
            const connection = await connect();
            if (this._closed) {
                await connection.close().catch(() => {});
                throw this._closedError();
            }
            this._stats.connectionsCreated += 1;
            return { connection, reused: false, createdAt: Date.now() };
        } catch (error) {
            entry.total -= 1;
            entry.inUse -= 1;
            this._prune(originKey, entry);
            throw error;
        }
    }

    async release(originKey, connection, reusable, createdAt = Date.now()) {
        const entry = this._entries.get(originKey);
        if (entry === undefined) {
            await connection.close().catch(() => {});
            return;
        }
        entry.inUse -= 1;
        const record = { connection, timer: undefined, createdAt };
        if (this._closed) {
            entry.total -= 1;
            this._stats.connectionsClosed += 1;
            await connection.close().catch(() => {});
            this._prune(originKey, entry);
            return;
        }
        if (reusable && !this._isExpired(record)) {
            if (this._settleNextQueued(originKey, entry, record)) {
                return;
            }
        }
        if (reusable && !this._isExpired(record) && this._options.idleTimeoutMs > 0) {
            const timer = setTimeout(() => {
                const index = entry.idle.findIndex((idle) => idle.connection === connection);
                if (index >= 0) {
                    entry.idle.splice(index, 1);
                    entry.total -= 1;
                    this._stats.connectionsClosedIdle += 1;
                    this._stats.connectionsClosed += 1;
                    connection.close().catch(() => {});
                    this._prune(originKey, entry);
                }
            }, this._options.idleTimeoutMs);
            record.timer = timer;
            entry.idle.push(record);
            return;
        }
        entry.total -= 1;
        this._stats.connectionsClosed += 1;
        await connection.close().catch(() => {});
        this._settleNextQueued(originKey, entry);
        this._prune(originKey, entry);
    }

    _http2Entry(originKey) {
        let entry = this._http2Entries.get(originKey);
        if (entry === undefined) {
            entry = { sessions: [], total: 0, pending: undefined };
            this._http2Entries.set(originKey, entry);
        }
        return entry;
    }

    _pruneHttp2(originKey, entry) {
        entry.sessions = entry.sessions.filter((record) => !record.session.closed);
        if (entry.total === 0 && entry.sessions.length === 0 && entry.pending === undefined) {
            this._http2Entries.delete(originKey);
        }
    }

    _dropHttp2Record(originKey, entry, record) {
        if (record.timer !== undefined) {
            clearTimeout(record.timer);
            record.timer = undefined;
        }
        const index = entry.sessions.indexOf(record);
        if (index >= 0) {
            entry.sessions.splice(index, 1);
            entry.total = Math.max(0, entry.total - 1);
        }
        this._pruneHttp2(originKey, entry);
    }

    _findReusableHttp2Session(entry) {
        for (const record of entry.sessions) {
            if (!record.session.closed && record.session.acceptsStreams) {
                if (record.timer !== undefined) {
                    clearTimeout(record.timer);
                    record.timer = undefined;
                }
                return record.session;
            }
        }
        return undefined;
    }

    async acquireHttp2(originKey, connect) {
        if (this._closed) {
            throw this._closedError();
        }
        const entry = this._http2Entry(originKey);
        const reusable = this._findReusableHttp2Session(entry);
        if (reusable !== undefined) {
            this._stats.connectionsReused += 1;
            return { session: reusable, reused: true };
        }
        if (entry.pending !== undefined) {
            this._stats.poolWaitCount += 1;
            const session = await entry.pending;
            if (!session.closed && session.acceptsStreams) {
                this._stats.connectionsReused += 1;
                return { session, reused: true };
            }
        }
        if (entry.total >= this._options.maxConnectionsPerOrigin) {
            this._stats.poolRejectedCount += 1;
            throw httpClientError(
                "HttpClientPoolExhaustedError",
                "SLOPPY_E_HTTP_CLIENT_POOL_EXHAUSTED",
                "HTTP client connection pool exhausted for origin.",
            );
        }
        entry.total += 1;
        entry.pending = connect().then((session) => {
            const record = { session, timer: undefined };
            entry.sessions.push(record);
            session.onClose(() => this._dropHttp2Record(originKey, entry, record));
            this._stats.connectionsCreated += 1;
            return session;
        });
        try {
            return { session: await entry.pending, reused: false };
        } catch (error) {
            entry.total = Math.max(0, entry.total - 1);
            this._pruneHttp2(originKey, entry);
            throw error;
        } finally {
            entry.pending = undefined;
            this._pruneHttp2(originKey, entry);
        }
    }

    peekHttp2(originKey) {
        const entry = this._http2Entries.get(originKey);
        if (entry === undefined) {
            return undefined;
        }
        return this._findReusableHttp2Session(entry);
    }

    adoptHttp2(originKey, session) {
        if (this._closed) {
            session.close().catch(() => {});
            throw this._closedError();
        }
        const entry = this._http2Entry(originKey);
        if (entry.total >= this._options.maxConnectionsPerOrigin) {
            this._stats.poolRejectedCount += 1;
            session.close().catch(() => {});
            throw httpClientError(
                "HttpClientPoolExhaustedError",
                "SLOPPY_E_HTTP_CLIENT_POOL_EXHAUSTED",
                "HTTP client connection pool exhausted for origin.",
            );
        }
        const record = { session, timer: undefined };
        entry.total += 1;
        entry.sessions.push(record);
        session.onClose(() => this._dropHttp2Record(originKey, entry, record));
        return session;
    }

    releaseHttp2(originKey, session) {
        const entry = this._http2Entries.get(originKey);
        if (entry === undefined) {
            session.close().catch(() => {});
            return;
        }
        if (this._closed) {
            session.close().catch(() => {});
            return;
        }
        const record = entry.sessions.find((candidate) => candidate.session === session);
        if (record === undefined) {
            session.close().catch(() => {});
            return;
        }
        if (session.closed) {
            this._dropHttp2Record(originKey, entry, record);
            return;
        }
        if (session.activeStreamCount > 0) {
            return;
        }
        if (this._options.idleTimeoutMs === 0) {
            this._dropHttp2Record(originKey, entry, record);
            this._stats.connectionsClosed += 1;
            session.close().catch(() => {});
            return;
        }
        if (record.timer !== undefined) {
            clearTimeout(record.timer);
        }
        record.timer = setTimeout(() => {
            this._dropHttp2Record(originKey, entry, record);
            this._stats.connectionsClosedIdle += 1;
            this._stats.connectionsClosed += 1;
            session.close().catch(() => {});
        }, this._options.idleTimeoutMs);
    }

    stats() {
        let activeRequests = 0;
        let idleConnections = 0;
        let queuedRequests = 0;
        for (const entry of this._entries.values()) {
            activeRequests += entry.inUse;
            idleConnections += entry.idle.length;
            queuedRequests += entry.queue.length;
        }
        for (const entry of this._http2Entries.values()) {
            for (const record of entry.sessions) {
                activeRequests += record.session.activeStreamCount;
                if (!record.session.closed && record.session.activeStreamCount === 0) {
                    idleConnections += 1;
                }
            }
        }
        return Object.freeze({
            connectionsCreated: this._stats.connectionsCreated,
            connectionsReused: this._stats.connectionsReused,
            connectionsClosedIdle: this._stats.connectionsClosedIdle,
            connectionsClosed: this._stats.connectionsClosed,
            poolWaitCount: this._stats.poolWaitCount,
            poolRejectedCount: this._stats.poolRejectedCount,
            activeRequests,
            idleConnections,
            queuedRequests,
        });
    }

    async close() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        const pending = [];
        for (const entry of this._entries.values()) {
            for (const idle of entry.idle.splice(0)) {
                clearTimeout(idle.timer);
                this._stats.connectionsClosed += 1;
                pending.push(idle.connection.close().catch(() => {}));
            }
            for (const waiter of entry.queue.splice(0)) {
                waiter.cleanup();
                waiter.reject(this._closedError());
            }
            entry.total = entry.inUse;
        }
        for (const entry of this._http2Entries.values()) {
            for (const record of entry.sessions.splice(0)) {
                if (record.timer !== undefined) {
                    clearTimeout(record.timer);
                }
                this._stats.connectionsClosed += 1;
                pending.push(record.session.close().catch(() => {}));
            }
            entry.total = 0;
        }
        await Promise.all(pending);
    }
}

function isHttpCancellationSignal(value) {
    return (
        value !== undefined &&
        value !== null &&
        typeof value === "object" &&
        typeof value.aborted === "boolean"
    );
}

function subscribeHttpCancellation(signal, listener) {
    if (!isHttpCancellationSignal(signal)) {
        return () => {};
    }
    if (signal.aborted) {
        listener(signal.reason);
        return () => {};
    }
    if (typeof signal._subscribe === "function") {
        return signal._subscribe(listener);
    }
    if (typeof signal.addEventListener === "function") {
        const wrapped = () => listener(signal.reason);
        signal.addEventListener("abort", wrapped);
        return () => signal.removeEventListener?.("abort", wrapped);
    }
    return () => {};
}

function httpDeadlineRemainingMs(deadline, operation) {
    if (deadline === undefined) {
        return Infinity;
    }
    if (typeof deadline !== "object" || deadline === null || typeof deadline.remainingMs !== "function") {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} deadline must come from sloppy/time Deadline.`,
        );
    }
    const remainingMs = deadline.remainingMs();
    if (!Number.isFinite(remainingMs) && remainingMs !== Infinity) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} deadline remaining time must be finite or Infinity.`,
        );
    }
    return Math.max(0, remainingMs);
}

function httpRequestTimeoutError() {
    return httpClientError(
        "HttpClientTimeoutError",
        "SLOPPY_E_HTTP_CLIENT_REQUEST_TIMEOUT",
        "HTTP client request timed out.",
    );
}

function httpRequestCancelledError() {
    return httpClientError(
        "HttpClientCancelledError",
        "SLOPPY_E_HTTP_CLIENT_REQUEST_CANCELLED",
        "HTTP client request was cancelled.",
    );
}

function httpRemainingMs(expiresAtMs) {
    return expiresAtMs === Infinity ? Infinity : Math.max(0, expiresAtMs - Date.now());
}

function parseHttpPort(text, operation) {
    if (text === undefined || text === "") {
        return undefined;
    }
    if (!/^[0-9]+$/.test(text)) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} URL port must contain decimal digits.`,
        );
    }
    const port = Number(text);
    if (!Number.isInteger(port) || port < 1 || port > 65535) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} URL port must be from 1 to 65535.`,
        );
    }
    return port;
}

function parseAbsoluteHttpUrl(url, operation) {
    if (typeof url !== "string" || url.length === 0) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} URL must be a non-empty string.`,
        );
    }
    const schemeEnd = url.indexOf("://");
    if (schemeEnd <= 0) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} requires an absolute http:// or https:// URL or a baseUrl-relative path.`,
        );
    }
    const scheme = url.slice(0, schemeEnd).toLowerCase();
    if (scheme !== "http" && scheme !== "https") {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} supports http:// and https:// URLs only.`,
        );
    }

    const rest = url.slice(schemeEnd + 3);
    let pathStart = rest.length;
    for (const marker of ["/", "?", "#"]) {
        const index = rest.indexOf(marker);
        if (index >= 0 && index < pathStart) {
            pathStart = index;
        }
    }
    const authority = rest.slice(0, pathStart);
    let target = rest.slice(pathStart);
    if (authority.length === 0 || authority.includes("@")) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} URL authority is invalid.`,
        );
    }

    const fragment = target.indexOf("#");
    if (fragment >= 0) {
        target = target.slice(0, fragment);
    }
    if (target.length === 0) {
        target = "/";
    } else if (target.startsWith("?")) {
        target = `/${target}`;
    } else if (!target.startsWith("/")) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} URL target must be an origin-form path.`,
        );
    }

    let host;
    let portText;
    let hostHeader;
    if (authority.startsWith("[")) {
        const close = authority.indexOf("]");
        if (close <= 1 || (authority.length > close + 1 && authority[close + 1] !== ":")) {
            throw httpClientError(
                "HttpClientInvalidUrlError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
                `${operation} IPv6 URL host must be bracketed as [host]:port.`,
            );
        }
        host = authority.slice(1, close);
        portText = authority.length > close + 1 ? authority.slice(close + 2) : undefined;
        hostHeader = `[${host}]`;
    } else {
        const firstColon = authority.indexOf(":");
        const lastColon = authority.lastIndexOf(":");
        if (firstColon !== lastColon) {
            throw httpClientError(
                "HttpClientInvalidUrlError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
                `${operation} unbracketed IPv6 URL hosts are ambiguous.`,
            );
        }
        host = firstColon < 0 ? authority : authority.slice(0, firstColon);
        portText = firstColon < 0 ? undefined : authority.slice(firstColon + 1);
        hostHeader = host;
    }
    if (host.length === 0 || hasHttpControlChars(host)) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} URL contains invalid control characters.`,
        );
    }

    const defaultPort = scheme === "https" ? 443 : 80;
    const port = parseHttpPort(portText, operation) ?? defaultPort;
    const headerPort = port === defaultPort ? "" : `:${port}`;
    const resolvedHostHeader = `${hostHeader}${headerPort}`;
    if (hasHttpControlChars(resolvedHostHeader) || hasHttpControlChars(target)) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} URL contains invalid control characters.`,
        );
    }
    return Object.freeze({
        scheme,
        host,
        port,
        hostHeader: resolvedHostHeader,
        target,
    });
}

function splitHttpTarget(target) {
    const queryStart = target.indexOf("?");
    if (queryStart < 0) {
        return { path: target, suffix: "" };
    }
    return { path: target.slice(0, queryStart), suffix: target.slice(queryStart) };
}

function normalizeHttpTargetPath(path) {
    const segments = [];
    for (const segment of path.split("/")) {
        if (segment === "" || segment === ".") {
            continue;
        }
        if (segment === "..") {
            segments.pop();
            continue;
        }
        segments.push(segment);
    }
    return `/${segments.join("/")}`;
}

function joinHttpTarget(baseTarget, requestUrl, operation) {
    if (hasHttpControlChars(requestUrl)) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} URL contains invalid control characters.`,
        );
    }
    if (requestUrl.length === 0) {
        const { path, suffix } = splitHttpTarget(baseTarget);
        return `${normalizeHttpTargetPath(path)}${suffix}`;
    }
    if (requestUrl.startsWith("?")) {
        const { path } = splitHttpTarget(baseTarget);
        return `${normalizeHttpTargetPath(path)}${requestUrl}`;
    }
    if (requestUrl.startsWith("/")) {
        const { path, suffix } = splitHttpTarget(requestUrl);
        return `${normalizeHttpTargetPath(path)}${suffix}`;
    }

    const { path: basePath } = splitHttpTarget(baseTarget);
    const baseDirectory = basePath.endsWith("/") ? basePath : basePath.slice(0, basePath.lastIndexOf("/") + 1);
    const { path, suffix } = splitHttpTarget(`${baseDirectory}${requestUrl}`);
    return `${normalizeHttpTargetPath(path)}${suffix}`;
}

function resolveHttpUrl(baseOptions, requestUrl, operation) {
    if (typeof requestUrl === "string" && hasHttpControlChars(requestUrl)) {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} URL contains invalid control characters.`,
        );
    }
    if (typeof requestUrl === "string" && requestUrl.includes("://")) {
        return parseAbsoluteHttpUrl(requestUrl, operation);
    }
    const baseUrl = baseOptions?.baseUrl;
    if (typeof requestUrl !== "string" || typeof baseUrl !== "string") {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} requires an absolute URL or a path with client baseUrl.`,
        );
    }
    const base = parseAbsoluteHttpUrl(baseUrl, operation);
    return Object.freeze({ ...base, target: joinHttpTarget(base.target, requestUrl, operation) });
}

function normalizeHttpMethod(method, operation) {
    const resolved = method === undefined ? "GET" : method;
    if (typeof resolved !== "string" || !/^[A-Za-z]+$/.test(resolved)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} method must be a token string.`,
        );
    }
    return resolved.toUpperCase();
}

function appendHttpHeaders(target, headers, operation) {
    if (headers === undefined) {
        return;
    }
    if (!isPlainObject(headers)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} headers must be a plain object.`,
        );
    }
    for (const [name, value] of Object.entries(headers)) {
        const normalizedName = String(name).toLowerCase();
        if (!/^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/.test(name)) {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} header name is invalid.`,
            );
        }
        if (normalizedName === "host" || normalizedName === "connection" || normalizedName === "content-length") {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} manages Host, Connection, and Content-Length headers.`,
            );
        }
        if (typeof value !== "string" || hasHttpControlChars(value)) {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} header value must be a string without control characters.`,
            );
        }
        target.set(normalizedName, { name, value });
    }
}

function enforceHttpRequestBodyLimit(bytes, maxRequestBytes) {
    if (bytes.byteLength > maxRequestBytes) {
        throw httpClientError(
            "HttpClientRequestLimitError",
            "SLOPPY_E_HTTP_CLIENT_REQUEST_BODY_LIMIT",
            "HTTP request body exceeded the configured limit.",
        );
    }
    return bytes;
}

function isHttpAsyncIterable(value) {
    return value !== undefined && value !== null && typeof value[Symbol.asyncIterator] === "function";
}

async function readHttpStreamChunk(iterator, signal, expiresAtMs) {
    if (isHttpCancellationSignal(signal) && signal.aborted) {
        throw httpRequestCancelledError();
    }
    const remainingMs = httpRemainingMs(expiresAtMs);
    if (remainingMs <= 0) {
        throw httpRequestTimeoutError();
    }
    if (remainingMs === Infinity && signal === undefined) {
        return await iterator.next();
    }
    let timeoutId;
    let cleanupCancellation = () => {};
    try {
        return await Promise.race([
            iterator.next(),
            new Promise((_, reject) => {
                cleanupCancellation = subscribeHttpCancellation(signal, () => reject(httpRequestCancelledError()));
                if (remainingMs !== Infinity) {
                    timeoutId = setTimeout(() => reject(httpRequestTimeoutError()), remainingMs);
                }
            }),
        ]);
    } finally {
        cleanupCancellation();
        if (timeoutId !== undefined) {
            clearTimeout(timeoutId);
        }
    }
}

function isHttpRequestStreamTimingError(error) {
    return error?.code === "SLOPPY_E_HTTP_CLIENT_REQUEST_TIMEOUT" ||
        error?.code === "SLOPPY_E_HTTP_CLIENT_REQUEST_CANCELLED";
}

async function closeHttpRequestStreamIterator(iterator, shouldAwaitCleanup) {
    if (typeof iterator.return !== "function") {
        return;
    }
    try {
        const cleanup = Promise.resolve(iterator.return());
        if (shouldAwaitCleanup) {
            await cleanup;
            return;
        }

        // Timed-out or cancelled async generators can remain suspended in next(); observe
        // cleanup failures without letting a non-interruptible iterator block settlement.
        cleanup.catch(() => {});
        await Promise.race([cleanup, Promise.resolve()]);
    } catch {
    }
}

async function consumeHttpRequestStream(stream, maxRequestBytes, headers, operation, timing) {
    if (!isHttpAsyncIterable(stream)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} stream body must be an async iterable of Uint8Array chunks.`,
        );
    }
    const chunks = [];
    let totalLength = 0;
    const iterator = stream[Symbol.asyncIterator]();
    let completed = false;
    let terminalError;
    try {
        while (true) {
            const result = await readHttpStreamChunk(iterator, timing.signal, timing.expiresAtMs);
            if (result.done) {
                completed = true;
                break;
            }
            const chunk = result.value;
            if (!(chunk instanceof Uint8Array)) {
                throw httpClientError(
                    "HttpClientInvalidOptionsError",
                    "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                    `${operation} stream body chunks must be Uint8Array values.`,
                );
            }
            totalLength += chunk.byteLength;
            if (totalLength > maxRequestBytes) {
                throw httpClientError(
                    "HttpClientRequestLimitError",
                    "SLOPPY_E_HTTP_CLIENT_REQUEST_BODY_LIMIT",
                    "HTTP request body exceeded the configured limit.",
                );
            }
            chunks.push(chunk.slice());
        }
    } catch (error) {
        terminalError = error;
        throw error;
    } finally {
        if (!completed) {
            await closeHttpRequestStreamIterator(
                iterator,
                !isHttpRequestStreamTimingError(terminalError),
            );
        }
    }
    setHttpDefaultHeader(headers, "Content-Type", "application/octet-stream");
    return concatHttpBytes(chunks, totalLength);
}

async function normalizeHttpBody(options, headers, operation, maxRequestBytes, timing) {
    const sources = ["json", "text", "bytes", "stream"].filter((key) => options?.[key] !== undefined);
    if (sources.length > 1) {
        throw httpClientError(
            "HttpClientAmbiguousBodyError",
            "SLOPPY_E_HTTP_CLIENT_AMBIGUOUS_BODY",
            `${operation} accepts only one request body source.`,
        );
    }
    if (sources.length === 0) {
        return new Uint8Array(0);
    }
    if (sources[0] === "json") {
        let text;
        try {
            text = JSON.stringify(options.json);
        } catch (error) {
            throw httpClientError(
                "HttpClientJsonError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_JSON",
                `${operation} JSON body could not be serialized.`,
                { cause: error },
            );
        }
        if (text === undefined) {
            throw httpClientError(
                "HttpClientJsonError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_JSON",
                `${operation} JSON body must serialize to a JSON value.`,
            );
        }
        setHttpDefaultHeader(headers, "Content-Type", "application/json");
        return enforceHttpRequestBodyLimit(Text.utf8.encode(text), maxRequestBytes);
    }
    if (sources[0] === "text") {
        if (typeof options.text !== "string") {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `${operation} text body must be a string.`,
            );
        }
        setHttpDefaultHeader(headers, "Content-Type", "text/plain; charset=utf-8");
        return enforceHttpRequestBodyLimit(Text.utf8.encode(options.text), maxRequestBytes);
    }
    if (sources[0] === "stream") {
        return await consumeHttpRequestStream(options.stream, maxRequestBytes, headers, operation, timing);
    }
    if (!(options.bytes instanceof Uint8Array)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} bytes body must be a Uint8Array.`,
        );
    }
    return enforceHttpRequestBodyLimit(options.bytes.slice(), maxRequestBytes);
}

function concatHttpBytes(chunks, totalLength) {
    const out = new Uint8Array(totalLength);
    let offset = 0;
    for (const chunk of chunks) {
        out.set(chunk, offset);
        offset += chunk.byteLength;
    }
    return out;
}

function httpBytesToAscii(bytes) {
    let text = "";
    for (const byte of bytes) {
        text += String.fromCharCode(byte);
    }
    return text;
}

function findHttpHeaderEnd(bytes) {
    for (let index = 3; index < bytes.byteLength; index += 1) {
        if (bytes[index - 3] === 13 && bytes[index - 2] === 10 && bytes[index - 1] === 13 && bytes[index] === 10) {
            return index + 1;
        }
    }
    return -1;
}

function findHttpCrlf(bytes, start) {
    for (let index = start + 1; index < bytes.byteLength; index += 1) {
        if (bytes[index - 1] === 13 && bytes[index] === 10) {
            return index - 1;
        }
    }
    return -1;
}

function incompleteHttpChunk(complete, message) {
    if (!complete) {
        return undefined;
    }
    throw httpClientError(
        "HttpClientMalformedResponseError",
        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
        message,
    );
}

function parseHttpChunkedBody(bodyBytes, maxResponseBytes, complete) {
    const chunks = [];
    let totalLength = 0;
    let offset = 0;

    while (true) {
        const sizeLineEnd = findHttpCrlf(bodyBytes, offset);
        if (sizeLineEnd < 0) {
            return incompleteHttpChunk(complete, "HTTP chunked response ended before a chunk size.");
        }
        const sizeLine = httpBytesToAscii(bodyBytes.slice(offset, sizeLineEnd));
        const sizeText = sizeLine.split(";", 1)[0].trim();
        if (!/^[0-9A-Fa-f]+$/.test(sizeText)) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP chunked response chunk size is malformed.",
            );
        }
        const size = Number.parseInt(sizeText, 16);
        if (!Number.isSafeInteger(size)) {
            throw httpClientError(
                "HttpClientResponseLimitError",
                "SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT",
                "HTTP response body exceeded the configured limit.",
            );
        }
        offset = sizeLineEnd + 2;
        if (size === 0) {
            if (offset + 2 > bodyBytes.byteLength) {
                return incompleteHttpChunk(complete, "HTTP chunked response ended before trailers.");
            }
            if (bodyBytes[offset] === 13 && bodyBytes[offset + 1] === 10) {
                return concatHttpBytes(chunks, totalLength);
            }
            const trailerEnd = findHttpHeaderEnd(bodyBytes.slice(offset));
            if (trailerEnd < 0) {
                return incompleteHttpChunk(complete, "HTTP chunked response trailers are incomplete.");
            }
            return concatHttpBytes(chunks, totalLength);
        }
        if (totalLength + size > maxResponseBytes) {
            throw httpClientError(
                "HttpClientResponseLimitError",
                "SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT",
                "HTTP response body exceeded the configured limit.",
            );
        }
        if (offset + size + 2 > bodyBytes.byteLength) {
            return incompleteHttpChunk(complete, "HTTP chunked response ended before a complete chunk.");
        }
        if (bodyBytes[offset + size] !== 13 || bodyBytes[offset + size + 1] !== 10) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP chunked response chunk terminator is malformed.",
            );
        }
        chunks.push(bodyBytes.slice(offset, offset + size));
        totalLength += size;
        offset += size + 2;
    }
}

class HttpHeaderBag {
    constructor(entries) {
        this._entries = entries;
        this._map = new Map(entries.map(([name, value]) => [name.toLowerCase(), value]));
        Object.freeze(this);
    }

    get(name) {
        if (typeof name !== "string") {
            throw new TypeError("HttpResponse.headers.get name must be a string.");
        }
        return this._map.get(name.toLowerCase()) ?? null;
    }

    entries() {
        return this._entries.map(([name, value]) => [name, value]);
    }
}

class HttpClientResponse {
    constructor(status, statusText, headers, body, connectionReusable = false) {
        this.status = status;
        this.statusText = statusText;
        this.headers = headers;
        this._body = body;
        this._consumed = false;
        this._connectionReusable = connectionReusable;
    }

    _consume() {
        if (this._consumed) {
            throw httpClientError(
                "HttpClientBodyConsumedError",
                "SLOPPY_E_HTTP_CLIENT_BODY_CONSUMED",
                "HTTP response body was already consumed.",
            );
        }
        this._consumed = true;
        return this._body;
    }

    async bytes() {
        return this._consume().slice();
    }

    async text() {
        return Text.utf8.decode(this._consume());
    }

    async json() {
        try {
            return JSON.parse(Text.utf8.decode(this._consume()));
        } catch (error) {
            throw httpClientError(
                "HttpClientJsonError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_JSON",
                "HTTP response body is not valid JSON.",
                { cause: error },
            );
        }
    }

    stream(options = undefined) {
        const chunkSize = options?.chunkSize ?? 8192;
        if (!Number.isInteger(chunkSize) || chunkSize <= 0) {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                "HTTP response stream chunkSize must be a positive integer.",
            );
        }
        const body = this._consume();
        return (async function* streamHttpResponseBody() {
            for (let offset = 0; offset < body.byteLength; offset += chunkSize) {
                yield body.slice(offset, Math.min(body.byteLength, offset + chunkSize));
            }
        })();
    }
}

function parseHttpResponse(headBytes, bodyBytes, maxResponseBytes, complete = false, requestMethod = "GET") {
    const head = httpBytesToAscii(headBytes);
    const lines = head.split("\r\n");
    const statusLine = lines.shift();
    const match = /^HTTP\/1\.([01]) ([0-9]{3})(?: (.*))?$/.exec(statusLine ?? "");
    const headers = [];
    let contentLength = undefined;
    let transferEncoding = undefined;
    let connectionHeader = "";

    if (match === null) {
        throw httpClientError(
            "HttpClientMalformedResponseError",
            "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
            "HTTP response status line is malformed.",
        );
    }
    const status = Number(match[2]);
    for (const line of lines) {
        if (line.length === 0) {
            continue;
        }
        const colon = line.indexOf(":");
        if (colon <= 0) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP response header is malformed.",
            );
        }
        const name = line.slice(0, colon);
        const value = line.slice(colon + 1).trimStart();
        headers.push([name, value]);
        if (name.toLowerCase() === "content-length") {
            if (!/^[0-9]+$/.test(value)) {
                throw httpClientError(
                    "HttpClientMalformedResponseError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP response Content-Length is malformed.",
                );
            }
            contentLength = Number(value);
        }
        if (name.toLowerCase() === "transfer-encoding") {
            transferEncoding = value.toLowerCase();
        }
        if (name.toLowerCase() === "connection") {
            connectionHeader = value.toLowerCase();
        }
    }
    const httpMinorVersion = match === null ? 1 : Number(match[1]);
    let connectionReusable =
        complete === false &&
        (httpMinorVersion === 1
            ? !httpConnectionHeaderHas(connectionHeader, "close")
            : httpConnectionHeaderHas(connectionHeader, "keep-alive"));
    const methodForbidsBody = requestMethod === "HEAD";
    const statusForbidsBody = isHttpBodyForbiddenStatus(status);
    const bodyForbidden = methodForbidsBody || statusForbidsBody;
    if (bodyForbidden) {
        if (statusForbidsBody && contentLength !== undefined && contentLength > 0) {
            connectionReusable = false;
        }
        if (statusForbidsBody && transferEncoding !== undefined) {
            connectionReusable = false;
        }
        if (bodyBytes.byteLength > 0) {
            connectionReusable = false;
        }
        bodyBytes = bodyBytes.slice(0, 0);
    } else if (transferEncoding === "chunked") {
        const decoded = parseHttpChunkedBody(bodyBytes, maxResponseBytes, complete);
        if (decoded === undefined) {
            return undefined;
        }
        bodyBytes = decoded;
    } else if (transferEncoding !== undefined && transferEncoding !== "identity") {
        throw httpClientError(
            "HttpClientMalformedResponseError",
            "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
            "HTTP response Transfer-Encoding is not supported by this HTTP client.",
        );
    } else if (contentLength !== undefined) {
        if (contentLength > maxResponseBytes) {
            throw httpClientError(
                "HttpClientResponseLimitError",
                "SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT",
                "HTTP response body exceeded the configured limit.",
            );
        }
        if (bodyBytes.byteLength < contentLength) {
            if (complete) {
                throw httpClientError(
                    "HttpClientMalformedResponseError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP response ended before the declared Content-Length body was fully received.",
                );
            }
            return undefined;
        }
        bodyBytes = bodyBytes.slice(0, contentLength);
    } else {
        if (bodyBytes.byteLength > maxResponseBytes) {
            throw httpClientError(
                "HttpClientResponseLimitError",
                "SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT",
                "HTTP response body exceeded the configured limit.",
            );
        }
        if (!complete) {
            return undefined;
        }
        connectionReusable = false;
    }
    return new HttpClientResponse(
        status,
        match[3] ?? "",
        new HttpHeaderBag(headers),
        bodyBytes,
        connectionReusable,
    );
}

async function readHttpResponse(connection, limits) {
    const chunks = [];
    let totalLength = 0;
    let headerEnd = -1;
    let parsed = undefined;

    while (parsed === undefined) {
        let chunk;
        try {
            chunk = await connection.read({ maxBytes: 8192 });
        } catch (error) {
            if (headerEnd >= 0) {
                break;
            }
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP response ended before a complete head was received.",
                { cause: error },
            );
        }
        chunks.push(chunk);
        totalLength += chunk.byteLength;
        if (totalLength > limits.maxHeaderBytes + limits.maxResponseBytes) {
            throw httpClientError(
                "HttpClientResponseLimitError",
                "SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT",
                "HTTP response exceeded the configured limit.",
            );
        }
        const received = concatHttpBytes(chunks, totalLength);
        headerEnd = findHttpHeaderEnd(received);
        if (headerEnd < 0) {
            if (totalLength > limits.maxHeaderBytes) {
                throw httpClientError(
                    "HttpClientHeaderLimitError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP response headers exceeded the configured limit.",
                );
            }
            continue;
        }
        if (headerEnd > limits.maxHeaderBytes) {
            throw httpClientError(
                "HttpClientHeaderLimitError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP response headers exceeded the configured limit.",
            );
        }
        parsed = parseHttpResponse(
            received.slice(0, headerEnd),
            received.slice(headerEnd),
            limits.maxResponseBytes,
            false,
            limits.method,
        );
    }
    if (parsed === undefined) {
        const received = concatHttpBytes(chunks, totalLength);
        parsed = parseHttpResponse(
            received.slice(0, headerEnd),
            received.slice(headerEnd),
            limits.maxResponseBytes,
            true,
            limits.method,
        );
    }
    return parsed;
}

const HTTP2_CLIENT_PREFACE = Text.utf8.encode("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n");
const HTTP2_FRAME_DATA = 0x0;
const HTTP2_FRAME_HEADERS = 0x1;
const HTTP2_FRAME_RST_STREAM = 0x3;
const HTTP2_FRAME_SETTINGS = 0x4;
const HTTP2_FRAME_PING = 0x6;
const HTTP2_FRAME_GOAWAY = 0x7;
const HTTP2_FRAME_WINDOW_UPDATE = 0x8;
const HTTP2_FRAME_CONTINUATION = 0x9;
const HTTP2_FLAG_END_STREAM = 0x1;
const HTTP2_FLAG_ACK = 0x1;
const HTTP2_FLAG_END_HEADERS = 0x4;
const HTTP2_DEFAULT_MAX_FRAME_SIZE = 16384;
const HTTP2_DEFAULT_DYNAMIC_TABLE_BYTES = 4096;
const HTTP2_SETTING_ENABLE_PUSH = 0x2;
const HTTP2_SETTING_MAX_FRAME_SIZE = 0x5;
const HTTP2_SETTING_MAX_HEADER_LIST_SIZE = 0x6;
const HTTP2_ERROR_CANCEL = 0x8;

const HTTP2_HPACK_STATIC = [
    undefined,
    [":authority", ""],
    [":method", "GET"],
    [":method", "POST"],
    [":path", "/"],
    [":path", "/index.html"],
    [":scheme", "http"],
    [":scheme", "https"],
    [":status", "200"],
    [":status", "204"],
    [":status", "206"],
    [":status", "304"],
    [":status", "400"],
    [":status", "404"],
    [":status", "500"],
    ["accept-charset", ""],
    ["accept-encoding", "gzip, deflate"],
    ["accept-language", ""],
    ["accept-ranges", ""],
    ["accept", ""],
    ["access-control-allow-origin", ""],
    ["age", ""],
    ["allow", ""],
    ["authorization", ""],
    ["cache-control", ""],
    ["content-disposition", ""],
    ["content-encoding", ""],
    ["content-language", ""],
    ["content-length", ""],
    ["content-location", ""],
    ["content-range", ""],
    ["content-type", ""],
    ["cookie", ""],
    ["date", ""],
    ["etag", ""],
    ["expect", ""],
    ["expires", ""],
    ["from", ""],
    ["host", ""],
    ["if-match", ""],
    ["if-modified-since", ""],
    ["if-none-match", ""],
    ["if-range", ""],
    ["if-unmodified-since", ""],
    ["last-modified", ""],
    ["link", ""],
    ["location", ""],
    ["max-forwards", ""],
    ["proxy-authenticate", ""],
    ["proxy-authorization", ""],
    ["range", ""],
    ["referer", ""],
    ["refresh", ""],
    ["retry-after", ""],
    ["server", ""],
    ["set-cookie", ""],
    ["strict-transport-security", ""],
    ["transfer-encoding", ""],
    ["user-agent", ""],
    ["vary", ""],
    ["via", ""],
    ["www-authenticate", ""],
];

const HTTP2_HPACK_NAME_INDEX = new Map();
for (let index = 1; index < HTTP2_HPACK_STATIC.length; index += 1) {
    const name = HTTP2_HPACK_STATIC[index][0];
    if (!HTTP2_HPACK_NAME_INDEX.has(name)) {
        HTTP2_HPACK_NAME_INDEX.set(name, index);
    }
}

const HTTP2_HPACK_HUFFMAN_EOS = 256;
const HTTP2_HPACK_HUFFMAN_CODES = [
    [0x1ff8, 13],
    [0x7fffd8, 23],
    [0xfffffe2, 28],
    [0xfffffe3, 28],
    [0xfffffe4, 28],
    [0xfffffe5, 28],
    [0xfffffe6, 28],
    [0xfffffe7, 28],
    [0xfffffe8, 28],
    [0xffffea, 24],
    [0x3ffffffc, 30],
    [0xfffffe9, 28],
    [0xfffffea, 28],
    [0x3ffffffd, 30],
    [0xfffffeb, 28],
    [0xfffffec, 28],
    [0xfffffed, 28],
    [0xfffffee, 28],
    [0xfffffef, 28],
    [0xffffff0, 28],
    [0xffffff1, 28],
    [0xffffff2, 28],
    [0x3ffffffe, 30],
    [0xffffff3, 28],
    [0xffffff4, 28],
    [0xffffff5, 28],
    [0xffffff6, 28],
    [0xffffff7, 28],
    [0xffffff8, 28],
    [0xffffff9, 28],
    [0xffffffa, 28],
    [0xffffffb, 28],
    [0x14, 6],
    [0x3f8, 10],
    [0x3f9, 10],
    [0xffa, 12],
    [0x1ff9, 13],
    [0x15, 6],
    [0xf8, 8],
    [0x7fa, 11],
    [0x3fa, 10],
    [0x3fb, 10],
    [0xf9, 8],
    [0x7fb, 11],
    [0xfa, 8],
    [0x16, 6],
    [0x17, 6],
    [0x18, 6],
    [0x0, 5],
    [0x1, 5],
    [0x2, 5],
    [0x19, 6],
    [0x1a, 6],
    [0x1b, 6],
    [0x1c, 6],
    [0x1d, 6],
    [0x1e, 6],
    [0x1f, 6],
    [0x5c, 7],
    [0xfb, 8],
    [0x7ffc, 15],
    [0x20, 6],
    [0xffb, 12],
    [0x3fc, 10],
    [0x1ffa, 13],
    [0x21, 6],
    [0x5d, 7],
    [0x5e, 7],
    [0x5f, 7],
    [0x60, 7],
    [0x61, 7],
    [0x62, 7],
    [0x63, 7],
    [0x64, 7],
    [0x65, 7],
    [0x66, 7],
    [0x67, 7],
    [0x68, 7],
    [0x69, 7],
    [0x6a, 7],
    [0x6b, 7],
    [0x6c, 7],
    [0x6d, 7],
    [0x6e, 7],
    [0x6f, 7],
    [0x70, 7],
    [0x71, 7],
    [0x72, 7],
    [0xfc, 8],
    [0x73, 7],
    [0xfd, 8],
    [0x1ffb, 13],
    [0x7fff0, 19],
    [0x1ffc, 13],
    [0x3ffc, 14],
    [0x22, 6],
    [0x7ffd, 15],
    [0x3, 5],
    [0x23, 6],
    [0x4, 5],
    [0x24, 6],
    [0x5, 5],
    [0x25, 6],
    [0x26, 6],
    [0x27, 6],
    [0x6, 5],
    [0x74, 7],
    [0x75, 7],
    [0x28, 6],
    [0x29, 6],
    [0x2a, 6],
    [0x7, 5],
    [0x2b, 6],
    [0x76, 7],
    [0x2c, 6],
    [0x8, 5],
    [0x9, 5],
    [0x2d, 6],
    [0x77, 7],
    [0x78, 7],
    [0x79, 7],
    [0x7a, 7],
    [0x7b, 7],
    [0x7ffe, 15],
    [0x7fc, 11],
    [0x3ffd, 14],
    [0x1ffd, 13],
    [0xffffffc, 28],
    [0xfffe6, 20],
    [0x3fffd2, 22],
    [0xfffe7, 20],
    [0xfffe8, 20],
    [0x3fffd3, 22],
    [0x3fffd4, 22],
    [0x3fffd5, 22],
    [0x7fffd9, 23],
    [0x3fffd6, 22],
    [0x7fffda, 23],
    [0x7fffdb, 23],
    [0x7fffdc, 23],
    [0x7fffdd, 23],
    [0x7fffde, 23],
    [0xffffeb, 24],
    [0x7fffdf, 23],
    [0xffffec, 24],
    [0xffffed, 24],
    [0x3fffd7, 22],
    [0x7fffe0, 23],
    [0xffffee, 24],
    [0x7fffe1, 23],
    [0x7fffe2, 23],
    [0x7fffe3, 23],
    [0x7fffe4, 23],
    [0x1fffdc, 21],
    [0x3fffd8, 22],
    [0x7fffe5, 23],
    [0x3fffd9, 22],
    [0x7fffe6, 23],
    [0x7fffe7, 23],
    [0xffffef, 24],
    [0x3fffda, 22],
    [0x1fffdd, 21],
    [0xfffe9, 20],
    [0x3fffdb, 22],
    [0x3fffdc, 22],
    [0x7fffe8, 23],
    [0x7fffe9, 23],
    [0x1fffde, 21],
    [0x7fffea, 23],
    [0x3fffdd, 22],
    [0x3fffde, 22],
    [0xfffff0, 24],
    [0x1fffdf, 21],
    [0x3fffdf, 22],
    [0x7fffeb, 23],
    [0x7fffec, 23],
    [0x1fffe0, 21],
    [0x1fffe1, 21],
    [0x3fffe0, 22],
    [0x1fffe2, 21],
    [0x7fffed, 23],
    [0x3fffe1, 22],
    [0x7fffee, 23],
    [0x7fffef, 23],
    [0xfffea, 20],
    [0x3fffe2, 22],
    [0x3fffe3, 22],
    [0x3fffe4, 22],
    [0x7ffff0, 23],
    [0x3fffe5, 22],
    [0x3fffe6, 22],
    [0x7ffff1, 23],
    [0x3ffffe0, 26],
    [0x3ffffe1, 26],
    [0xfffeb, 20],
    [0x7fff1, 19],
    [0x3fffe7, 22],
    [0x7ffff2, 23],
    [0x3fffe8, 22],
    [0x1ffffec, 25],
    [0x3ffffe2, 26],
    [0x3ffffe3, 26],
    [0x3ffffe4, 26],
    [0x7ffffde, 27],
    [0x7ffffdf, 27],
    [0x3ffffe5, 26],
    [0xfffff1, 24],
    [0x1ffffed, 25],
    [0x7fff2, 19],
    [0x1fffe3, 21],
    [0x3ffffe6, 26],
    [0x7ffffe0, 27],
    [0x7ffffe1, 27],
    [0x3ffffe7, 26],
    [0x7ffffe2, 27],
    [0xfffff2, 24],
    [0x1fffe4, 21],
    [0x1fffe5, 21],
    [0x3ffffe8, 26],
    [0x3ffffe9, 26],
    [0xffffffd, 28],
    [0x7ffffe3, 27],
    [0x7ffffe4, 27],
    [0x7ffffe5, 27],
    [0xfffec, 20],
    [0xfffff3, 24],
    [0xfffed, 20],
    [0x1fffe6, 21],
    [0x3fffe9, 22],
    [0x1fffe7, 21],
    [0x1fffe8, 21],
    [0x7ffff3, 23],
    [0x3fffea, 22],
    [0x3fffeb, 22],
    [0x1ffffee, 25],
    [0x1ffffef, 25],
    [0xfffff4, 24],
    [0xfffff5, 24],
    [0x3ffffea, 26],
    [0x7ffff4, 23],
    [0x3ffffeb, 26],
    [0x7ffffe6, 27],
    [0x3ffffec, 26],
    [0x3ffffed, 26],
    [0x7ffffe7, 27],
    [0x7ffffe8, 27],
    [0x7ffffe9, 27],
    [0x7ffffea, 27],
    [0x7ffffeb, 27],
    [0xffffffe, 28],
    [0x7ffffec, 27],
    [0x7ffffed, 27],
    [0x7ffffee, 27],
    [0x7ffffef, 27],
    [0x7fffff0, 27],
    [0x3ffffee, 26],
    [0x3fffffff, 30],
];

function buildHpackHuffmanTree() {
    const root = {};
    for (let symbol = 0; symbol < HTTP2_HPACK_HUFFMAN_CODES.length; symbol += 1) {
        const [code, length] = HTTP2_HPACK_HUFFMAN_CODES[symbol];
        let node = root;
        for (let bitIndex = length - 1; bitIndex >= 0; bitIndex -= 1) {
            const bit = (code >>> bitIndex) & 1;
            node[bit] ??= {};
            node = node[bit];
        }
        node.symbol = symbol;
    }
    return root;
}

const HTTP2_HPACK_HUFFMAN_TREE = buildHpackHuffmanTree();

function http2Frame(type, flags, streamId, payload = new Uint8Array(0)) {
    if (!(payload instanceof Uint8Array) || payload.byteLength > 0xffffff) {
        throw httpClientError(
            "HttpClientRequestLimitError",
            "SLOPPY_E_HTTP_CLIENT_REQUEST_BODY_LIMIT",
            "HTTP/2 frame payload exceeded the supported frame size.",
        );
    }
    const frame = new Uint8Array(9 + payload.byteLength);
    frame[0] = (payload.byteLength >>> 16) & 0xff;
    frame[1] = (payload.byteLength >>> 8) & 0xff;
    frame[2] = payload.byteLength & 0xff;
    frame[3] = type;
    frame[4] = flags;
    frame[5] = (streamId >>> 24) & 0x7f;
    frame[6] = (streamId >>> 16) & 0xff;
    frame[7] = (streamId >>> 8) & 0xff;
    frame[8] = streamId & 0xff;
    frame.set(payload, 9);
    return frame;
}

function http2Concat(parts) {
    let total = 0;
    for (const part of parts) {
        total += part.byteLength;
    }
    const bytes = new Uint8Array(total);
    let offset = 0;
    for (const part of parts) {
        bytes.set(part, offset);
        offset += part.byteLength;
    }
    return bytes;
}

function http2Uint32(value) {
    return new Uint8Array([
        (value >>> 24) & 0xff,
        (value >>> 16) & 0xff,
        (value >>> 8) & 0xff,
        value & 0xff,
    ]);
}

function http2Setting(id, value) {
    return new Uint8Array([
        (id >>> 8) & 0xff,
        id & 0xff,
        (value >>> 24) & 0xff,
        (value >>> 16) & 0xff,
        (value >>> 8) & 0xff,
        value & 0xff,
    ]);
}

function http2DataFrames(streamId, body) {
    if (body.byteLength === 0) {
        return [];
    }
    const frames = [];
    for (let offset = 0; offset < body.byteLength; offset += HTTP2_DEFAULT_MAX_FRAME_SIZE) {
        const end = Math.min(body.byteLength, offset + HTTP2_DEFAULT_MAX_FRAME_SIZE);
        const flags = end === body.byteLength ? HTTP2_FLAG_END_STREAM : 0;
        frames.push(http2Frame(HTTP2_FRAME_DATA, flags, streamId, body.slice(offset, end)));
    }
    return frames;
}

function http2HeaderFrames(streamId, headerBlock, endStream, maxFrameSize = HTTP2_DEFAULT_MAX_FRAME_SIZE) {
    const frames = [];
    let offset = 0;
    while (offset < headerBlock.byteLength || frames.length === 0) {
        const end = Math.min(headerBlock.byteLength, offset + maxFrameSize);
        const final = end === headerBlock.byteLength;
        const type = frames.length === 0 ? HTTP2_FRAME_HEADERS : HTTP2_FRAME_CONTINUATION;
        let flags = final ? HTTP2_FLAG_END_HEADERS : 0;
        if (frames.length === 0 && endStream) {
            flags |= HTTP2_FLAG_END_STREAM;
        }
        frames.push(http2Frame(type, flags, streamId, headerBlock.slice(offset, end)));
        offset = end;
        if (headerBlock.byteLength === 0) {
            break;
        }
    }
    return frames;
}

function http2UnpadPayload(payload) {
    if (payload.byteLength === 0) {
        throw httpClientError(
            "HttpClientMalformedResponseError",
            "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
            "HTTP/2 padded frame is missing the Pad Length field.",
        );
    }
    const padLength = payload[0];
    if (padLength >= payload.byteLength) {
        throw httpClientError(
            "HttpClientMalformedResponseError",
            "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
            "HTTP/2 padded frame declares padding beyond the payload.",
        );
    }
    return payload.slice(1, payload.byteLength - padLength);
}

function http2WindowUpdateFrame(streamId, increment) {
    if (!Number.isInteger(increment) || increment <= 0 || increment > 0x7fffffff) {
        throw httpClientError(
            "HttpClientMalformedResponseError",
            "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
            "HTTP/2 flow-control increment is invalid.",
        );
    }
    return http2Frame(HTTP2_FRAME_WINDOW_UPDATE, 0, streamId, http2Uint32(increment));
}

function http2RstStreamFrame(streamId, errorCode = HTTP2_ERROR_CANCEL) {
    return http2Frame(HTTP2_FRAME_RST_STREAM, 0, streamId, http2Uint32(errorCode));
}

function hpackInteger(value, prefixBits, prefixMask) {
    const maxPrefix = (1 << prefixBits) - 1;
    const bytes = [];
    if (value < maxPrefix) {
        bytes.push(prefixMask | value);
    } else {
        bytes.push(prefixMask | maxPrefix);
        value -= maxPrefix;
        while (value >= 128) {
            bytes.push((value % 128) + 128);
            value = Math.floor(value / 128);
        }
        bytes.push(value);
    }
    return new Uint8Array(bytes);
}

function hpackReadInteger(bytes, offset, prefixBits) {
    const maxPrefix = (1 << prefixBits) - 1;
    let value = bytes[offset] & maxPrefix;
    offset += 1;
    if (value !== maxPrefix) {
        return { value, offset };
    }
    let shift = 0;
    while (offset < bytes.byteLength) {
        const byte = bytes[offset];
        offset += 1;
        value += (byte & 0x7f) * (2 ** shift);
        if ((byte & 0x80) === 0) {
            return { value, offset };
        }
        shift += 7;
    }
    throw httpClientError(
        "HttpClientMalformedResponseError",
        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
        "HTTP/2 HPACK integer is incomplete.",
    );
}

function hpackString(value) {
    const bytes = Text.utf8.encode(value);
    return http2Concat([hpackInteger(bytes.byteLength, 7, 0x00), bytes]);
}

function hpackDecodeHuffman(bytes) {
    const output = [];
    let node = HTTP2_HPACK_HUFFMAN_TREE;
    let pendingBits = 0;
    let pendingValue = 0;

    for (const byte of bytes) {
        for (let bitIndex = 7; bitIndex >= 0; bitIndex -= 1) {
            const bit = (byte >>> bitIndex) & 1;
            node = node[bit];
            pendingBits += 1;
            pendingValue = (pendingValue << 1) | bit;
            if (node === undefined) {
                throw httpClientError(
                    "HttpClientMalformedResponseError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 HPACK Huffman string contains an invalid code.",
                );
            }
            if (node.symbol !== undefined) {
                if (node.symbol === HTTP2_HPACK_HUFFMAN_EOS) {
                    throw httpClientError(
                        "HttpClientMalformedResponseError",
                        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 HPACK Huffman string contains the EOS symbol.",
                    );
                }
                output.push(node.symbol);
                node = HTTP2_HPACK_HUFFMAN_TREE;
                pendingBits = 0;
                pendingValue = 0;
            }
        }
    }

    if (node !== HTTP2_HPACK_HUFFMAN_TREE) {
        if (pendingBits > 7 || pendingValue !== (1 << pendingBits) - 1) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 HPACK Huffman string has invalid padding.",
            );
        }
    }

    return new Uint8Array(output);
}

function hpackReadString(bytes, offset) {
    if (offset >= bytes.byteLength) {
        throw httpClientError(
            "HttpClientMalformedResponseError",
            "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
            "HTTP/2 HPACK string is incomplete.",
        );
    }
    const huffman = (bytes[offset] & 0x80) !== 0;
    const length = hpackReadInteger(bytes, offset, 7);
    offset = length.offset;
    if (offset + length.value > bytes.byteLength) {
        throw httpClientError(
            "HttpClientMalformedResponseError",
            "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
            "HTTP/2 HPACK string length exceeds the header block.",
        );
    }
    const encoded = bytes.slice(offset, offset + length.value);
    const decoded = huffman ? hpackDecodeHuffman(encoded) : encoded;
    return {
        value: Text.utf8.decode(decoded),
        offset: offset + length.value,
    };
}

function hpackHeader(name, value, sensitive = false) {
    const normalized = name.toLowerCase();
    if (normalized === ":method" && value === "GET") {
        return hpackInteger(2, 7, 0x80);
    }
    if (normalized === ":method" && value === "POST") {
        return hpackInteger(3, 7, 0x80);
    }
    if (normalized === ":path" && value === "/") {
        return hpackInteger(4, 7, 0x80);
    }
    if (normalized === ":scheme" && value === "http") {
        return hpackInteger(6, 7, 0x80);
    }
    if (normalized === ":scheme" && value === "https") {
        return hpackInteger(7, 7, 0x80);
    }
    const nameIndex = HTTP2_HPACK_NAME_INDEX.get(normalized);
    const prefix = sensitive ? 0x10 : 0x00;
    const nameBytes = nameIndex === undefined ? hpackString(normalized) : new Uint8Array(0);
    const indexedName = nameIndex === undefined ? hpackInteger(0, 4, prefix) : hpackInteger(nameIndex, 4, prefix);
    return http2Concat([indexedName, nameBytes, hpackString(value)]);
}

function hpackEncodeRequestHeaders(request) {
    const parts = [
        hpackHeader(":method", request.method),
        hpackHeader(":scheme", request.url.scheme),
        hpackHeader(":authority", request.url.hostHeader),
        hpackHeader(":path", request.url.target),
    ];
    if (!request.headers.has("accept")) {
        parts.push(hpackHeader("accept", "*/*"));
    }
    for (const { name, value } of request.headers.values()) {
        const normalized = name.toLowerCase();
        if (
            normalized === "connection" ||
            normalized === "upgrade" ||
            normalized === "keep-alive" ||
            normalized === "proxy-connection" ||
            normalized === "transfer-encoding" ||
            (normalized === "te" && value.toLowerCase() !== "trailers")
        ) {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                `HTTP/2 request header "${name}" is not allowed.`,
            );
        }
        parts.push(hpackHeader(name, value, isHttpSensitiveHeader(name, value, new Set())));
    }
    if (request.body.byteLength > 0) {
        parts.push(hpackHeader("content-length", String(request.body.byteLength)));
    }
    return http2Concat(parts);
}

function hpackIndexedHeader(index, dynamicTable) {
    if (index > 0 && index < HTTP2_HPACK_STATIC.length) {
        return HTTP2_HPACK_STATIC[index];
    }
    const dynamicIndex = index - HTTP2_HPACK_STATIC.length;
    if (dynamicIndex >= 0 && dynamicIndex < dynamicTable.length) {
        return dynamicTable[dynamicIndex];
    }
    throw httpClientError(
        "HttpClientMalformedResponseError",
        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
        "HTTP/2 HPACK header index is invalid.",
    );
}

function hpackDynamicEntryBytes(header) {
    return Text.utf8.encode(header[0]).byteLength + Text.utf8.encode(header[1]).byteLength + 32;
}

function hpackTrimDynamicTable(dynamicTable, maxBytes) {
    let total = 0;
    for (let index = 0; index < dynamicTable.length; index += 1) {
        total += hpackDynamicEntryBytes(dynamicTable[index]);
        if (total > maxBytes) {
            dynamicTable.length = index;
            return;
        }
    }
}

function hpackDecodeHeaders(block, dynamicTable, maxDynamicTableBytes = HTTP2_DEFAULT_DYNAMIC_TABLE_BYTES) {
    const headers = [];
    let offset = 0;
    while (offset < block.byteLength) {
        const byte = block[offset];
        if ((byte & 0x80) !== 0) {
            const indexed = hpackReadInteger(block, offset, 7);
            offset = indexed.offset;
            headers.push(hpackIndexedHeader(indexed.value, dynamicTable));
            continue;
        }
        if ((byte & 0xe0) === 0x20) {
            const update = hpackReadInteger(block, offset, 5);
            offset = update.offset;
            if (update.value > maxDynamicTableBytes) {
                throw httpClientError(
                    "HttpClientMalformedResponseError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 HPACK dynamic table size update exceeds the configured limit.",
                );
            }
            hpackTrimDynamicTable(dynamicTable, update.value);
            continue;
        }
        const incremental = (byte & 0x40) !== 0;
        const prefixBits = incremental ? 6 : 4;
        const nameRef = hpackReadInteger(block, offset, prefixBits);
        offset = nameRef.offset;
        let name;
        if (nameRef.value === 0) {
            const decodedName = hpackReadString(block, offset);
            name = decodedName.value;
            offset = decodedName.offset;
        } else {
            name = hpackIndexedHeader(nameRef.value, dynamicTable)[0];
        }
        const decodedValue = hpackReadString(block, offset);
        offset = decodedValue.offset;
        const header = [name, decodedValue.value];
        headers.push(header);
        if (incremental) {
            if (hpackDynamicEntryBytes(header) <= maxDynamicTableBytes) {
                dynamicTable.unshift(header);
                hpackTrimDynamicTable(dynamicTable, maxDynamicTableBytes);
            } else {
                dynamicTable.length = 0;
            }
        }
    }
    return headers;
}

function parseHttp2FrameHeader(bytes, offset) {
    const length = (bytes[offset] << 16) | (bytes[offset + 1] << 8) | bytes[offset + 2];
    return {
        length,
        type: bytes[offset + 3],
        flags: bytes[offset + 4],
        streamId:
            ((bytes[offset + 5] & 0x7f) << 24) |
            (bytes[offset + 6] << 16) |
            (bytes[offset + 7] << 8) |
            bytes[offset + 8],
    };
}

function http2HeaderListBytes(headers) {
    let total = 0;
    for (const [name, value] of headers) {
        total += Text.utf8.encode(name).byteLength + Text.utf8.encode(value).byteLength + 32;
    }
    return total;
}

function parseHttp2Headers(headers, maxHeaderBytes) {
    let status = undefined;
    const regular = [];
    let contentLength = undefined;
    let regularSeen = false;
    if (http2HeaderListBytes(headers) > maxHeaderBytes) {
        throw httpClientError(
            "HttpClientHeaderLimitError",
            "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
            "HTTP/2 response headers exceeded the configured limit.",
        );
    }
    for (const [name, value] of headers) {
        if (name === ":status") {
            if (regularSeen || status !== undefined) {
                throw httpClientError(
                    "HttpClientMalformedResponseError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 response pseudo-headers are malformed.",
                );
            }
            status = Number(value);
        } else if (name.startsWith(":")) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 response contains an unsupported pseudo-header.",
            );
        } else {
            regularSeen = true;
            if (
                /[A-Z]/.test(name) ||
                name.toLowerCase() === "connection" ||
                name.toLowerCase() === "transfer-encoding"
            ) {
                throw httpClientError(
                    "HttpClientMalformedResponseError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 response contains an invalid header field.",
                );
            }
            if (name.toLowerCase() === "content-length") {
                if (!/^[0-9]+$/.test(value)) {
                    throw httpClientError(
                        "HttpClientMalformedResponseError",
                        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 response content-length is invalid.",
                    );
                }
                const parsedLength = Number(value);
                if (!Number.isSafeInteger(parsedLength) || parsedLength < 0) {
                    throw httpClientError(
                        "HttpClientMalformedResponseError",
                        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 response content-length is out of range.",
                    );
                }
                if (contentLength !== undefined && contentLength !== parsedLength) {
                    throw httpClientError(
                        "HttpClientMalformedResponseError",
                        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 response has conflicting content-length headers.",
                    );
                }
                contentLength = parsedLength;
            }
            regular.push([name, value]);
        }
    }
    if (!Number.isInteger(status) || status < 100 || status > 599) {
        throw httpClientError(
            "HttpClientMalformedResponseError",
            "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
            "HTTP/2 response is missing a valid :status header.",
        );
    }
    return { status, headers: regular, contentLength };
}

class Http2ClientSession {
    constructor(connection, pool, originKey) {
        this._connection = connection;
        this._pool = pool;
        this._originKey = originKey;
        this._closed = false;
        this._acceptsStreams = true;
        this._pending = new Uint8Array(0);
        this._dynamicTable = [];
        this._streams = new Map();
        this._nextStreamId = 1;
        this._peerMaxFrameSize = HTTP2_DEFAULT_MAX_FRAME_SIZE;
        this._pendingHeaderStream = 0;
        this._writeQueue = Promise.resolve();
        this._closeListeners = new Set();
        this._reader = undefined;
    }

    get closed() {
        return this._closed;
    }

    get acceptsStreams() {
        return !this._closed && this._acceptsStreams && this._nextStreamId < 0x7fffffff;
    }

    get activeStreamCount() {
        return this._streams.size;
    }

    onClose(listener) {
        if (this._closed) {
            listener();
            return;
        }
        this._closeListeners.add(listener);
    }

    async start() {
        await this._write(
            http2Concat([
                HTTP2_CLIENT_PREFACE,
                http2Frame(HTTP2_FRAME_SETTINGS, 0, 0, http2Setting(HTTP2_SETTING_ENABLE_PUSH, 0)),
            ]),
        );
        this._reader = this._readLoop();
        this._reader.catch(() => {});
    }

    async close() {
        if (this._closed) {
            return;
        }
        this._closed = true;
        this._acceptsStreams = false;
        await this._connection.close().catch(() => {});
        this._notifyClosed();
    }

    abort() {
        return this.close();
    }

    _notifyClosed() {
        const listeners = Array.from(this._closeListeners);
        this._closeListeners.clear();
        for (const listener of listeners) {
            listener();
        }
    }

    async _write(bytes) {
        if (this._closed) {
            throw new Error("SLOPPY_E_NET_CONNECTION_CLOSED");
        }
        const write = this._writeQueue.then(() => this._connection.write(bytes));
        this._writeQueue = write.catch(() => {});
        await write;
    }

    _newStream(request) {
        if (!this.acceptsStreams) {
            throw new Error("SLOPPY_E_NET_CONNECTION_CLOSED");
        }
        const streamId = this._nextStreamId;
        this._nextStreamId += 2;
        const stream = {
            streamId,
            request,
            headerBlocks: [],
            dataChunks: [],
            headerBlockBytes: 0,
            totalBodyBytes: 0,
            response: undefined,
            headerBlockEndsStream: false,
            settled: false,
            resolve: undefined,
            reject: undefined,
        };
        stream.promise = new Promise((resolve, reject) => {
            stream.resolve = resolve;
            stream.reject = reject;
        });
        this._streams.set(streamId, stream);
        return stream;
    }

    async request(request, lifecycle) {
        const stream = this._newStream(request);
        const previousAbort = lifecycle.abort;
        lifecycle.connection = this;
        lifecycle.abort = (reason) => {
            const error = reason === "timeout" ? httpRequestTimeoutError() : httpRequestCancelledError();
            this.cancelStream(stream.streamId, error).catch(() => {});
        };
        try {
            const headers = hpackEncodeRequestHeaders(request);
            const frames = http2HeaderFrames(
                stream.streamId,
                headers,
                request.body.byteLength === 0,
                this._peerMaxFrameSize,
            );
            frames.push(...http2DataFrames(stream.streamId, request.body));
            await this._write(http2Concat(frames));
            return await stream.promise;
        } catch (error) {
            this._finishStream(stream.streamId, undefined, error);
            throw error;
        } finally {
            if (lifecycle.connection === this) {
                lifecycle.connection = undefined;
            }
            if (lifecycle.abort !== previousAbort) {
                lifecycle.abort = previousAbort;
            }
        }
    }

    async cancelStream(streamId, error) {
        if (!this._streams.has(streamId)) {
            return;
        }
        this._finishStream(streamId, undefined, error);
        if (!this._closed) {
            await this._write(http2RstStreamFrame(streamId)).catch(() => {});
        }
    }

    _finishStream(streamId, response, error) {
        const stream = this._streams.get(streamId);
        if (stream === undefined || stream.settled) {
            return;
        }
        stream.settled = true;
        this._streams.delete(streamId);
        if (error !== undefined) {
            stream.reject(error);
        } else {
            stream.resolve(response);
        }
        this._pool?.releaseHttp2(this._originKey, this);
    }

    _failAll(error) {
        for (const streamId of Array.from(this._streams.keys())) {
            this._finishStream(streamId, undefined, error);
        }
    }

    async _readLoop() {
        try {
            while (!this._closed) {
                const frame = await this._readFrame();
                await this._handleFrame(frame);
            }
        } catch (error) {
            if (!this._closed) {
                this._failAll(error);
            }
        } finally {
            this._closed = true;
            this._acceptsStreams = false;
            this._failAll(new Error("SLOPPY_E_NET_CONNECTION_CLOSED"));
            await this._connection.close().catch(() => {});
            this._notifyClosed();
        }
    }

    async _readFrame() {
        while (this._pending.byteLength < 9) {
            const chunk = await this._connection.read({ maxBytes: 8192 });
            this._pending = http2Concat([this._pending, chunk]);
        }
        const frame = parseHttp2FrameHeader(this._pending, 0);
        if (frame.length > HTTP2_DEFAULT_MAX_FRAME_SIZE) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 peer frame exceeded the local max frame size.",
            );
        }
        while (this._pending.byteLength < 9 + frame.length) {
            const chunk = await this._connection.read({ maxBytes: 8192 });
            this._pending = http2Concat([this._pending, chunk]);
        }
        const payload = this._pending.slice(9, 9 + frame.length);
        this._pending = this._pending.slice(9 + frame.length);
        return { ...frame, payload };
    }

    async _handleFrame(frame) {
        if (frame.type === HTTP2_FRAME_SETTINGS) {
            await this._handleSettings(frame);
            return;
        }
        if (frame.type === HTTP2_FRAME_PING) {
            await this._handlePing(frame);
            return;
        }
        if (frame.type === HTTP2_FRAME_GOAWAY) {
            this._acceptsStreams = false;
            this._failAll(
                httpClientError(
                    "HttpClientConnectError",
                    "SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED",
                    "HTTP/2 peer sent GOAWAY before the response completed.",
                ),
            );
            await this.close();
            return;
        }
        const stream = this._streams.get(frame.streamId);
        if (stream === undefined) {
            return;
        }
        if (frame.type === HTTP2_FRAME_RST_STREAM) {
            this._finishStream(
                frame.streamId,
                undefined,
                httpClientError(
                    "HttpClientConnectError",
                    "SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED",
                    "HTTP/2 stream was reset by the peer.",
                ),
            );
            return;
        }
        if (frame.type === HTTP2_FRAME_HEADERS || frame.type === HTTP2_FRAME_CONTINUATION) {
            await this._handleHeaderFrame(stream, frame);
            return;
        }
        if (frame.type === HTTP2_FRAME_DATA) {
            await this._handleDataFrame(stream, frame);
        }
    }

    async _handleSettings(frame) {
        const payload = frame.payload;
        if (frame.streamId !== 0 || ((frame.flags & HTTP2_FLAG_ACK) !== 0 && frame.length !== 0)) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 SETTINGS frame is malformed.",
            );
        }
        if ((frame.flags & HTTP2_FLAG_ACK) !== 0) {
            return;
        }
        if (payload.byteLength % 6 !== 0) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 SETTINGS payload is malformed.",
            );
        }
        for (let offset = 0; offset < payload.byteLength; offset += 6) {
            const id = (payload[offset] << 8) | payload[offset + 1];
            const value =
                payload[offset + 2] * 0x1000000 +
                (payload[offset + 3] << 16) +
                (payload[offset + 4] << 8) +
                payload[offset + 5];
            if (id === HTTP2_SETTING_MAX_FRAME_SIZE) {
                if (value < HTTP2_DEFAULT_MAX_FRAME_SIZE || value > 16777215) {
                    throw httpClientError(
                        "HttpClientMalformedResponseError",
                        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 SETTINGS_MAX_FRAME_SIZE is invalid.",
                    );
                }
                this._peerMaxFrameSize = value;
            }
        }
        await this._write(http2Frame(HTTP2_FRAME_SETTINGS, HTTP2_FLAG_ACK, 0));
    }

    async _handlePing(frame) {
        if (frame.streamId !== 0 || frame.length !== 8) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 PING frame is malformed.",
            );
        }
        if ((frame.flags & HTTP2_FLAG_ACK) === 0) {
            await this._write(http2Frame(HTTP2_FRAME_PING, HTTP2_FLAG_ACK, 0, frame.payload));
        }
    }

    async _handleHeaderFrame(stream, frame) {
        let payload = frame.payload;
        if (frame.type === HTTP2_FRAME_CONTINUATION) {
            if (this._pendingHeaderStream !== frame.streamId) {
                throw httpClientError(
                    "HttpClientMalformedResponseError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 CONTINUATION frame is out of sequence.",
                );
            }
        } else if (this._pendingHeaderStream !== 0) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 response started a new header block before END_HEADERS.",
            );
        }
        if ((frame.flags & 0x8) !== 0) {
            payload = http2UnpadPayload(payload);
        }
        if (frame.type === HTTP2_FRAME_HEADERS && (frame.flags & 0x20) !== 0) {
            if (payload.byteLength < 5) {
                throw httpClientError(
                    "HttpClientMalformedResponseError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 response priority field is incomplete.",
                );
            }
            payload = payload.slice(5);
        }
        stream.headerBlockBytes += payload.byteLength;
        if (stream.headerBlockBytes > stream.request.maxHeaderBytes) {
            throw httpClientError(
                "HttpClientHeaderLimitError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 response headers exceeded the configured limit.",
            );
        }
        stream.headerBlocks.push(payload);
        stream.headerBlockEndsStream =
            stream.headerBlockEndsStream || (frame.flags & HTTP2_FLAG_END_STREAM) !== 0;
        if ((frame.flags & HTTP2_FLAG_END_HEADERS) === 0) {
            this._pendingHeaderStream = frame.streamId;
            return;
        }

        const blockEndedStream = stream.headerBlockEndsStream;
        const decoded = hpackDecodeHeaders(http2Concat(stream.headerBlocks), this._dynamicTable);
        const parsed = parseHttp2Headers(decoded, stream.request.maxHeaderBytes);
        if (parsed.status >= 100 && parsed.status < 200) {
            if (blockEndedStream) {
                throw httpClientError(
                    "HttpClientMalformedResponseError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 informational response ended the stream.",
                );
            }
        } else if (stream.response === undefined) {
            stream.response = parsed;
        }
        stream.headerBlocks.length = 0;
        stream.headerBlockBytes = 0;
        stream.headerBlockEndsStream = false;
        this._pendingHeaderStream = 0;
        if (blockEndedStream && stream.response !== undefined) {
            this._finishStream(stream.streamId, this._buildResponse(stream));
        }
    }

    async _handleDataFrame(stream, frame) {
        let payload = frame.payload;
        if (stream.response === undefined) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 response DATA arrived before final response headers.",
            );
        }
        if ((frame.flags & 0x8) !== 0) {
            payload = http2UnpadPayload(payload);
        }
        stream.totalBodyBytes += payload.byteLength;
        const bodyForbidden =
            stream.request.method === "HEAD" || isHttpBodyForbiddenStatus(stream.response.status);
        if (bodyForbidden && payload.byteLength !== 0) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 response included DATA for a body-forbidden response.",
            );
        }
        if (
            stream.response.contentLength !== undefined &&
            stream.totalBodyBytes > stream.response.contentLength
        ) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 response body exceeded declared content-length.",
            );
        }
        if (stream.totalBodyBytes > stream.request.maxResponseBytes) {
            throw httpClientError(
                "HttpClientResponseLimitError",
                "SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT",
                "HTTP response body exceeded the configured limit.",
            );
        }
        if (payload.byteLength > 0) {
            await this._write(
                http2Concat([
                    http2WindowUpdateFrame(0, payload.byteLength),
                    http2WindowUpdateFrame(stream.streamId, payload.byteLength),
                ]),
            );
            stream.dataChunks.push(payload);
        }
        if ((frame.flags & HTTP2_FLAG_END_STREAM) !== 0) {
            this._finishStream(stream.streamId, this._buildResponse(stream));
        }
    }

    _buildResponse(stream) {
        const response = stream.response;
        if (response === undefined) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 response ended before response headers were received.",
            );
        }
        const bodyForbidden =
            stream.request.method === "HEAD" || isHttpBodyForbiddenStatus(response.status);
        if (
            !bodyForbidden &&
            response.contentLength !== undefined &&
            stream.totalBodyBytes !== response.contentLength
        ) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 response body length did not match declared content-length.",
            );
        }
        return new HttpClientResponse(
            response.status,
            "",
            new HttpHeaderBag(response.headers),
            bodyForbidden ? new Uint8Array(0) : concatHttpBytes(stream.dataChunks, stream.totalBodyBytes),
            false,
        );
    }
}

async function readHttp2Response(connection, request) {
    let pending = new Uint8Array(0);
    const dynamicTable = [];
    const headerBlocks = [];
    const dataChunks = [];
    let headerBlockBytes = 0;
    let totalBodyBytes = 0;
    let response = undefined;
    let pendingHeaderStream = 0;
    let headerBlockEndsStream = false;
    let peerMaxFrameSize = HTTP2_DEFAULT_MAX_FRAME_SIZE;
    let peerMaxHeaderListSize = request.maxHeaderBytes;

    while (true) {
        while (pending.byteLength < 9) {
            const chunk = await connection.read({ maxBytes: 8192 });
            pending = http2Concat([pending, chunk]);
        }
        const frame = parseHttp2FrameHeader(pending, 0);
        if (frame.length > peerMaxFrameSize) {
            throw httpClientError(
                "HttpClientMalformedResponseError",
                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                "HTTP/2 peer frame exceeded the current max frame size.",
            );
        }
        if (pending.byteLength < 9 + frame.length) {
            const chunk = await connection.read({ maxBytes: 8192 });
            pending = http2Concat([pending, chunk]);
            continue;
        }
        let payload = pending.slice(9, 9 + frame.length);
        pending = pending.slice(9 + frame.length);

        if (frame.type === HTTP2_FRAME_SETTINGS) {
            if (frame.streamId !== 0 || ((frame.flags & HTTP2_FLAG_ACK) !== 0 && frame.length !== 0)) {
                throw httpClientError(
                    "HttpClientMalformedResponseError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 SETTINGS frame is malformed.",
                );
            }
            if ((frame.flags & HTTP2_FLAG_ACK) === 0) {
                if (payload.byteLength % 6 !== 0) {
                    throw httpClientError(
                        "HttpClientMalformedResponseError",
                        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 SETTINGS payload is malformed.",
                    );
                }
                for (let offset = 0; offset < payload.byteLength; offset += 6) {
                    const id = (payload[offset] << 8) | payload[offset + 1];
                    const value =
                        payload[offset + 2] * 0x1000000 +
                        (payload[offset + 3] << 16) +
                        (payload[offset + 4] << 8) +
                        payload[offset + 5];
                    if (id === HTTP2_SETTING_MAX_FRAME_SIZE) {
                        if (value < HTTP2_DEFAULT_MAX_FRAME_SIZE || value > 16777215) {
                            throw httpClientError(
                                "HttpClientMalformedResponseError",
                                "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                                "HTTP/2 SETTINGS_MAX_FRAME_SIZE is invalid.",
                            );
                        }
                        peerMaxFrameSize = value;
                    } else if (id === HTTP2_SETTING_MAX_HEADER_LIST_SIZE) {
                        peerMaxHeaderListSize = Math.min(peerMaxHeaderListSize, value);
                    }
                }
                await connection.write(http2Frame(HTTP2_FRAME_SETTINGS, HTTP2_FLAG_ACK, 0));
            }
            continue;
        }
        if (frame.type === HTTP2_FRAME_PING) {
            if (frame.streamId !== 0 || frame.length !== 8) {
                throw httpClientError(
                    "HttpClientMalformedResponseError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 PING frame is malformed.",
                );
            }
            if ((frame.flags & HTTP2_FLAG_ACK) === 0) {
                await connection.write(http2Frame(HTTP2_FRAME_PING, HTTP2_FLAG_ACK, 0, payload));
            }
            continue;
        }
        if (frame.type === HTTP2_FRAME_GOAWAY) {
            if (response !== undefined) {
                continue;
            }
            throw httpClientError(
                "HttpClientConnectError",
                "SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED",
                "HTTP/2 peer sent GOAWAY before the response completed.",
            );
        }
        if (frame.streamId !== 1) {
            continue;
        }
        if (frame.type === HTTP2_FRAME_RST_STREAM) {
            throw httpClientError(
                "HttpClientConnectError",
                "SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED",
                "HTTP/2 stream was reset by the peer.",
            );
        }
        if (frame.type === HTTP2_FRAME_HEADERS || frame.type === HTTP2_FRAME_CONTINUATION) {
            if (frame.type === HTTP2_FRAME_CONTINUATION) {
                if (pendingHeaderStream !== frame.streamId) {
                    throw httpClientError(
                        "HttpClientMalformedResponseError",
                        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 CONTINUATION frame is out of sequence.",
                    );
                }
            } else if (pendingHeaderStream !== 0) {
                throw httpClientError(
                    "HttpClientMalformedResponseError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 response started a new header block before END_HEADERS.",
                );
            }
            if ((frame.flags & 0x8) !== 0) {
                payload = http2UnpadPayload(payload);
            }
            if (frame.type === HTTP2_FRAME_HEADERS && (frame.flags & 0x20) !== 0) {
                if (payload.byteLength < 5) {
                    throw httpClientError(
                        "HttpClientMalformedResponseError",
                        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 response priority field is incomplete.",
                    );
                }
                payload = payload.slice(5);
            }
            headerBlockBytes += payload.byteLength;
            if (headerBlockBytes > request.maxHeaderBytes) {
                throw httpClientError(
                    "HttpClientHeaderLimitError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 response headers exceeded the configured limit.",
                );
            }
            headerBlocks.push(payload);
            headerBlockEndsStream = headerBlockEndsStream || (frame.flags & HTTP2_FLAG_END_STREAM) !== 0;
            if ((frame.flags & HTTP2_FLAG_END_HEADERS) !== 0) {
                const blockEndedStream = headerBlockEndsStream;
                const decoded = hpackDecodeHeaders(http2Concat(headerBlocks), dynamicTable);
                const parsed = parseHttp2Headers(decoded, peerMaxHeaderListSize);
                if (parsed.status >= 100 && parsed.status < 200) {
                    if (blockEndedStream) {
                        throw httpClientError(
                            "HttpClientMalformedResponseError",
                            "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                            "HTTP/2 informational response ended the stream.",
                        );
                    }
                } else if (response === undefined) {
                    response = parsed;
                }
                headerBlocks.length = 0;
                headerBlockBytes = 0;
                pendingHeaderStream = 0;
                headerBlockEndsStream = false;
                if (blockEndedStream && response !== undefined) {
                    break;
                }
            } else {
                pendingHeaderStream = frame.streamId;
            }
            if (headerBlockEndsStream && pendingHeaderStream === 0) {
                break;
            }
            continue;
        }
        if (frame.type === HTTP2_FRAME_DATA) {
            if (response === undefined) {
                throw httpClientError(
                    "HttpClientMalformedResponseError",
                    "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                    "HTTP/2 response DATA arrived before final response headers.",
                );
            }
            if ((frame.flags & 0x8) !== 0) {
                payload = http2UnpadPayload(payload);
            }
            totalBodyBytes += payload.byteLength;
            if (response !== undefined) {
                const bodyForbidden = request.method === "HEAD" || isHttpBodyForbiddenStatus(response.status);
                if (bodyForbidden && payload.byteLength !== 0) {
                    throw httpClientError(
                        "HttpClientMalformedResponseError",
                        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 response included DATA for a body-forbidden response.",
                    );
                }
                if (response.contentLength !== undefined && totalBodyBytes > response.contentLength) {
                    throw httpClientError(
                        "HttpClientMalformedResponseError",
                        "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
                        "HTTP/2 response body exceeded declared content-length.",
                    );
                }
            }
            if (totalBodyBytes > request.maxResponseBytes) {
                throw httpClientError(
                    "HttpClientResponseLimitError",
                    "SLOPPY_E_HTTP_CLIENT_RESPONSE_BODY_LIMIT",
                    "HTTP response body exceeded the configured limit.",
                );
            }
            if (payload.byteLength > 0) {
                await connection.write(
                    http2Concat([
                        http2WindowUpdateFrame(0, payload.byteLength),
                        http2WindowUpdateFrame(1, payload.byteLength),
                    ]),
                );
            }
            dataChunks.push(payload);
            if ((frame.flags & HTTP2_FLAG_END_STREAM) !== 0) {
                break;
            }
        }
    }

    if (response === undefined) {
        throw httpClientError(
            "HttpClientMalformedResponseError",
            "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
            "HTTP/2 response ended before response headers were received.",
        );
    }

    const bodyForbidden = request.method === "HEAD" || isHttpBodyForbiddenStatus(response.status);
    if (!bodyForbidden && response.contentLength !== undefined && totalBodyBytes !== response.contentLength) {
        throw httpClientError(
            "HttpClientMalformedResponseError",
            "SLOPPY_E_HTTP_CLIENT_MALFORMED_RESPONSE",
            "HTTP/2 response body length did not match declared content-length.",
        );
    }
    return new HttpClientResponse(
        response.status,
        "",
        new HttpHeaderBag(response.headers),
        bodyForbidden ? new Uint8Array(0) : concatHttpBytes(dataChunks, totalBodyBytes),
        false,
    );
}

async function connectHttp2Transport(request, explicitH2) {
    assertHttpNetworkAllowed(request.network, request.url);
    if (request.url.scheme === "http") {
        const bridge = requireNetBridge();
        const remainingMs = httpRemainingMs(request.expiresAtMs);
        if (remainingMs <= 0) {
            throw httpRequestTimeoutError();
        }
        return {
            connection: new TcpConnection(
                bridge,
                await bridge.connect({
                    host: request.url.host,
                    port: request.url.port,
                    timeoutMs: remainingMs === Infinity ? request.timeoutMs : remainingMs,
                    noDelay: true,
                }),
            ),
            protocol: "h2c",
        };
    }
    const bridge = requireNetBridge();
    const remainingMs = httpRemainingMs(request.expiresAtMs);
    if (remainingMs <= 0) {
        throw httpRequestTimeoutError();
    }
    if (typeof bridge.connectTls !== "function") {
        throw httpClientTlsUnavailableError();
    }
    if (bridge.tlsAlpn !== true) {
        if (explicitH2) {
            throw httpClientError(
                "HttpClientTlsUnavailableError",
                "SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE",
                "HTTP/2 over TLS requires an ALPN-capable outbound TLS bridge.",
            );
        }
        return undefined;
    }
    assertHttpTlsBridgeCapabilities(bridge, request.tls, request.operation);
    const handle = await bridge.connectTls({
        host: request.url.host,
        port: request.url.port,
        timeoutMs: remainingMs === Infinity ? request.timeoutMs : remainingMs,
        noDelay: true,
        serverName: request.url.host,
        tls: request.tls,
        alpnProtocols: ["h2", "http/1.1"],
    });
    const connection = new TcpConnection(bridge, handle);
    if (handle.selectedProtocol !== "h2") {
        if (explicitH2) {
            await connection.close().catch(() => {});
            throw httpClientError(
                "HttpClientConnectError",
                "SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED",
                "HTTP/2 TLS connection did not negotiate h2 with ALPN.",
            );
        }
        return { connection, protocol: "http/1.1" };
    }
    return { connection, protocol: "h2" };
}

async function openHttp2ClientSession(request, pool, originKey) {
    const transport = await connectHttp2Transport(request, true);
    const session = new Http2ClientSession(transport.connection, pool, originKey);
    try {
        await session.start();
        return session;
    } catch (error) {
        await session.close().catch(() => {});
        throw error;
    }
}

async function sendHttp1RequestOnConnection(request, connection, lifecycle, keepAlive) {
    lifecycle.connection = connection;
    try {
        await connection.write(serializeHttpRequest(request, keepAlive));
        return await readHttpResponse(connection, request);
    } finally {
        if (lifecycle.connection === connection) {
            lifecycle.connection = undefined;
        }
    }
}

async function sendHttp2RequestOnce(request, pool, lifecycle) {
    let session;
    try {
        if (request.protocol === "h2c" && request.url.scheme !== "http") {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                "HTTP/2 h2c requires an http:// URL.",
            );
        }
        if (request.protocol === "h2" && request.url.scheme !== "https") {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                "HTTP/2 h2 requires an https:// URL.",
            );
        }
        const originKey = httpOriginKey(request.url);
        if (pool !== undefined) {
            const lease = await pool.acquireHttp2(originKey, () =>
                openHttp2ClientSession(request, pool, originKey),
            );
            return await lease.session.request(request, lifecycle);
        }
        session = await openHttp2ClientSession(request, undefined, originKey);
        return await session.request(request, lifecycle);
    } finally {
        if (pool === undefined && session !== undefined) {
            await session.close().catch(() => {});
        }
    }
}

async function sendHttpAutoTlsRequestOnce(request, pool, lifecycle) {
    const originKey = httpOriginKey(request.url);
    const existing = pool?.peekHttp2(originKey);
    if (existing !== undefined) {
        return await existing.request(request, lifecycle);
    }

    const transport = await connectHttp2Transport(request, false);
    if (transport === undefined) {
        return undefined;
    }
    if (transport.protocol !== "h2") {
        try {
            return await sendHttp1RequestOnConnection(request, transport.connection, lifecycle, false);
        } finally {
            await transport.connection.close().catch(() => {});
        }
    }

    const session = new Http2ClientSession(transport.connection, pool, originKey);
    try {
        await session.start();
        if (pool !== undefined) {
            pool.adoptHttp2(originKey, session);
            return await session.request(request, lifecycle);
        } else {
            try {
                return await session.request(request, lifecycle);
            } finally {
                await session.close().catch(() => {});
            }
        }
    } catch (error) {
        await session.close().catch(() => {});
        throw error;
    }
}

function normalizeHttpOptionsObject(value, operation) {
    if (value === undefined || value === null) {
        return undefined;
    }
    if (!isPlainObject(value)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} options must be a plain object.`,
        );
    }
    return value;
}

async function normalizeHttpRequest(baseOptions, request, options, defaultMethod) {
    const operation = "HttpClient.request";
    const requestOptions = typeof request === "string" ? normalizeHttpOptionsObject(options, operation) : undefined;
    const requestObject = typeof request === "string" ? { ...(requestOptions ?? {}), url: request } : request;
    if (!isPlainObject(requestObject)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            "HttpClient.request requires a request object or URL string.",
        );
    }
    const url = resolveHttpUrl(baseOptions, requestObject.url, operation);
    const method = normalizeHttpMethod(defaultMethod ?? requestObject.method, operation);
    const headers = new Map();
    appendHttpHeaders(headers, baseOptions?.headers, operation);
    appendHttpHeaders(headers, requestObject.headers, operation);
    const redirects = normalizeHttpRedirectPolicy(baseOptions, requestObject, operation);
    const network = normalizeHttpNetworkPolicy(baseOptions, requestObject, operation);
    const protocol = normalizeHttpProtocol(baseOptions, requestObject, operation);
    const tls = normalizeHttpTlsOptions(
        requestObject.tls === undefined ? baseOptions?.tls : requestObject.tls,
        operation,
    );
    assertHttpTlsAllowedForScheme(url, tls, operation);
    const maxRequestBytes =
        parseHttpSize(requestObject.maxRequestBytes ?? baseOptions?.maxRequestBytes, operation) ??
        HTTP_CLIENT_DEFAULT_MAX_REQUEST_BYTES;
    const maxResponseBytes =
        parseHttpSize(requestObject.maxResponseBytes ?? baseOptions?.maxResponseBytes, operation) ??
        HTTP_CLIENT_DEFAULT_MAX_RESPONSE_BYTES;
    const maxHeaderBytes =
        parseHttpSize(requestObject.maxHeaderBytes ?? baseOptions?.maxHeaderBytes, operation) ??
        HTTP_CLIENT_DEFAULT_MAX_HEADER_BYTES;
    const timeoutMs = requestObject.timeoutMs ?? baseOptions?.timeoutMs;
    if (timeoutMs !== undefined && (!Number.isFinite(timeoutMs) || timeoutMs < 0)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} timeoutMs must be a non-negative number.`,
        );
    }
    const deadlineMs = httpDeadlineRemainingMs(requestObject.deadline ?? baseOptions?.deadline, operation);
    const signal = requestObject.signal ?? baseOptions?.signal;
    if (signal !== undefined && !isHttpCancellationSignal(signal)) {
        throw httpClientError(
            "HttpClientInvalidOptionsError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
            `${operation} signal must be a Sloppy cancellation signal or AbortSignal-like object.`,
        );
    }
    const timeoutBudgetMs = timeoutMs === undefined ? Infinity : Math.ceil(timeoutMs);
    const timeoutDelayMs = Math.min(timeoutBudgetMs, Math.ceil(deadlineMs));
    const expiresAtMs = timeoutDelayMs === Infinity ? Infinity : Date.now() + timeoutDelayMs;
    const body = await normalizeHttpBody(requestObject, headers, operation, maxRequestBytes, {
        signal,
        expiresAtMs,
    });
    const remainingTimeoutMs = httpRemainingMs(expiresAtMs);
    return Object.freeze({
        url,
        method,
        headers,
        body,
        signal,
        timeoutMs: timeoutBudgetMs === Infinity ? undefined : timeoutBudgetMs,
        timeoutDelayMs: remainingTimeoutMs,
        maxHeaderBytes,
        maxRequestBytes,
        maxResponseBytes,
        redirects,
        network,
        protocol,
        tls,
        operation,
        expiresAtMs,
    });
}

function serializeHttpRequest(request, keepAlive = false) {
    const lines = [
        `${request.method} ${request.url.target} HTTP/1.1`,
        `Host: ${request.url.hostHeader}`,
        keepAlive ? "Connection: keep-alive" : "Connection: close",
    ];
    if (!request.headers.has("accept")) {
        lines.push("Accept: */*");
    }
    for (const { name, value } of request.headers.values()) {
        lines.push(`${name}: ${value}`);
    }
    if (request.body.byteLength > 0) {
        lines.push(`Content-Length: ${request.body.byteLength}`);
    }
    lines.push("", "");
    const head = Text.utf8.encode(lines.join("\r\n"));
    const bytes = new Uint8Array(head.byteLength + request.body.byteLength);
    bytes.set(head, 0);
    bytes.set(request.body, head.byteLength);
    return bytes;
}

function mapHttpTransportError(error) {
    const message = String(error?.message ?? error);
    if (message.includes("SLOPPY_E_HTTP_CLIENT_TLS_HOSTNAME_MISMATCH")) {
        return httpClientError(
            "HttpClientTlsHostnameMismatchError",
            "SLOPPY_E_HTTP_CLIENT_TLS_HOSTNAME_MISMATCH",
            "HTTP client TLS hostname verification failed.",
            { cause: error },
        );
    }
    if (message.includes("SLOPPY_E_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED")) {
        return httpClientError(
            "HttpClientTlsCertificateValidationError",
            "SLOPPY_E_HTTP_CLIENT_TLS_CERTIFICATE_VALIDATION_FAILED",
            "HTTP client TLS certificate validation failed.",
            { cause: error },
        );
    }
    if (message.includes("SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE")) {
        return httpClientError(
            "HttpClientTlsUnavailableError",
            "SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE",
            "HTTP client TLS backend is unavailable.",
            { cause: error },
        );
    }
    if (message.includes("SLOPPY_E_NET_DNS_FAILURE")) {
        return httpClientError(
            "HttpClientDnsError",
            "SLOPPY_E_HTTP_CLIENT_DNS_FAILED",
            "HTTP client DNS resolution failed.",
            { cause: error },
        );
    }
    if (message.includes("SLOPPY_E_NET_CONNECT_TIMEOUT")) {
        return httpClientError(
            "HttpClientTimeoutError",
            "SLOPPY_E_HTTP_CLIENT_REQUEST_TIMEOUT",
            "HTTP client request timed out.",
            { cause: error },
        );
    }
    if (message.includes("SLOPPY_E_NET_CONNECT_CANCELLED")) {
        return httpClientError(
            "HttpClientCancelledError",
            "SLOPPY_E_HTTP_CLIENT_REQUEST_CANCELLED",
            "HTTP client request was cancelled.",
            { cause: error },
        );
    }
    return httpClientError(
        "HttpClientConnectError",
        "SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED",
        "HTTP client transport operation failed.",
        { cause: error },
    );
}

function httpClientTlsUnavailableError() {
    return httpClientError(
        "HttpClientTlsUnavailableError",
        "SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE",
        "HTTPS requires the private outbound TLS bridge.",
    );
}

function httpErrorMessageChain(error) {
    let text = "";
    let current = error;
    for (let depth = 0; depth < 5 && current !== undefined && current !== null; depth += 1) {
        text += ` ${String(current.message ?? current)}`;
        current = current.cause;
    }
    return text;
}

function isSafeHttpRetryMethod(method) {
    return method === "GET" || method === "HEAD";
}

function isStalePooledConnectionError(error) {
    const message = httpErrorMessageChain(error);
    return (
        message.includes("SLOPPY_E_NET_CONNECTION_CLOSED") ||
        message.includes("ECONNRESET") ||
        message.includes("EPIPE")
    );
}

async function sendHttpRequestOnce(request, pool, lifecycle) {
    if (request.protocol === "h2" || request.protocol === "h2c") {
        return await sendHttp2RequestOnce(request, pool, lifecycle);
    }
    if (request.protocol === "auto" && request.url.scheme === "https") {
        const negotiated = await sendHttpAutoTlsRequestOnce(request, pool, lifecycle);
        if (negotiated !== undefined) {
            return negotiated;
        }
    }
    let connection;
    let originKey;
    let reusable = false;
    let released = false;
    const acquireConnection = async () => {
        assertHttpNetworkAllowed(request.network, request.url);
        const remainingMs = httpRemainingMs(request.expiresAtMs);
        if (remainingMs <= 0) {
            throw httpRequestTimeoutError();
        }
        const bridge = requireNetBridge();
        const connectOptions = {
            host: request.url.host,
            port: request.url.port,
            timeoutMs: remainingMs === Infinity ? request.timeoutMs : remainingMs,
            noDelay: true,
        };
        if (request.url.scheme === "https") {
            if (typeof bridge.connectTls !== "function") {
                throw httpClientTlsUnavailableError();
            }
            assertHttpTlsBridgeCapabilities(bridge, request.tls, request.operation);
            return new TcpConnection(
                bridge,
                await bridge.connectTls({
                    ...connectOptions,
                    serverName: request.url.host,
                    tls: request.tls,
                }),
            );
        }
        return new TcpConnection(bridge, await bridge.connect(connectOptions));
    };

    for (let attempt = 0; attempt < 2; attempt += 1) {
        connection = undefined;
        reusable = false;
        released = false;
        let reused = false;
        let createdAt = Date.now();
        try {
            originKey = httpOriginKey(request.url);
            if (pool === undefined) {
                connection = await acquireConnection();
            } else {
                const lease = await pool.acquire(originKey, acquireConnection, {
                    signal: request.signal,
                    expiresAtMs: request.expiresAtMs,
                });
                connection = lease.connection;
                reused = lease.reused;
                createdAt = lease.createdAt;
            }
            lifecycle.connection = connection;
            await connection.write(serializeHttpRequest(request, pool !== undefined));
            const response = await readHttpResponse(connection, request);
            reusable = response._connectionReusable === true;
            return response;
        } catch (error) {
            if (
                connection !== undefined &&
                pool !== undefined &&
                reused &&
                attempt === 0 &&
                isSafeHttpRetryMethod(request.method) &&
                isStalePooledConnectionError(error)
            ) {
                released = true;
                if (lifecycle.connection === connection) {
                    lifecycle.connection = undefined;
                }
                await pool.release(originKey, connection, false, createdAt).catch(() => {});
                continue;
            }
            throw error;
        } finally {
            if (lifecycle.connection === connection) {
                lifecycle.connection = undefined;
            }
            if (connection !== undefined && !released) {
                released = true;
                if (pool === undefined) {
                    await connection.close().catch(() => {});
                } else {
                    await pool.release(originKey, connection, reusable, createdAt).catch(() => {});
                }
            }
        }
    }
    throw httpClientError(
        "HttpClientConnectError",
        "SLOPPY_E_HTTP_CLIENT_CONNECT_FAILED",
        "HTTP client transport operation failed.",
    );
}

async function sendHttpRequestWithRedirects(normalized, pool, lifecycle) {
    if (typeof globalThis.__sloppy?.net?.connect !== "function") {
        return await httpClientUnavailable("request");
    }
    let current = normalized;
    const visited = new Set([httpUrlToString(current.url)]);
    let redirectCount = 0;

    while (true) {
        const response = await sendHttpRequestOnce(current, pool, lifecycle);
        if (!current.redirects.enabled || !isHttpRedirectStatus(response.status)) {
            return response;
        }
        const location = response.headers.get("location");
        if (location === null) {
            return response;
        }
        if (redirectCount >= current.redirects.max) {
            throw httpClientError(
                "HttpClientMaxRedirectsError",
                "SLOPPY_E_HTTP_CLIENT_MAX_REDIRECTS_EXCEEDED",
                "HTTP client exceeded the configured redirect limit.",
            );
        }
        if (current.method !== "GET" && current.method !== "HEAD" && !current.redirects.allowPost) {
            throw httpClientError(
                "HttpClientInvalidOptionsError",
                "SLOPPY_E_HTTP_CLIENT_INVALID_OPTIONS",
                "HTTP client redirect for request body methods requires redirects.allowPost.",
            );
        }
        const nextUrl = resolveHttpRedirectUrl(current.url, location, current.operation);
        const nextUrlText = httpUrlToString(nextUrl);
        if (visited.has(nextUrlText)) {
            throw httpClientError(
                "HttpClientRedirectLoopError",
                "SLOPPY_E_HTTP_CLIENT_REDIRECT_LOOP",
                "HTTP client detected a redirect loop.",
            );
        }
        visited.add(nextUrlText);
        redirectCount += 1;
        const headers = cloneHttpHeaders(current.headers);
        if (httpOriginKey(nextUrl) !== httpOriginKey(current.url)) {
            stripHttpSensitiveHeaders(headers, current.redirects);
        }
        let method = current.method;
        let body = current.body;
        const hasBody = current.body.byteLength > 0;
        if (
            current.method !== "HEAD" &&
            (response.status === 303 ||
                ((response.status === 301 || response.status === 302) &&
                    (hasBody || current.method === "POST" || current.method === "PUT" || current.method === "PATCH")))
        ) {
            method = "GET";
            body = new Uint8Array(0);
            headers.delete("content-length");
            headers.delete("content-type");
        }
        current = Object.freeze({ ...current, url: nextUrl, headers, method, body });
    }
}

async function sendHttpRequest(baseOptions, request, options = undefined, defaultMethod = undefined, pool = undefined) {
    const normalized = await normalizeHttpRequest(baseOptions, request, options, defaultMethod);
    let timeoutId;
    let timedOut = false;
    let cancelled = false;
    let cancelReason;
    let cleanupCancellation = () => {};
    const lifecycle = { connection: undefined };
    const abortLifecycle = (reason) => {
        if (typeof lifecycle.abort === "function") {
            lifecycle.abort(reason);
            return;
        }
        lifecycle.connection?.abort().catch(() => {});
    };
    const run = async () => {
        return await sendHttpRequestWithRedirects(normalized, pool, lifecycle);
    };

    try {
        if (isHttpCancellationSignal(normalized.signal) && normalized.signal.aborted) {
            cancelled = true;
            cancelReason = normalized.signal.reason;
            throw httpRequestCancelledError();
        }
        if (normalized.timeoutDelayMs <= 0) {
            throw httpRequestTimeoutError();
        }
        if (normalized.timeoutDelayMs === Infinity && normalized.signal === undefined) {
            return await run();
        }
        return await Promise.race([
            run(),
            new Promise((_, reject) => {
                cleanupCancellation = subscribeHttpCancellation(normalized.signal, (reason) => {
                    cancelled = true;
                    cancelReason = reason;
                    abortLifecycle("cancel");
                    reject(httpRequestCancelledError());
                });
                if (normalized.timeoutDelayMs !== Infinity) {
                    timeoutId = setTimeout(() => {
                        timedOut = true;
                        abortLifecycle("timeout");
                        reject(httpRequestTimeoutError());
                    }, normalized.timeoutDelayMs);
                }
            }),
        ]);
    } catch (error) {
        if (cancelled) {
            throw httpClientError(
                "HttpClientCancelledError",
                "SLOPPY_E_HTTP_CLIENT_REQUEST_CANCELLED",
                "HTTP client request was cancelled.",
                { cause: error, reason: cancelReason },
            );
        }
        if (timedOut) {
            throw httpClientError(
                "HttpClientTimeoutError",
                "SLOPPY_E_HTTP_CLIENT_REQUEST_TIMEOUT",
                "HTTP client request timed out.",
                { cause: error },
            );
        }
        if (error instanceof SloppyNetError &&
            typeof error.code === "string" &&
            error.code.startsWith("SLOPPY_E_HTTP_CLIENT_"))
        {
            throw error;
        }
        throw mapHttpTransportError(error);
    } finally {
        cleanupCancellation();
        if (timeoutId !== undefined) {
            clearTimeout(timeoutId);
        }
    }
}

function withHttpDefaultHeader(options, name, value) {
    const normalizedOptions = normalizeHttpOptionsObject(options, "HttpClient.request");
    const merged = { ...(normalizedOptions ?? {}) };
    if (normalizedOptions?.headers !== undefined && !isPlainObject(normalizedOptions.headers)) {
        return merged;
    }
    const headers = { ...(isPlainObject(normalizedOptions?.headers) ? normalizedOptions.headers : {}) };
    const normalizedName = name.toLowerCase();
    const hasHeader = Object.keys(headers).some((key) => key.toLowerCase() === normalizedName);
    if (!hasHeader) {
        headers[name] = value;
    }
    merged.headers = headers;
    return merged;
}

async function readHttpResponseBody(response, kind) {
    if (kind === "json") {
        return await response.json();
    }
    if (kind === "text") {
        return await response.text();
    }
    return await response.bytes();
}

async function sendHttpBodyRequest(baseOptions, url, options, kind, pool = undefined) {
    const requestOptions =
        kind === "json" ? withHttpDefaultHeader(options, "Accept", "application/json") : options;
    const response = await sendHttpRequest(baseOptions, url, requestOptions, "GET", pool);
    return await readHttpResponseBody(response, kind);
}

function postJsonRequest(baseOptions, url, value, options = undefined, pool = undefined) {
    const normalizedOptions = normalizeHttpOptionsObject(options, "HttpClient.request");
    return sendHttpRequest(baseOptions, url, { ...(normalizedOptions ?? {}), json: value }, "POST", pool);
}

function createHttpClientFacade(baseOptions = undefined) {
    const rawBaseOptions = normalizeHttpOptionsObject(baseOptions, "HttpClient.create");
    const normalizedTls = normalizeHttpTlsOptions(rawBaseOptions?.tls, "HttpClient.create");
    const poolOptions = normalizeHttpPoolOptions(rawBaseOptions?.pool, "HttpClient.create");
    baseOptions = createHttpClientBaseOptions(rawBaseOptions, normalizedTls, poolOptions);
    const descriptor = sanitizeHttpClientOptionsDescriptor(
        rawBaseOptions,
        normalizedTls,
        poolOptions,
    );
    const pool = poolOptions === undefined ? undefined : new HttpConnectionPool(poolOptions);
    let closed = false;
    let closePromise;
    function assertOpen() {
        if (closed) {
            throw httpClientError(
                "HttpClientClosedError",
                "SLOPPY_E_HTTP_CLIENT_CLOSED",
                "HTTP client is closed.",
            );
        }
    }
    function requestOpen(request, options, defaultMethod) {
        assertOpen();
        return sendHttpRequest(baseOptions, request, options, defaultMethod, pool);
    }
    function closeClient() {
        if (closePromise === undefined) {
            closed = true;
            closePromise = Promise.resolve(pool?.close()).then(() => undefined);
        }
        return closePromise;
    }
    const client = {
        request(request, options = undefined) {
            return requestOpen(request, options, undefined);
        },
        get(url, options = undefined) {
            return requestOpen(url, options, "GET");
        },
        post(url, options = undefined) {
            return requestOpen(url, options, "POST");
        },
        put(url, options = undefined) {
            return requestOpen(url, options, "PUT");
        },
        patch(url, options = undefined) {
            return requestOpen(url, options, "PATCH");
        },
        delete(url, options = undefined) {
            return requestOpen(url, options, "DELETE");
        },
        head(url, options = undefined) {
            return requestOpen(url, options, "HEAD");
        },
        getJson(url, options = undefined) {
            return sendHttpBodyRequest(baseOptions, url, options, "json", pool);
        },
        postJson(url, value, options = undefined) {
            return postJsonRequest(baseOptions, url, value, options, pool);
        },
        text(url, options = undefined) {
            return sendHttpBodyRequest(baseOptions, url, options, "text", pool);
        },
        json(url, options = undefined) {
            return sendHttpBodyRequest(baseOptions, url, options, "json", pool);
        },
        bytes(url, options = undefined) {
            return sendHttpBodyRequest(baseOptions, url, options, "bytes", pool);
        },
        poolStats() {
            return pool?.stats() ?? Object.freeze({
                connectionsCreated: 0,
                connectionsReused: 0,
                connectionsClosedIdle: 0,
                connectionsClosed: 0,
                poolWaitCount: 0,
                poolRejectedCount: 0,
                activeRequests: 0,
                idleConnections: 0,
                queuedRequests: 0,
            });
        },
        close() {
            return closeClient();
        },
        dispose() {
            return closeClient();
        },
    };
    Object.defineProperty(client, "__sloppyHttpClientOptions", {
        value: descriptor,
        enumerable: false,
    });
    return Object.freeze(client);
}

const HttpClient = Object.freeze({
    create(options = undefined) {
        return createHttpClientFacade(options);
    },
    request(request, options = undefined) {
        return sendHttpRequest(undefined, request, options);
    },
    get(url, options = undefined) {
        return sendHttpRequest(undefined, url, options, "GET");
    },
    post(url, options = undefined) {
        return sendHttpRequest(undefined, url, options, "POST");
    },
    put(url, options = undefined) {
        return sendHttpRequest(undefined, url, options, "PUT");
    },
    patch(url, options = undefined) {
        return sendHttpRequest(undefined, url, options, "PATCH");
    },
    delete(url, options = undefined) {
        return sendHttpRequest(undefined, url, options, "DELETE");
    },
    head(url, options = undefined) {
        return sendHttpRequest(undefined, url, options, "HEAD");
    },
    getJson(url, options = undefined) {
        return sendHttpBodyRequest(undefined, url, options, "json");
    },
    postJson(url, value, options = undefined) {
        return postJsonRequest(undefined, url, value, options);
    },
    text(url, options = undefined) {
        return sendHttpBodyRequest(undefined, url, options, "text");
    },
    json(url, options = undefined) {
        return sendHttpBodyRequest(undefined, url, options, "json");
    },
    bytes(url, options = undefined) {
        return sendHttpBodyRequest(undefined, url, options, "bytes");
    },
});

export {
    HttpClient,
    LocalEndpoint,
    NamedPipe,
    NetworkAddress,
    SloppyNetError,
    TcpClient,
    TcpConnection,
    TcpListener,
    UnixSocket,
};
