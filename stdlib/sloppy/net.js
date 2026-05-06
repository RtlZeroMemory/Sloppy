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
    return Object.freeze({ maxConnectionsPerOrigin, idleTimeoutMs });
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
    }

    _entry(originKey) {
        let entry = this._entries.get(originKey);
        if (entry === undefined) {
            entry = { idle: [], total: 0, inUse: 0 };
            this._entries.set(originKey, entry);
        }
        return entry;
    }

    _prune(originKey, entry) {
        if (entry.total === 0 && entry.inUse === 0 && entry.idle.length === 0) {
            this._entries.delete(originKey);
        }
    }

    async acquire(originKey, connect) {
        const entry = this._entry(originKey);
        const idle = entry.idle.pop();
        if (idle !== undefined) {
            clearTimeout(idle.timer);
            entry.inUse += 1;
            return { connection: idle.connection, reused: true };
        }
        if (entry.total >= this._options.maxConnectionsPerOrigin) {
            throw httpClientError(
                "HttpClientPoolExhaustedError",
                "SLOPPY_E_HTTP_CLIENT_POOL_EXHAUSTED",
                "HTTP client connection pool exhausted for origin.",
            );
        }
        entry.total += 1;
        entry.inUse += 1;
        try {
            return { connection: await connect(), reused: false };
        } catch (error) {
            entry.total -= 1;
            entry.inUse -= 1;
            this._prune(originKey, entry);
            throw error;
        }
    }

    async release(originKey, connection, reusable) {
        const entry = this._entries.get(originKey);
        if (entry === undefined) {
            await connection.close().catch(() => {});
            return;
        }
        entry.inUse -= 1;
        if (reusable && this._options.idleTimeoutMs > 0) {
            const timer = setTimeout(() => {
                const index = entry.idle.findIndex((idle) => idle.connection === connection);
                if (index >= 0) {
                    entry.idle.splice(index, 1);
                    entry.total -= 1;
                    connection.close().catch(() => {});
                    this._prune(originKey, entry);
                }
            }, this._options.idleTimeoutMs);
            entry.idle.push({ connection, timer });
            return;
        }
        entry.total -= 1;
        await connection.close().catch(() => {});
        this._prune(originKey, entry);
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
            `${operation} requires an absolute http:// URL or a baseUrl-relative path.`,
        );
    }
    const scheme = url.slice(0, schemeEnd).toLowerCase();
    if (scheme === "https") {
        throw httpClientError(
            "HttpClientTlsUnavailableError",
            "SLOPPY_E_HTTP_CLIENT_TLS_BACKEND_UNAVAILABLE",
            "HTTPS requires the later TLS backend slice; no custom TLS fallback is available.",
        );
    }
    if (scheme !== "http") {
        throw httpClientError(
            "HttpClientInvalidUrlError",
            "SLOPPY_E_HTTP_CLIENT_INVALID_URL",
            `${operation} supports http:// URLs in this slice.`,
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

    const port = parseHttpPort(portText, operation) ?? 80;
    const headerPort = port === 80 ? "" : `:${port}`;
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
    } finally {
        if (!completed && typeof iterator.return === "function") {
            try {
                const returned = Promise.resolve(iterator.return());
                returned.catch(() => {});
                await Promise.race([returned, Promise.resolve()]);
            } catch {
            }
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
            "HTTP response Transfer-Encoding is not supported in this slice.",
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
        return await TcpClient.connect({
            host: request.url.host,
            port: request.url.port,
            timeoutMs: remainingMs === Infinity ? request.timeoutMs : remainingMs,
            noDelay: true,
        });
    };

    for (let attempt = 0; attempt < 2; attempt += 1) {
        connection = undefined;
        reusable = false;
        released = false;
        let reused = false;
        try {
            originKey = httpOriginKey(request.url);
            if (pool === undefined) {
                connection = await acquireConnection();
            } else {
                const lease = await pool.acquire(originKey, acquireConnection);
                connection = lease.connection;
                reused = lease.reused;
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
                await pool.release(originKey, connection, false).catch(() => {});
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
                    await pool.release(originKey, connection, reusable).catch(() => {});
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
                    lifecycle.connection?.abort().catch(() => {});
                    reject(httpRequestCancelledError());
                });
                if (normalized.timeoutDelayMs !== Infinity) {
                    timeoutId = setTimeout(() => {
                        timedOut = true;
                        lifecycle.connection?.abort().catch(() => {});
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
        if (error instanceof SloppyNetError && String(error.message).startsWith("SLOPPY_E_HTTP_CLIENT_")) {
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
    baseOptions = normalizeHttpOptionsObject(baseOptions, "HttpClient.create");
    const poolOptions = normalizeHttpPoolOptions(baseOptions?.pool, "HttpClient.create");
    const pool = poolOptions === undefined ? undefined : new HttpConnectionPool(poolOptions);
    const client = {
        request(request, options = undefined) {
            return sendHttpRequest(baseOptions, request, options, undefined, pool);
        },
        get(url, options = undefined) {
            return sendHttpRequest(baseOptions, url, options, "GET", pool);
        },
        post(url, options = undefined) {
            return sendHttpRequest(baseOptions, url, options, "POST", pool);
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
    };
    Object.defineProperty(client, "__sloppyHttpClientOptions", {
        value: baseOptions,
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
