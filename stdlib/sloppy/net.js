class SloppyNetError extends Error {
    constructor(name, message, options) {
        super(message);
        this.name = name;
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
    if (rest.length === 0 || rest.startsWith("/") || rest.includes("\\") || rest.split("/").includes("..")) {
        throw new TypeError(`${operation} path must stay inside the named root.`);
    }
    return path;
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
    const normalized = { path: normalizeLocalEndpointPath(options.path, operation) };
    if (options.backend !== undefined) {
        if (options.backend !== "unix" && options.backend !== "namedPipe") {
            throw new TypeError(`${operation} backend must be "unix" or "namedPipe" when specified.`);
        }
        normalized.backend = options.backend;
    }
    if (options.timeoutMs !== undefined) {
        if (!Number.isFinite(options.timeoutMs) || options.timeoutMs < 0) {
            throw new TypeError(`${operation} timeoutMs must be a non-negative number.`);
        }
        normalized.timeoutMs = Math.ceil(options.timeoutMs);
    }
    return normalized;
}

function normalizeLocalListenOptions(options, operation) {
    const normalized = normalizeLocalConnectOptions(options, operation);
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
    return normalized;
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

    async accept(options = undefined) {
        if (this._closed) {
            throw new SloppyNetError("LocalEndpointDisposedError", "Local endpoint server is closed.");
        }
        const timeoutMs = normalizeTimeoutOption(options, "LocalEndpoint.accept");
        if (typeof this._bridge.acceptLocal !== "function") {
            localIpcUnavailable();
        }
        return this._bridge.acceptLocal(this._handle, timeoutMs);
    }

    acceptLoop(options = undefined) {
        const server = this;
        return {
            async *[Symbol.asyncIterator]() {
                while (!server.closed) {
                    yield await server.accept(options);
                }
            },
        };
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

const LocalEndpoint = Object.freeze({
    async connect(options) {
        const bridge = requireNetBridge();
        const normalized = normalizeLocalConnectOptions(options, "LocalEndpoint.connect");
        if (typeof bridge.connectLocal !== "function") {
            localIpcUnavailable();
        }
        return bridge.connectLocal(normalized);
    },

    async listen(options) {
        const bridge = requireNetBridge();
        const normalized = normalizeLocalListenOptions(options, "LocalEndpoint.listen");
        if (typeof bridge.listenLocal !== "function") {
            localIpcUnavailable();
        }
        return new LocalEndpointServer(bridge, await bridge.listenLocal(normalized));
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

function normalizeTimeoutOption(options, operation) {
    if (options === undefined) {
        return undefined;
    }
    if (!isPlainObject(options)) {
        throw new TypeError(`${operation} options must be a plain object.`);
    }
    for (const key of Object.keys(options)) {
        if (key !== "timeoutMs") {
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
        await this.write(new TextEncoder().encode(text));
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
            typeof delimiter === "string" ? new TextEncoder().encode(delimiter) : delimiter;
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

function httpClientUnavailable(operation) {
    return Promise.reject(
        new SloppyNetError(
            "HttpClientUnavailableError",
            `SLOPPY_E_HTTP_CLIENT_FEATURE_UNAVAILABLE: HttpClient.${operation} is contract-visible, but the outbound HTTP client transport is not implemented in this runtime lane.`,
        ),
    );
}

function createHttpClientFacade(baseOptions = undefined) {
    const client = {
        request() {
            return httpClientUnavailable("request");
        },
        get() {
            return httpClientUnavailable("get");
        },
        post() {
            return httpClientUnavailable("post");
        },
        getJson() {
            return httpClientUnavailable("getJson");
        },
        postJson() {
            return httpClientUnavailable("postJson");
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
    request() {
        return httpClientUnavailable("request");
    },
    get() {
        return httpClientUnavailable("get");
    },
    post() {
        return httpClientUnavailable("post");
    },
    getJson() {
        return httpClientUnavailable("getJson");
    },
    postJson() {
        return httpClientUnavailable("postJson");
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
