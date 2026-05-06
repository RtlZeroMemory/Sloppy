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

function normalizeTimeoutOption(options, operation) {
    if (options === undefined) {
        return undefined;
    }
    if (!isPlainObject(options)) {
        throw new TypeError(`${operation} options must be a plain object.`);
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

export { NetworkAddress, SloppyNetError, TcpClient, TcpConnection, TcpListener };
